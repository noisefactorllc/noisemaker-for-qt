// asyncInit overlays (runtime/async_overlay.h, stroke_canvas.h,
// worm_tracer.h): the reference generator and Math functions, the canvas
// model, the upload conversion, parameter semantics, regeneration, and the
// overlay as nm::Backend renders it. Plain executable, run from the repo
// root (WORKING_DIRECTORY in CMakeLists.txt).
//
// Expected values marked "Chromium" were captured from Chromium 153's
// software canvas (headless, Windows x64) running the reference modules;
// the canvas is the same on every host, and parity/check_async_overlay.mjs
// re-measures it against a live browser.

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/async_overlay.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/stroke_canvas.h"
#include "../noisemaker/runtime/worm_tracer.h"

#include <QColor>
#include <QCryptographicHash>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

const QString kDataRoot = QStringLiteral("qt/noisemaker");

QByteArray sha256(const std::vector<std::uint8_t>& bytes) {
    return QCryptographicHash::hash(
               QByteArrayView(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())),
               QCryptographicHash::Sha256)
        .toHex();
}

bool allZero(const std::vector<std::uint8_t>& bytes) {
    for (std::uint8_t b : bytes) {
        if (b) return false;
    }
    return true;
}

// Chromium: new SeededRNG(seed) from shaders/src/cpu/wormTracer.js, then
// next() three times, float(), normal(0.75, 0.125).
void testSeededRng() {
    struct Case {
        double seed;
        std::uint32_t next[3];
        double nextFloat;
        double normal;
    };
    const Case cases[] = {
        {1000.0, {1620979842u, 3759771664u, 3250681287u}, 0.6000252083409636, 0.7374169031593095},
        {31337000.0, {717011962u, 1534072173u, 147532003u}, 0.1873316166892954, 0.6678315186998097},
        // 100411 * 31337: products above 2^53 round, as in JavaScript.
        {3146579507.0, {3332846378u, 2218043408u, 3960114816u}, 0.3862814482735194, 0.6921356036877776},
        {4294967295.5, {3836633866u, 1650125961u, 1696029588u}, 0.4629659118277407, 0.8843023451567588},
        {-7.0, {1789160138u, 2051777833u, 1807167790u}, 0.18321884311344913, 0.7278518722806588},
        {0.0, {718263531u, 106116617u, 3991160471u}, 0.9253103220661427, 0.8797650596022587},
    };
    bool ok = true;
    for (const Case& c : cases) {
        nm::SeededRng rng(c.seed);
        for (std::uint32_t expected : c.next) ok = ok && rng.next() == expected;
        ok = ok && rng.nextFloat() == c.nextFloat;
        ok = ok && rng.normal(0.75, 0.125) == c.normal;
    }
    check(ok, "SeededRng reproduces the reference generator bit for bit (6 seeds, Chromium)");
}

// Arguments where Apple's libm differs from V8 by 1 ulp; V8 values from Chromium.
void testMath() {
    struct Case {
        int kind; // 0 sin, 1 cos, 2 log
        double x;
        double expected;
    };
    const Case cases[] = {
        {0, 0x1.3a03dbcc0aa1fp+1, 0x1.454148086b9cap-1},
        {0, -0x1.ac01ccdee2c7p-1, -0x1.7bdedae364d29p-1},
        {0, 0x1.5cbb4c83e3878p+2, -0x1.7b498454d2446p-1},
        {0, 0x1.24fd1c6d938fdp+16, 0x1.9b83a7c801305p-2},
        {0, 0x1.038dcd4f19851p+8, 0x1.dce22ce1142fp-1},
        {0, -0x1.3392840870a4ep+3, 0x1.7c75eb19ffa22p-3},
        {1, 0x1.09248a8bda324p+13, -0x1.488a776dd0ee5p-1},
        {1, 0x1.53f2a01aa4022p+2, 0x1.20cc8066bf033p-1},
        {1, -0x1.16e4a2e713651p+3, -0x1.847df677e4096p-1},
        {1, -0x1.35687ce306596p+2, 0x1.f2ef3e28df861p-4},
        {1, 0x1.578be01547b38p+1, -0x1.cb5098540749ep-1},
        {1, 0x1.808b6fe6512c4p+7, -0x1.9c23e04c3cbd2p-1},
        {2, 0x1.2cc69205228dcp-2, -0x1.39a09eb45225dp+0},
        {2, 0x1.343d3e62a2878p-3, -0x1.e4cc538fa6e0dp+0},
        {2, 0x1.19433149dbeep-1, -0x1.32b4e3dc1647fp-1},
        {2, 0x1.8cf16374f5a78p-2, -0x1.e53638c2c5e09p-1},
        {2, 0x1.6d0c4dca17288p-1, -0x1.5a6a29a57793fp-2},
        {2, 0x1.091112fa6f8dp-4, -0x1.5e6fba8d86559p+1},
    };
    bool ok = true;
    for (const Case& c : cases) {
        const double got = c.kind == 0 ? nm::detail::jsSin(c.x) : c.kind == 1 ? nm::detail::jsCos(c.x) : nm::detail::jsLog(c.x);
        if (got != c.expected) {
            std::printf("  kind %d x %a: got %a expected %a\n", c.kind, c.x, got, c.expected);
            ok = false;
        }
    }
    check(ok, "Math.sin, Math.cos and Math.log match V8 where the platform libm need not (18 cases, Chromium)");
    const double inf = std::numeric_limits<double>::infinity();
    check(std::signbit(nm::detail::jsSin(-0.0)) && nm::detail::jsSin(-0.0) == 0.0 && nm::detail::jsCos(0.0) == 1.0
              && std::isnan(nm::detail::jsSin(inf)) && std::isnan(nm::detail::jsCos(std::nan("")))
              && nm::detail::jsLog(1.0) == 0.0 && nm::detail::jsLog(0.0) == -inf && std::isnan(nm::detail::jsLog(-1.0)),
          "special values: sin(-0) = -0, cos(0) = 1, sin(inf) = NaN, log(1) = 0, log(0) = -inf, log(-1) = NaN");
}

