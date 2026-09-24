// asyncInit overlays (runtime/async_overlay.h, stroke_canvas.h,
// worm_tracer.h): the reference generator and Math functions, the canvas
// model, the upload conversion, parameter semantics, regeneration, and the
// overlay as nm::Backend renders it. Plain executable, run from the repo
// root (WORKING_DIRECTORY in CMakeLists.txt).
//
// Expected values marked "Chromium" were captured from Chromium 153
// (headless, --use-angle=metal, Apple M4) running the reference modules;
// parity/check_async_overlay.mjs re-measures them against a live browser.

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/async_overlay.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/stroke_canvas.h"
#include "../noisemaker/runtime/worm_tracer.h"

#include <QCryptographicHash>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

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

// Chromium: four opaque white round-capped strokes on a 32x32 canvas; the
// premultiplied alpha of every inked pixel.
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
        {26, 8, 42}, {27, 8, 5}, {10, 9, 6}, {24, 9, 38}, {25, 9, 96}, {26, 9, 100}, {27, 9, 21},
        {9, 10, 108}, {10, 10, 246}, {11, 10, 39}, {22, 10, 33}, {23, 10, 92}, {24, 10, 104}, {25, 10, 45},
        {9, 11, 30}, {10, 11, 170}, {11, 11, 18}, {20, 11, 29}, {21, 11, 88}, {22, 11, 108}, {23, 11, 50},
        {19, 12, 27}, {20, 12, 113}, {21, 12, 54}, {23, 23, 31}, {24, 23, 47}, {5, 24, 77}, {23, 24, 237},
        {24, 24, 255}, {25, 24, 95}, {5, 25, 255}, {23, 25, 211}, {24, 25, 255}, {25, 25, 106}, {5, 26, 255},
        {24, 26, 48}, {5, 27, 255}, {5, 28, 255}, {5, 29, 102},
    };
    std::vector<int> want(32 * 32, 0);
    for (const Ink& ink : expected) want[static_cast<size_t>(ink.y * 32 + ink.x)] = ink.alpha;
    int mismatches = 0;
    for (size_t i = 0; i < want.size(); ++i) {
        if (canvas.premultiplied()[i * 4 + 3] != want[i]) ++mismatches;
    }
    check(mismatches == 0, "stroke coverage equals the Chromium canvas at every pixel (4 strokes, 39 inked pixels)");

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
    dot.strokeLine(8.3, 8.4, static_cast<double>(std::nextafter(8.3f, 100.0f)), 8.4);
    int sum = 0;
    for (size_t i = 3; i < dot.premultiplied().size(); i += 4) sum += dot.premultiplied()[i];
    check(sum == 1870,
          "a segment one float ulp long draws a round dot of width 3; width -1 and infinity are ignored (alpha sum 1870, Chromium)");

    canvas.clear();
    check(allZero(canvas.premultiplied()), "clear() makes the canvas transparent");
}

// Measured on Chromium's WebGL upload with UNPACK_PREMULTIPLY_ALPHA_WEBGL
// false: every c * 255 / a below is a midpoint, rounded up or down by the
// float quotient.
void testUploadConversion() {
    const std::vector<std::uint8_t> premultiplied = {
        7, 3, 1, 10,        // 178.5 -> 178, 76.5 -> 77, 25.5 -> 26
        1, 7, 21, 34,       // 7.5 -> 8, 52.5 -> 52, 157.5 -> 157
        1, 0, 2, 2,         // 127.5 -> 128, 0 -> 0, 255 -> 255
        5, 1, 6, 6,         // 212.5 -> 213, 42.5 -> 43, 255 -> 255
        100, 33, 3, 200,    // 127.5 -> 128, 42.075 -> 42, 3.825 -> 4
        200, 100, 3, 255,   // alpha 255 keeps the channels
        9, 9, 9, 0,         // alpha 0 -> 0
    };
    const std::vector<std::uint8_t> expected = {
        178, 77, 26, 10,
        8, 52, 157, 34,
        128, 0, 255, 2,
        213, 43, 255, 6,
        128, 42, 4, 200,
        200, 100, 3, 255,
        0, 0, 0, 0,
    };
    check(nm::unpremultiplyForUpload(premultiplied) == expected,
          "upload conversion rounds the float quotient like Chromium, including its midpoints");
}