// Chromium 153's software canvas (willReadFrequently, as the overlays
// create theirs): four opaque white round-capped strokes on a 32x32 canvas;
// the premultiplied alpha of every inked pixel.
void testCanvasStrokes() {
    nm::StrokeCanvas canvas(32, 32);
    const double strokes[4][5] = {
        {10.3, 10.7, 10.5, 10.9, 1.5},
        {20.25, 12.5, 26.8, 9.1, 0.5},
        {5.5, 25.2, 5.5, 28.9, 1.0},
        {24.1, 24.9, 24.35, 25.05, 2.3},
    };
    canvas.setStrokeColor(255, 255, 255, 1.0);
    for (const auto& s : strokes) {
        canvas.setLineWidth(s[4]);
        canvas.strokeLine(s[0], s[1], s[2], s[3]);
    }
    struct Ink {
        int x, y, alpha;
    };
    const Ink expected[] = {
        {26, 8, 33}, {27, 8, 13}, {24, 9, 28}, {25, 9, 94}, {26, 9, 94}, {27, 9, 3}, {9, 10, 79},
        {10, 10, 223}, {11, 10, 36}, {22, 10, 23}, {23, 10, 89}, {24, 10, 99}, {25, 10, 33}, {9, 11, 30},
        {10, 11, 176}, {11, 11, 14}, {20, 11, 18}, {21, 11, 84}, {22, 11, 104}, {23, 11, 38}, {19, 12, 8},
        {20, 12, 108}, {21, 12, 43}, {19, 13, 5}, {23, 23, 14}, {24, 23, 23}, {5, 24, 51}, {23, 24, 227},
        {24, 24, 255}, {25, 24, 74}, {5, 25, 255}, {22, 25, 4}, {23, 25, 218}, {24, 25, 255}, {25, 25, 90},
        {5, 26, 255}, {23, 26, 4}, {24, 26, 64}, {25, 26, 2}, {5, 27, 255}, {5, 28, 255}, {5, 29, 71},
    };
    std::vector<int> want(32 * 32, 0);
    for (const Ink& ink : expected) want[static_cast<size_t>(ink.y * 32 + ink.x)] = ink.alpha;
    const std::vector<std::uint8_t> drawn = canvas.premultiplied();
    int mismatches = 0;
    for (size_t i = 0; i < want.size(); ++i) {
        if (drawn[i * 4 + 3] != want[i]) ++mismatches;
    }
    check(mismatches == 0, "stroke coverage equals the Chromium canvas at every pixel (4 strokes, 42 inked pixels)");

    const std::vector<std::uint8_t> before = canvas.premultiplied();
    canvas.setLineWidth(3.0);
    canvas.strokeLine(16.3, 20.4, 16.3, 20.4);           // zero length
    canvas.strokeLine(16.3, 20.4, 16.3 + 1e-9, 20.4);    // zero length in float
    canvas.strokeLine(33.51, 5.0, 34.0, 6.0);            // bounds outset by 1.5 start at 32.01
    canvas.strokeLine(-2.0, -1.51, -3.0, -2.0);          // entirely above and left
    canvas.setStrokeColor(255, 0, 0, 0.0);
    canvas.strokeLine(16.0, 16.0, 18.0, 16.0);           // transparent
    canvas.setStrokeColor(255, 0, 0, std::nan(""));      // invalid style: kept transparent
    canvas.strokeLine(16.0, 16.0, 18.0, 16.0);
    check(canvas.premultiplied() == before,
          "zero-length, culled, transparent strokes and invalid styles draw nothing");

    nm::StrokeCanvas dot(16, 16);
    dot.setLineWidth(3.0);
    dot.setLineWidth(-1.0);
    dot.setLineWidth(std::numeric_limits<double>::infinity());
    dot.setStrokeColor(255, 255, 255, 1.0);
    dot.strokeLine(8.3, 8.4, 8.31, 8.4);
    const std::vector<std::uint8_t> dotPixels = dot.premultiplied();
    int sum = 0;
    for (size_t i = 3; i < dotPixels.size(); i += 4) sum += dotPixels[i];
    check(sum == 1768,
          "a 0.01 px segment draws a round dot of width 3; width -1 and infinity are ignored (alpha sum 1768, Chromium)");

    canvas.clear();
    check(allZero(canvas.premultiplied()), "clear() makes the canvas transparent");
}

// Measured on Chromium 153's WebGL upload of a software canvas with
// UNPACK_PREMULTIPLY_ALPHA_WEBGL false: every c * 255 / a below is a
// midpoint, which the float product rounds up or down, ties to even.
void testUploadConversion() {
    const std::vector<std::uint8_t> premultiplied = {
        7, 3, 1, 10,        // 178.5 -> 179, 76.5 -> 77, 25.5 -> 26
        1, 7, 21, 34,       // 7.5 -> 8, 52.5 -> 52, 157.5 -> 158
        1, 0, 2, 2,         // 127.5 -> 128, 0 -> 0, 255 -> 255
        5, 1, 6, 6,         // 212.5 -> 212, 42.5 -> 42, 255 -> 255
        100, 33, 3, 200,    // 127.5 -> 128, 42.075 -> 42, 3.825 -> 4
        200, 100, 3, 255,   // alpha 255 keeps the channels
        9, 9, 9, 0,         // alpha 0 -> 0
    };
    const std::vector<std::uint8_t> expected = {
        179, 77, 26, 10,
        8, 52, 158, 34,
        128, 0, 255, 2,
        212, 42, 255, 6,
        128, 42, 4, 200,
        200, 100, 3, 255,
        0, 0, 0, 0,
    };
    check(nm::unpremultiplyForUpload(premultiplied) == expected,
          "upload conversion rounds the float product like Chromium, including its midpoints");
}

// Chromium 153: SHA-256 of the uploaded overlay bytes (straight RGBA8, row 0
// at the top) after the reference asyncInit completed at 256x256.
void testOverlays() {
    const QSize size(256, 256);
    const QJsonObject scratchParams{{QStringLiteral("density"), 0.3}, {QStringLiteral("seed"), 1}};
    const QJsonObject hairParams{{QStringLiteral("density"), 0.5}, {QStringLiteral("seed"), 1}};
    check(sha256(nm::generateAsyncOverlay(QStringLiteral("filter.scratches"), size, scratchParams))
              == QByteArrayLiteral("3fbc037f11404b1b06afabdd1b5bd4fcb73dc92541f325b96b0ec46254f17f14"),
          "filter/scratches 256x256 overlay is byte-identical to Chromium");
    check(sha256(nm::generateAsyncOverlay(QStringLiteral("filter.strayHair"), size, hairParams))
              == QByteArrayLiteral("651f767359ebb52192327d0d04cbea4a183ac65700b67f7a67d4e558ec9bd012"),
          "filter/strayHair 256x256 overlay is byte-identical to Chromium");
    const QJsonObject fiberParams{{QStringLiteral("density"), 1}, {QStringLiteral("seed"), 1}};
    check(sha256(nm::generateAsyncOverlay(QStringLiteral("filter.fibers"), size, fiberParams))
              == QByteArrayLiteral("3772c34c45a0905ea94d1a3097838486c26dd4b40316492dd94a1d2c9e9d4eb5"),
          "filter/fibers 256x256 overlay is byte-identical to Chromium");

    // updateTexture calls: the initial clear, after worms 0, 3, 6, ... and at
    // the end of each layer. Chromium counted 861 for fibers at 256x256.
    nm::StrokeCanvas canvas(256, 256);
    int updates = 0;
    const nm::TraceResult result = nm::runAsyncInit(QStringLiteral("filter.fibers"), canvas, fiberParams, {},
                                                    [&updates](const QString& name) {
                                                        if (name == QStringLiteral("overlayTex")) ++updates;
                                                    });
    check(result == nm::TraceResult::Completed && updates == 861,
          "fibers uploads its overlay 861 times at 256x256, as the reference does");

    int polls = 0;
    nm::StrokeCanvas cancelled(64, 64);
    const nm::TraceResult stopped = nm::runAsyncInit(QStringLiteral("filter.scratches"), cancelled, scratchParams,
                                                     [&polls] { return ++polls > 5; });
    check(stopped == nm::TraceResult::Cancelled, "isCancelled stops the trace");

    check(nm::hasAsyncInit(QStringLiteral("filter.fibers")) && nm::hasAsyncInit(QStringLiteral("filter.scratches"))
              && nm::hasAsyncInit(QStringLiteral("filter.strayHair")) && !nm::hasAsyncInit(QStringLiteral("filter.grain"))
              && nm::asyncInitTextures(QStringLiteral("filter.fibers")) == QStringList{QStringLiteral("overlayTex")},
          "the three asyncInit effects are registered with their overlayTex");
}

// reference: `params.seed || 1`, `params.density !== undefined ? params.density : default`,
// and checkAsyncRegen's scalar test.
void testParams() {
    const QSize size(64, 64);
    const QString fibers = QStringLiteral("filter.fibers");
    const auto overlay = [&](const QJsonObject& params) { return nm::generateAsyncOverlay(fibers, size, params); };
    const QJsonObject seedOne{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), 0.5}};
    check(overlay(QJsonObject{{QStringLiteral("seed"), 0}, {QStringLiteral("density"), 0.5}}) == overlay(seedOne)
              && overlay(QJsonObject{{QStringLiteral("density"), 0.5}}) == overlay(seedOne)
              && overlay(QJsonObject{}) == overlay(seedOne),
          "seed 0 or absent traces as seed 1; absent density uses the default 0.5");
    check(overlay(QJsonObject{{QStringLiteral("seed"), QStringLiteral(" 1 ")}, {QStringLiteral("density"), 0.5}})
                  == overlay(seedOne)
              && overlay(QJsonObject{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), QJsonArray{0.5}}})
                  == overlay(seedOne)
              && overlay(QJsonObject{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), QJsonArray{}}})
                  == overlay(QJsonObject{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), 0}})
              && overlay(QJsonObject{{QStringLiteral("seed"), true}, {QStringLiteral("density"), 0.5}})
                  == overlay(seedOne),
          "strings, one-element arrays, empty arrays and booleans convert as JavaScript ToNumber does");
    check(allZero(overlay(QJsonObject{{QStringLiteral("seed"), QStringLiteral("abc")}, {QStringLiteral("density"), 0.5}})),
          "a non-numeric string seed draws nothing (NaN kink), as in the reference");
    const QJsonObject osc{{QStringLiteral("type"), QStringLiteral("Oscillator")}};
    check(allZero(overlay(QJsonObject{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), osc}})),
          "a non-numeric density (automation) draws nothing, as in the reference");
    check(allZero(overlay(QJsonObject{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), 1e12}})),
          "a density beyond the worm limit stops instead of exhausting memory");

    const QJsonObject uniforms{{QStringLiteral("seed"), 3}, {QStringLiteral("density"), 0.2},
                               {QStringLiteral("alpha"), 0.5}, {QStringLiteral("color"), QJsonArray{0, 0, 0}}};
    const QJsonObject globals{{QStringLiteral("seed"), 9}, {QStringLiteral("time"), 0.25}};
    check(nm::asyncInitParams(fibers, uniforms, globals)
              == QJsonObject({{QStringLiteral("seed"), 3}, {QStringLiteral("density"), 0.2}}),
          "step values supply seed and density");
    const QJsonObject automated{{QStringLiteral("seed"), osc}, {QStringLiteral("density"), osc}};
    check(nm::asyncInitParams(fibers, automated, globals) == QJsonObject({{QStringLiteral("seed"), 9}}),
          "without a scalar seed or density the global uniforms supply them");
    check(nm::asyncInitParams(QStringLiteral("synth.solid"), uniforms, globals).isEmpty(),
          "effects without asyncInit have no params");
}