// Chromium: SHA-256 of the uploaded overlay bytes (straight RGBA8, row 0 at
// the top) after the reference asyncInit completed at 256x256.
void testOverlays() {
    const QSize size(256, 256);
    const QJsonObject scratchParams{{QStringLiteral("density"), 0.3}, {QStringLiteral("seed"), 1}};
    const QJsonObject hairParams{{QStringLiteral("density"), 0.5}, {QStringLiteral("seed"), 1}};
    check(sha256(nm::generateAsyncOverlay(QStringLiteral("filter.scratches"), size, scratchParams))
              == QByteArrayLiteral("0f65532476d88b85354ebceeb568b7f92bcf27a62f3a31671cdf8e5cbabfc112"),
          "filter/scratches 256x256 overlay is byte-identical to Chromium");
    check(sha256(nm::generateAsyncOverlay(QStringLiteral("filter.strayHair"), size, hairParams))
              == QByteArrayLiteral("729fad21ed78abad5e9368bcd2f83c6859badf527be714715c8e97715ca4a8e7"),
          "filter/strayHair 256x256 overlay is byte-identical to Chromium");
    // Chromium's fibers overlay differs from this one in 25 channel values
    // at 14 pixels (GPU rounding at midpoints; check_async_overlay.mjs).
    // This hash guards the port's own output.
    const QJsonObject fiberParams{{QStringLiteral("density"), 1}, {QStringLiteral("seed"), 1}};
    const QByteArray fibers = sha256(nm::generateAsyncOverlay(QStringLiteral("filter.fibers"), size, fiberParams));
    check(fibers == QByteArrayLiteral("de08f4663feebfb5d38b0edc0d67706035a67bba16fd11e9ab9baa7018af2bfd"),
          "filter/fibers 256x256 overlay matches the recorded port output");

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

    // Largest difference between a frame and the blend of the port's overlay
    // at `frameSize`; `inked` counts the overlay's non-transparent pixels.
    const auto blendError = [](const QImage& frame, QSize frameSize, int& inked) {
        const std::vector<std::uint8_t> overlay = nm::generateAsyncOverlay(
            QStringLiteral("filter.fibers"), frameSize, QJsonObject{{QStringLiteral("seed"), 1}, {QStringLiteral("density"), 1}});
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
    };
    int inked = 0;
    const int worst = blendError(first, size, inked);
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
    const int resizedWorst = blendError(backend.readSurface().convertToFormat(QImage::Format_RGBA8888), resized, resizedInked);
    check(resizedInked > 200 && resizedWorst <= 1, "Backend::resize re-traces the overlay at the new size");

    nm::Graph blank = graph;
    for (nm::Pass& pass : blank.passes) {
        if (pass.effectKey == QStringLiteral("filter.fibers")) {
            pass.uniforms.insert(QStringLiteral("density"), QJsonObject{{QStringLiteral("type"), QStringLiteral("Oscillator")}});
        }
    }
    backend.render(blank, 0.25);
    const QImage cleared = backend.readSurface().convertToFormat(QImage::Format_RGBA8888);
    bool black = !cleared.isNull();
    for (int y = 0; y < cleared.height() && black; ++y) {
        const uchar* row = cleared.constScanLine(y);
        for (int x = 0; x < cleared.width(); ++x) {
            if (row[x * 4] || row[x * 4 + 1] || row[x * 4 + 2]) {
                black = false;
                break;
            }
        }
    }
    check(black, "an automated density uploads the cleared canvas, so the input passes through");
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

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