void testSync(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(64, 64));
    nm::Graph graph = nm::compileGraph(
        QStringLiteral("search filter, synth\nsolid(color: #000000).fibers(density: 1).write(o0)\nrender(o0)"), registry);
    nm::AsyncOverlays overlays;
    const QSize size(64, 64);
    const int first = overlays.sync(backend, graph, size, {});
    const int again = overlays.sync(backend, graph, size, {});
    check(first == 1 && again == 0 && overlays.textureIds() == QStringList{QStringLiteral("node_1_overlayTex")},
          "sync generates node_1_overlayTex once and keeps it while nothing changes");

    backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_1"), QJsonObject{{QStringLiteral("alpha"), 0.9}}}});
    const int afterAlpha = overlays.sync(backend, graph, size, {});
    backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_1"), QJsonObject{{QStringLiteral("seed"), 7}}}});
    const int afterSeed = overlays.sync(backend, graph, size, {});
    const int afterSize = overlays.sync(backend, graph, QSize(32, 32), {});
    check(afterAlpha == 0 && afterSeed == 1 && afterSize == 1,
          "alpha does not regenerate; seed and the render size do");

    const nm::Graph plain = nm::compileGraph(
        QStringLiteral("search synth\nsolid(color: #000000).write(o0)\nrender(o0)"), registry);
    const int none = overlays.sync(backend, plain, size, {});
    check(none == 0 && overlays.textureIds().isEmpty(), "a graph without the node drops its overlay texture");
}

// Largest difference between a frame of fibersBlend over black and the
// blend of `overlay` (straight RGBA8, row 0 at the top) at alpha 0.5:
// overlay.rgb * overlay.a * 0.5. `inked` counts non-transparent overlay pixels.
int blendError(const QImage& frame, QSize frameSize, const std::vector<std::uint8_t>& overlay, int& inked) {
    int worst = frame.size() == frameSize ? 0 : 255;
    inked = 0;
    for (int y = 0; y < frameSize.height() && worst < 255; ++y) {
        const uchar* row = frame.constScanLine(y);
        for (int x = 0; x < frameSize.width(); ++x) {
            const size_t o = static_cast<size_t>(y * frameSize.width() + x) * 4;
            const double a = overlay[o + 3] / 255.0 * 0.5;
            if (overlay[o + 3]) ++inked;
            for (int c = 0; c < 3; ++c) {
                const int expected = static_cast<int>(std::lround(overlay[o + static_cast<size_t>(c)] / 255.0 * a * 255.0));
                worst = std::max(worst, std::abs(expected - static_cast<int>(row[x * 4 + c])));
            }
        }
    }
    return worst;
}

// The port's fibers overlay at `size` for `seed` (density 1).
std::vector<std::uint8_t> fibersOverlay(QSize size, int seed) {
    return nm::generateAsyncOverlay(QStringLiteral("filter.fibers"), size,
                                    QJsonObject{{QStringLiteral("seed"), seed}, {QStringLiteral("density"), 1}});
}

// The overlay as the Backend renders it: fibersBlend over black gives
// overlay.rgb * overlay.a * alpha.
void testRender(nm::EffectRegistry& registry) {
    const QSize size(64, 64);
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, size);
    nm::Graph graph = nm::compileGraph(
        QStringLiteral("search filter, synth\nsolid(color: #000000).fibers(density: 1).write(o0)\nrender(o0)"), registry);
    backend.render(graph, 0.25);
    const QImage first = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);

    int inked = 0;
    const int worst = blendError(first, size, fibersOverlay(size, 1), inked);
    check(inked > 500 && worst <= 1,
          "the first rendered frame blends the completed overlay, upright (within 1 level, fp16 intermediates)");

    backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_1"), QJsonObject{{QStringLiteral("seed"), 5}}}});
    backend.render(graph, 0.25);
    const QImage reseeded = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_1"), QJsonObject{{QStringLiteral("seed"), 1}}}});
    backend.render(graph, 0.25);
    const QImage restored = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    check(reseeded != first && restored == first, "a seed change re-traces on the next render; restoring it restores the frame");

    const QSize resized(48, 40);
    backend.resize(resized);
    backend.render(graph, 0.25);
    int resizedInked = 0;
    const int resizedWorst = blendError(backend.readSurface().convertToFormat(QImage::Format_RGBA8888), resized,
                                        fibersOverlay(resized, 1), resizedInked);
    check(resizedInked > 200 && resizedWorst <= 1, "Backend::resize re-traces the overlay at the new size");

    // reference 3bae8ea0: an automation descriptor is drawn from the param's definition
    // default, so an automated density renders as the default density (0.5) does.
    nm::Graph automated = graph;
    nm::Graph defaulted = graph;
    for (nm::Pass& pass : automated.passes) {
        if (pass.effectKey == QStringLiteral("filter.fibers")) {
            pass.uniforms.insert(QStringLiteral("density"), QJsonObject{{QStringLiteral("type"), QStringLiteral("Oscillator")}});
        }
    }
    for (nm::Pass& pass : defaulted.passes) {
        if (pass.effectKey == QStringLiteral("filter.fibers")) {
            pass.uniforms.insert(QStringLiteral("density"), 0.5);
        }
    }
    backend.render(graph, 0.25);
    const QImage fromDensityOne = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    backend.render(automated, 0.25);
    const QImage fromAutomation = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    backend.render(defaulted, 0.25);
    const QImage fromDefault = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    check(!fromAutomation.isNull() && fromAutomation == fromDefault && fromAutomation != fromDensityOne,
          "an automated density draws the overlay at the definition default, as the reference does");
}

// A host texture under an asyncInit overlay id takes precedence over the
// generated overlay (backend.h): the parity job on llvmpipe passes the
// reference's own overlay this way.
void testHostOverlay(nm::EffectRegistry& registry) {
    const QSize size(64, 64);
    const QString overlayId = QStringLiteral("node_1_overlayTex");
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, size);
    nm::Graph graph = nm::compileGraph(
        QStringLiteral("search filter, synth\nsolid(color: #000000).fibers(density: 1).write(o0)\nrender(o0)"), registry);
    check(nm::asyncOverlayTextureIds(graph) == QStringList{overlayId}
              && nm::asyncOverlayTextureIds(nm::compileGraph(
                     QStringLiteral("search synth\nsolid(color: #000000).write(o0)\nrender(o0)"), registry)).isEmpty(),
          "asyncOverlayTextureIds lists node_1_overlayTex for fibers and nothing for solid");

    QImage host(size, QImage::Format_RGBA8888);
    host.fill(QColor(200, 100, 50, 255));
    const std::vector<std::uint8_t> hostPixels(host.constBits(), host.constBits() + host.sizeInBytes());
    const auto render = [&](const nm::Graph& g) {
        backend.render(g, 0.25);
        return backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    };
    int inked = 0;

    backend.updateTextureFromSource(overlayId, host);
    const QImage hostFrame = render(graph);
    check(blendError(hostFrame, size, hostPixels, inked) <= 1,
          "a host texture set before the first render replaces the traced overlay");

    nm::AsyncOverlays overlays;
    check(overlays.sync(backend, graph, size, {}) == 0 && overlays.textureIds().isEmpty(),
          "AsyncOverlays neither generates nor keeps a node whose texture the host supplies");

    backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_1"), QJsonObject{{QStringLiteral("seed"), 5}}}});
    check(blendError(render(graph), size, hostPixels, inked) <= 1,
          "a seed change does not overwrite the host texture");

    backend.removeExternalTexture(overlayId);
    check(blendError(render(graph), size, fibersOverlay(size, 5), inked) <= 1 && inked > 500,
          "removeExternalTexture returns the node to its generated overlay on the next render");

    backend.updateTextureFromSource(overlayId, host);
    check(blendError(render(graph), size, hostPixels, inked) <= 1,
          "a host texture set after the overlay was generated takes over");

    render(nm::compileGraph(QStringLiteral("search synth\nsolid(color: #000000).write(o0)\nrender(o0)"), registry));
    check(blendError(render(graph), size, hostPixels, inked) <= 1,
          "a graph without the node leaves the host texture in place");
}

const char* const kFibers = "search filter, synth\nsolid(color: #000000).fibers(density: 1).write(o0)\nrender(o0)";

double secondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

// CPU seconds this process has used, all threads.
double processCpuSeconds() {
#if defined(_WIN32)
    FILETIME creation, exit, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) return 0.0;
    const auto seconds = [](const FILETIME& t) {
        return static_cast<double>((static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime) * 1e-7;
    };
    return seconds(kernel) + seconds(user);
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0.0;
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec)
        + static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) * 1e-6;
#endif
}

// The p quantile of `samples` (linear interpolation between order statistics).
double quantile(std::vector<double> samples, double p) {
    std::sort(samples.begin(), samples.end());
    const double rank = p * static_cast<double>(samples.size() - 1);
    const size_t below = static_cast<size_t>(std::floor(rank));
    const size_t above = std::min(below + 1, samples.size() - 1);
    return samples[below] + (samples[above] - samples[below]) * (rank - static_cast<double>(below));
}

void printSamples(const char* label, const std::vector<double>& samples) {
    std::printf("  %s samples ms:", label);
    for (double sample : samples) std::printf(" %.3f", sample);
    std::printf("; count %zu, median %.3f, max %.3f ms\n", samples.size(), quantile(samples, 0.5),
                *std::max_element(samples.begin(), samples.end()));
}

void setSeed(nm::Backend& backend, nm::Graph& graph, nm::EffectRegistry& registry, int seed) {
    backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_1"), QJsonObject{{QStringLiteral("seed"), seed}}}});
}

// True when every pixel of `frame` is opaque black: fibers over black with
// a transparent overlay.
bool blackFrame(const QImage& frame) {
    const QImage rgba = frame.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            if (row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2]) return false;
        }
    }
    return true;
}

// OverlayTraceMode::Background at 1920x1080 (GAP-038): render() returns
// while the trace runs, keeps the previous overlay, and the completed frame
// equals the synchronous mode's byte for byte. Returns the seconds of a
// synchronous 1920x1080 re-trace render.
double testBackgroundTrace(nm::EffectRegistry& registry) {
    const QSize size(1920, 1080);
    nm::Graph graph = nm::compileGraph(QString::fromUtf8(kFibers), registry);

    nm::Backend synchronous;
    synchronous.setup(nullptr, kDataRoot, size);
    auto start = std::chrono::steady_clock::now();
    synchronous.render(graph, 0.25);
    const double syncFirst = secondsSince(start);
    const QImage syncSeed1 = synchronous.readSurface();
    nm::Graph syncGraph = graph;
    setSeed(synchronous, syncGraph, registry, 2);
    start = std::chrono::steady_clock::now();
    synchronous.render(syncGraph, 0.25);
    const double syncRetrace = secondsSince(start);
    const QImage syncSeed2 = synchronous.readSurface();
    std::printf("  synchronous 1920x1080 fibers: first render %.3f s, re-trace render %.3f s\n", syncFirst, syncRetrace);

    nm::Backend background;
    background.setup(nullptr, kDataRoot, size);
    background.setOverlayTraceMode(nm::OverlayTraceMode::Background);
    check(background.overlayTraceMode() == nm::OverlayTraceMode::Background
              && synchronous.overlayTraceMode() == nm::OverlayTraceMode::Synchronous,
          "the trace mode defaults to Synchronous and is selectable");
    start = std::chrono::steady_clock::now();
    background.render(graph, 0.25);
    const double firstRender = secondsSince(start);
    const bool pendingAfterFirst = background.overlayTracesPending();
    check(pendingAfterFirst && blackFrame(background.readSurface()),
          "a first trace runs after render() returns, and the node shows a transparent overlay meanwhile");
    background.waitForOverlayTraces();
    background.render(graph, 0.25);
    check(!background.overlayTracesPending() && background.readSurface() == syncSeed1,
          "the next render() after the trace uploads it: the frame equals the synchronous frame byte for byte");

    // The host's own frame time on this machine: frames with nothing to
    // trace, timed before the re-trace and again after it, so a drift of the
    // host's speed across the re-trace widens the steady spread.
    constexpr int kTimedFrames = 30;
    const auto timedRender = [&] {
        const auto t0 = std::chrono::steady_clock::now();
        background.render(graph, 0.25);
        return secondsSince(t0) * 1000.0;
    };
    double steady = 0.0; // seconds: the slowest of the first three steady frames
    std::vector<double> steadyMs;
    steadyMs.reserve(2 * kTimedFrames);
    double cpu = processCpuSeconds();
    start = std::chrono::steady_clock::now();
    for (int i = 0; i < kTimedFrames; ++i) {
        steadyMs.push_back(timedRender());
        if (i < 3) steady = std::max(steady, steadyMs.back() / 1000.0);
    }
    const double steadyCpus = (processCpuSeconds() - cpu) / secondsSince(start);
    const double maxSteadyMs = *std::max_element(steadyMs.begin(), steadyMs.end());

    setSeed(background, graph, registry, 2);
    std::vector<double> retraceMs;
    retraceMs.reserve(kTimedFrames);
    cpu = processCpuSeconds();
    start = std::chrono::steady_clock::now();
    retraceMs.push_back(timedRender());
    const double retraceRender = retraceMs.back() / 1000.0;
    const bool pendingAfterRetrace = background.overlayTracesPending();
    const QImage duringRetrace = background.readSurface();
    double switchMs = -1.0; // the render() that uploads the completed trace
    while (static_cast<int>(retraceMs.size()) < kTimedFrames && background.overlayTracesPending()) {
        const double sample = timedRender();
        if (!background.overlayTracesPending()) {
            switchMs = sample;
            break;
        }
        retraceMs.push_back(sample);
    }
    const double retraceCpus = (processCpuSeconds() - cpu) / secondsSince(start);
    // The overlay must change to the completed trace while a live host keeps
    // presenting, although the trace runs below the frame's threads: frames
    // continue at a 60 Hz pace (back to back on a host slower than that)
    // until the trace completes. The bound is ten synchronous trace times,
    // inside the reference's own: Chromium completes this 1920x1080 trace in
    // 33 s against the port's 2.8 s synchronous trace (async_overlay.h).
    std::vector<double> pacedMs;
    const auto deadline = start + std::chrono::duration<double>(10.0 * syncRetrace);
    auto nextFrame = std::chrono::steady_clock::now();
    while (switchMs < 0.0 && background.overlayTracesPending() && std::chrono::steady_clock::now() < deadline) {
        nextFrame += std::chrono::microseconds(16667);
        std::this_thread::sleep_until(nextFrame);
        const double sample = timedRender();
        if (!background.overlayTracesPending()) {
            switchMs = sample;
            break;
        }
        pacedMs.push_back(sample);
    }
    const double completionSeconds = secondsSince(start);
    const bool completedLive = switchMs >= 0.0;
    std::printf("  live re-trace completion %.3f s after %zu paced frames; synchronous re-trace %.3f s\n",
                completionSeconds, pacedMs.size(), syncRetrace);
    check(completedLive,
          "the overlay changes to the completed trace while frames continue at 60 Hz, within ten synchronous trace times");
    if (!completedLive) {
        background.waitForOverlayTraces();
        switchMs = timedRender();
    }
    check(!background.overlayTracesPending() && background.readSurface() == syncSeed2,
          "the completed re-trace equals the synchronous frame byte for byte");
    for (int i = 0; i < kTimedFrames; ++i) steadyMs.push_back(timedRender());

    std::printf("  background: first render %.3f s, steady frame %.3f s, render during re-trace %.3f s\n",
                firstRender, steady, retraceRender);
    printSamples("steady render", steadyMs);
    printSamples("re-trace render", retraceMs);
    if (!pacedMs.empty()) {
        printSamples("paced re-trace render", pacedMs);
        std::printf("  paced re-trace frames over 16.667 ms: %ld of %zu\n",
                    static_cast<long>(std::count_if(pacedMs.begin(), pacedMs.end(), [](double ms) { return ms > 1000.0 / 60.0; })),
                    pacedMs.size());
    }
    std::printf("  switch to the completed trace: render %.3f ms\n", switchMs);
    std::printf("  process CPU seconds per second: steady %.2f, during re-trace %.2f\n", steadyCpus, retraceCpus);

    // GAP-038's criterion: in the background mode render() returns within one
    // 60 Hz frame budget (16.667 ms) during a 1920x1080 re-trace. The frames
    // counted are the back-to-back render() calls while the trace is pending
    // and the render() that switches to the completed overlay; the steady
    // frames are back to back too. The paced frames are printed, not counted:
    // their number follows the trace's duration (hundreds of frames), so
    // their slowest frame measures the host's rare preemptions, which the 60
    // steady frames do not sample as often.
    //
    // Frames vary with no trace running. On Linux llvmpipe in CI a late frame
    // is followed by an early one (17.422 ms, then 11.368 ms, around a median
    // of 14.456 ms). The steady envelope is the slowest steady frame plus the
    // steady frames' spread: max + (max - min).
    //   - Envelope within the budget: the host renders inside the budget with
    //     room for its own variation, and the criterion applies as written.
    //     Every counted frame must be within 16.667 ms.
    //   - Envelope over the budget: the host's own steady frames can miss the
    //     budget, so the budget cannot show whether the trace stalls a frame.
    //     The test then asserts what the budget protects: the trace stalls no
    //     frame and takes no CPU from it. No counted frame may be slower than
    //     the envelope. The median frame during the re-trace may exceed the
    //     steady median by at most the steady interquartile range plus half
    //     of one CPU's share of the steady median, median / (2 N) for N
    //     hardware threads. A trace that takes one of the N CPUs a software
    //     rasterizer uses costs about median / (N - 1): with the trace at
    //     normal priority, seven Linux CI runs (N = 4) moved the median by
    //     3.432 to 7.135 ms, 1.306 to 1.445 times the steady median, and every
    //     run fails this bound. A trace that only shares caches and memory
    //     bandwidth costs less: on Apple's software renderer (one rasterizer
    //     thread, 8 CPUs) the median moved 0.332 ms, 2.25 percent.
    constexpr double kFrameBudgetMs = 1000.0 / 60.0;
    std::vector<double> counted = retraceMs;
    counted.push_back(switchMs);
    const double countedMax = *std::max_element(counted.begin(), counted.end());
    const double steadyMax = *std::max_element(steadyMs.begin(), steadyMs.end());
    const double steadyMin = *std::min_element(steadyMs.begin(), steadyMs.end());
    const double envelope = steadyMax + (steadyMax - steadyMin);
    const double steadyMedian = quantile(steadyMs, 0.5);
    const double steadyIqr = quantile(steadyMs, 0.75) - quantile(steadyMs, 0.25);
    const double retraceMedian = quantile(retraceMs, 0.5);
    const unsigned cpus = std::max(1u, std::thread::hardware_concurrency());
    const double medianBound = steadyMedian + steadyIqr + steadyMedian / (2.0 * cpus);
    std::printf("  steady envelope %.3f ms (max %.3f + spread %.3f); steady median %.3f ms, interquartile range %.3f ms; "
                "re-trace median %.3f ms (bound %.3f ms, %u hardware threads); counted max %.3f ms\n",
                envelope, steadyMax, steadyMax - steadyMin, steadyMedian, steadyIqr, retraceMedian, medianBound, cpus,
                countedMax);
    check(retraceMs.size() >= 10, "the re-trace stays pending for at least ten timed frames");
    if (envelope <= kFrameBudgetMs) {
        std::printf("  the steady envelope fits the 60 Hz budget: every counted frame must fit it\n");
        check(countedMax <= kFrameBudgetMs,
              "every render() during a 1920x1080 re-trace, and the switch to the completed overlay, "
              "returns within the 60 Hz frame budget (16.667 ms)");
    } else {
        std::printf("  the steady envelope exceeds the 60 Hz budget: the trace must add no stall beyond the steady variation\n");
        check(countedMax <= envelope,
              "no render() during a 1920x1080 re-trace, nor the switch to the completed overlay, "
              "is slower than the steady envelope");
        check(retraceMedian <= medianBound,
              "the median render() during a 1920x1080 re-trace stays within the steady interquartile range "
              "and half of one CPU's share of the steady median");
    }
    // The earlier allowances stay as well.
    check(*std::max_element(retraceMs.begin(), retraceMs.end()) <= std::max(16.667, 4.0 * maxSteadyMs + 0.05),
          "every sampled render during a 1920x1080 re-trace stays within four steady frames");
    check(pendingAfterRetrace && retraceRender <= 4.0 * steady + 0.05 && retraceRender < 0.5 * syncRetrace,
          "during a 1920x1080 re-trace render() takes a frame's time, not the trace's");
    check(duringRetrace == syncSeed1, "the previous overlay keeps rendering until the new trace completes");
    return syncRetrace;
}

// A newer change supersedes an older trace, which never uploads.
void testSupersession(nm::EffectRegistry& registry) {
    const QSize size(256, 256);
    nm::Graph graph = nm::compileGraph(QString::fromUtf8(kFibers), registry);
    nm::Backend reference;
    reference.setup(nullptr, kDataRoot, size);
    const auto referenceFrame = [&](int seed) {
        nm::Graph g = graph;
        setSeed(reference, g, registry, seed);
        reference.render(g, 0.25);
        return reference.readSurface();
    };
    const QImage seed3 = referenceFrame(3);
    const QImage seed4 = referenceFrame(4);

    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, size);
    backend.setOverlayTraceMode(nm::OverlayTraceMode::Background);
    setSeed(backend, graph, registry, 1);
    backend.render(graph, 0.25);
    bool onlyPlaceholder = blackFrame(backend.readSurface());
    backend.waitForOverlayTraces(); // seed 1 completed, not yet uploaded
    setSeed(backend, graph, registry, 2);
    backend.render(graph, 0.25);    // seed 1 is stale: discarded
    onlyPlaceholder = onlyPlaceholder && blackFrame(backend.readSurface());
    setSeed(backend, graph, registry, 3);
    backend.render(graph, 0.25);    // seed 2 is cancelled, running or not
    onlyPlaceholder = onlyPlaceholder && blackFrame(backend.readSurface());
    backend.waitForOverlayTraces();
    backend.render(graph, 0.25);
    check(onlyPlaceholder && backend.readSurface() == seed3,
          "two quick seed changes: only the newest trace (seed 3) ever appears");

    setSeed(backend, graph, registry, 4);
    backend.render(graph, 0.25);
    const bool kept = backend.readSurface() == seed3;
    backend.waitForOverlayTraces();
    backend.render(graph, 0.25);
    check(kept && backend.readSurface() == seed4, "a later change keeps seed 3 until seed 4 completes");

    setSeed(backend, graph, registry, 5);
    backend.render(graph, 0.25);
    setSeed(backend, graph, registry, 4);
    backend.render(graph, 0.25);
    check(!backend.overlayTracesPending() && backend.readSurface() == seed4,
          "returning to the uploaded seed cancels the trace in flight and keeps the frame");

    setSeed(backend, graph, registry, 3);
    backend.render(graph, 0.25);
    backend.setOverlayTraceMode(nm::OverlayTraceMode::Synchronous);
    backend.render(graph, 0.25);
    check(!backend.overlayTracesPending() && backend.readSurface() == seed3,
          "switching to Synchronous mid-trace renders the completed overlay on the next frame");
}

// Destroying or releasing a Backend while a trace runs cancels and joins it.
// `traceSeconds` is a synchronous render of the same trace on this machine:
// the bound scales with the runner and with sanitizer builds.
void testTeardownMidTrace(nm::EffectRegistry& registry, double traceSeconds) {
    const QSize size(1920, 1080);
    const nm::Graph graph = nm::compileGraph(QString::fromUtf8(kFibers), registry);

    auto owned = std::make_unique<nm::Backend>();
    owned->setup(nullptr, kDataRoot, size);
    owned->setOverlayTraceMode(nm::OverlayTraceMode::Background);
    owned->render(graph, 0.25);
    const bool running = owned->overlayTracesPending();
    auto start = std::chrono::steady_clock::now();
    owned.reset();
    const double destroy = secondsSince(start);

    nm::Backend released;
    released.setup(nullptr, kDataRoot, size);
    released.setOverlayTraceMode(nm::OverlayTraceMode::Background);
    released.render(graph, 0.25);
    start = std::chrono::steady_clock::now();
    released.releaseGl();
    const double release = secondsSince(start);
    std::printf("  teardown mid-trace: destructor %.3f s, releaseGl %.3f s\n", destroy, release);
    check(running && !released.overlayTracesPending() && destroy < 0.25 * traceSeconds && release < 0.25 * traceSeconds,
          "the destructor and releaseGl() cancel and join a running 1920x1080 trace in a fraction of its time");
}

// The host-texture precedence rule holds in the background mode.
void testBackgroundHostOverlay(nm::EffectRegistry& registry) {
    const QSize size(64, 64);
    const QString overlayId = QStringLiteral("node_1_overlayTex");
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, size);
    backend.setOverlayTraceMode(nm::OverlayTraceMode::Background);
    nm::Graph graph = nm::compileGraph(QString::fromUtf8(kFibers), registry);
    QImage host(size, QImage::Format_RGBA8888);
    host.fill(QColor(200, 100, 50, 255));
    const std::vector<std::uint8_t> hostPixels(host.constBits(), host.constBits() + host.sizeInBytes());
    const auto render = [&] {
        backend.render(graph, 0.25);
        return backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    };
    int inked = 0;

    backend.updateTextureFromSource(overlayId, host);
    const bool hostFirst = blendError(render(), size, hostPixels, inked) <= 1 && !backend.overlayTracesPending();
    check(hostFirst, "background mode: a host texture replaces the overlay and starts no trace");

    backend.removeExternalTexture(overlayId);
    const bool placeholder = blackFrame(render()) && backend.overlayTracesPending();
    backend.updateTextureFromSource(overlayId, host); // while the trace runs
    backend.waitForOverlayTraces();
    const bool hostKept = blendError(render(), size, hostPixels, inked) <= 1;
    backend.render(graph, 0.25);
    check(placeholder && hostKept && !backend.overlayTracesPending()
              && blendError(render(), size, hostPixels, inked) <= 1,
          "background mode: a host texture set during a trace is never overwritten by its result");

    backend.removeExternalTexture(overlayId);
    render();
    backend.waitForOverlayTraces();
    check(blendError(render(), size, fibersOverlay(size, 1), inked) <= 1 && inked > 500,
          "background mode: removing the host texture traces the overlay again");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    nm::EffectRegistry registry;
    registry.loadAll(kDataRoot);

    testSeededRng();
    testMath();
    testCanvasStrokes();
    testUploadConversion();
    testOverlays();
    testParams();
    testSync(registry);
    testRender(registry);
    testHostOverlay(registry);
    const double traceSeconds = testBackgroundTrace(registry);
    testSupersession(registry);
    testTeardownMidTrace(registry, traceSeconds);
    testBackgroundHostOverlay(registry);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
