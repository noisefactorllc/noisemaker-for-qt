// Host-input runtime APIs on nm::Backend: external textures
// (updateTextureFromSource / setExternalTexture), live parameter updates
// (applyStepParameterValues / setUniform), and engine time globals
// (deltaTime / frame / syncTime / normalizedLoopTime). Plain executable,
// run from the repo root (WORKING_DIRECTORY in CMakeLists.txt).

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/output_sink.h"
#include "../noisemaker/runtime/parameters.h"

#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace nm {
struct BackendTestAccess {
    static QJsonObject engineUniforms(const Backend& backend) { return backend.engineUniforms(); }
};
} // namespace nm

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

const QString kDataRoot = QStringLiteral("qt/noisemaker");

nm::Graph compile(nm::EffectRegistry& registry, const char* source) {
    return nm::compileGraph(QString::fromUtf8(source), registry);
}

struct Rgba {
    int r, g, b, a;
};

Rgba pixel(const QImage& image, int x, int y) {
    const QRgb c = image.pixel(x, y);
    return {qRed(c), qGreen(c), qBlue(c), qAlpha(c)};
}

bool near(const Rgba& p, int r, int g, int b, int tolerance = 2) {
    return std::abs(p.r - r) <= tolerance && std::abs(p.g - g) <= tolerance && std::abs(p.b - b) <= tolerance;
}

// 64x64 RGBA8888 image: rows 0..31 (top) red, rows 32..63 (bottom) blue.
QImage topRedBottomBlue() {
    QImage image(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            image.setPixel(x, y, y < 32 ? qRgba(255, 0, 0, 255) : qRgba(0, 0, 255, 255));
        }
    }
    return image;
}

void testExternalTextureIds(nm::EffectRegistry& registry) {
    const nm::Graph graph = compile(registry,
        "search synth, filter\nmedia().write(o0)\nnoise().text().write(o1)\nrender(o0)");
    const QStringList ids = nm::Backend::externalTextureIds(graph);
    check(ids == QStringList({QStringLiteral("imageTex_step_0"), QStringLiteral("textTex_step_3")}),
          "externalTextureIds lists the per-step media and text ids in pass order");
    check(nm::Backend::externalTextureId(QStringLiteral("imageTex"), 0) == QStringLiteral("imageTex_step_0"),
          "externalTextureId builds <externalTexture>_step_<N>");
    check(graph.passes.first().stepIndex == 0, "graph passes carry stepIndex from the compiled graph");
}

void testMediaUpload(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(64, 64));
    nm::Graph graph = compile(registry, "search synth\nmedia().write(o0)\nrender(o0)");
    const QString texId = QStringLiteral("imageTex_step_0");

    backend.render(graph, 0.0);
    const QImage unsupplied = backend.readSurface();
    const Rgba unsuppliedTop = pixel(unsupplied, 32, 8);
    check(!near(unsuppliedTop, 255, 0, 0, 40), "media without a host texture does not show red");

    // Reference demo host: media uploads use flipY=false, then imageSize.
    const QSize uploaded = backend.updateTextureFromSource(texId, topRedBottomBlue(), nm::ExternalTextureOptions{false});
    check(uploaded == QSize(64, 64), "updateTextureFromSource returns the uploaded size");
    const int writes = backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_0"), QJsonObject{{QStringLiteral("imageSize"), QJsonArray{64, 64}}}}});
    check(writes == 1, "applyStepParameterValues writes the media imageSize uniform");
    backend.render(graph, 0.0);
    const QImage upright = backend.readSurface();
    const Rgba top = pixel(upright, 32, 8);
    const Rgba bottom = pixel(upright, 32, 56);
    std::printf("  flipY=false top=(%d,%d,%d,%d) bottom=(%d,%d,%d,%d)\n", top.r, top.g, top.b, top.a,
                bottom.r, bottom.g, bottom.b, bottom.a);
    check(near(top, 255, 0, 0) && near(bottom, 0, 0, 255),
          "media with flipY=false renders the source upright (top red, bottom blue)");

    backend.updateTextureFromSource(texId, topRedBottomBlue(), nm::ExternalTextureOptions{true});
    backend.render(graph, 0.0);
    const QImage flipped = backend.readSurface();
    check(near(pixel(flipped, 32, 8), 0, 0, 255) && near(pixel(flipped, 32, 56), 255, 0, 0),
          "flipY=true reverses the source rows");

    // Raw-pointer upload with a padded stride (row = 64 px + 16 px padding).
    const int stride = (64 + 16) * 4;
    std::vector<unsigned char> raw(static_cast<size_t>(stride) * 64, 0x7f);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            unsigned char* p = raw.data() + y * stride + x * 4;
            p[0] = 0; p[1] = 255; p[2] = 0; p[3] = 255;
        }
    }
    check(backend.updateTextureFromSource(texId, raw.data(), 64, 64, stride, nm::ExternalTextureOptions{false})
              == QSize(64, 64),
          "raw RGBA8 upload with padded stride returns its size");
    backend.render(graph, 0.0);
    check(near(pixel(backend.readSurface(), 32, 32), 0, 255, 0),
          "raw RGBA8 upload ignores row padding and renders green");

    bool threw = false;
    try {
        backend.updateTextureFromSource(texId, raw.data(), 64, 64, 64 * 4 - 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a stride shorter than width*4 is rejected");
    check(backend.updateTextureFromSource(texId, QImage()) == QSize(0, 0), "an empty source uploads nothing");

    // Zero-copy: a host-owned GL texture in the Backend's current context.
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()->functions();
    GLuint hostTexture = 0;
    gl->glGenTextures(1, &hostTexture);
    gl->glBindTexture(GL_TEXTURE_2D, hostTexture);
    std::vector<unsigned char> yellow(64 * 64 * 4);
    for (size_t i = 0; i < yellow.size(); i += 4) {
        yellow[i] = 255; yellow[i + 1] = 255; yellow[i + 2] = 0; yellow[i + 3] = 255;
    }
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, yellow.data());
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    backend.setExternalTexture(texId, hostTexture, QSize(64, 64));
    backend.render(graph, 0.0);
    check(near(pixel(backend.readSurface(), 32, 32), 255, 255, 0), "setExternalTexture samples the host GL texture");

    backend.removeExternalTexture(texId);
    backend.render(graph, 0.0);
    const QImage removed = backend.readSurface();
    check(pixel(removed, 32, 8).r == unsuppliedTop.r && pixel(removed, 32, 8).g == unsuppliedTop.g
              && pixel(removed, 32, 8).b == unsuppliedTop.b,
          "removeExternalTexture returns the pass to the default texture");
    check(gl->glIsTexture(hostTexture) == GL_TRUE, "the Backend never deletes a host-owned texture");
    gl->glDeleteTextures(1, &hostTexture);

    backend.updateTextureFromSource(texId, topRedBottomBlue());
    backend.releaseGl();
    check(true, "releaseGl frees owned external textures without error");
}

void testTextUpload(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(64, 64));
    const nm::Graph graph = compile(registry, "search synth, filter\nsolid(color: #000000).text().write(o0)\nrender(o0)");
    const QStringList ids = nm::Backend::externalTextureIds(graph);
    check(ids == QStringList({QStringLiteral("textTex_step_1")}), "filter/text reads textTex_step_1");
    backend.render(graph, 0.0);
    const Rgba before = pixel(backend.readSurface(), 32, 32);
    QImage white(64, 64, QImage::Format_RGBA8888);
    white.fill(qRgba(255, 255, 255, 255));
    backend.updateTextureFromSource(ids.first(), white, nm::ExternalTextureOptions{true});
    backend.render(graph, 0.0);
    const Rgba after = pixel(backend.readSurface(), 32, 32);
    std::printf("  text before=(%d,%d,%d) after=(%d,%d,%d)\n", before.r, before.g, before.b, after.r, after.g, after.b);
    check(near(before, 0, 0, 0) && after.r > 200 && after.g > 200 && after.b > 200,
          "a supplied text texture composites over the input");
}

void testLiveParameters(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(16, 16));
    nm::Graph graph = compile(registry, "search synth\nsolid(color: #ff0000).write(o0)\nrender(o0)");
    backend.render(graph, 0.0);
    check(near(pixel(backend.readSurface(), 8, 8), 255, 0, 0), "solid renders its compiled color");

    int writes = backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_0"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#00ff00")}}}});
    backend.render(graph, 0.0);
    check(writes == 1 && near(pixel(backend.readSurface(), 8, 8), 0, 255, 0),
          "applyStepParameterValues converts a hex color and updates the pass without recompiling");

    writes = backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_9"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#0000ff")}}},
                    {QStringLiteral("step_0"), QJsonObject{{QStringLiteral("nope"), 1},
                                                          {QStringLiteral("_skip"), true}}}});
    check(writes == 0, "unknown steps, unknown params and _skip write nothing");

    const QJsonObject osc{{QStringLiteral("type"), QStringLiteral("Oscillator")}};
    writes = backend.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_0"), QJsonObject{{QStringLiteral("color"), osc}}}});
    check(writes == 0, "automation values are not applied as parameter values");

    backend.setUniform(graph, QStringLiteral("color"), QJsonArray{0.0, 0.0, 1.0});
    backend.render(graph, 0.0);
    check(near(pixel(backend.readSurface(), 8, 8), 0, 0, 255), "setUniform fans out to every pass carrying the uniform");

    nm::Graph automated = graph;
    automated.passes.first().uniforms.insert(QStringLiteral("color"), osc);
    backend.setUniform(automated, QStringLiteral("color"), QJsonArray{1.0, 1.0, 1.0});
    check(automated.passes.first().uniforms.value(QStringLiteral("color")).isObject(),
          "setUniform does not overwrite an automation descriptor");

    backend.setUniform(graph, QStringLiteral("time"), 0.9);
    backend.setUniform(graph, QStringLiteral("hostOnly"), 7);
    backend.render(graph, 0.25);
    const QJsonObject engine = nm::BackendTestAccess::engineUniforms(backend);
    check(engine.value(QStringLiteral("hostOnly")).toInt() == 7, "setUniform stores host globals");
    check(engine.value(QStringLiteral("time")).toDouble() == 0.25, "engine time overrides a host global of the same name");

    // Palette expansion (palette-expansion.js expandPalette).
    const QJsonObject palette = nm::expandPalette(1);
    check(palette.value(QStringLiteral("paletteMode")).toInt() == 3
              && palette.value(QStringLiteral("paletteAmp")).toArray() == QJsonArray({0.76, 0.88, 0.37}),
          "expandPalette(1) is seventiesShirt");
    check(nm::expandPalette(55).value(QStringLiteral("paletteOffset")).toArray() == QJsonArray({0.56, 0.35, 0.14}),
          "expandPalette(55) is vintagePhoto");
    check(nm::expandPalette(0).isEmpty() && nm::expandPalette(56).isEmpty(), "expandPalette rejects out-of-range indices");

    // convertParameterForUniform coercions.
    const QJsonObject intSpec{{QStringLiteral("type"), QStringLiteral("int")}};
    check(nm::convertParameterForUniform(2.5, intSpec, nullptr).toDouble() == 3.0
              && nm::convertParameterForUniform(true, intSpec, nullptr).toInt() == 1
              && nm::convertParameterForUniform(QStringLiteral("12px"), intSpec, nullptr).toInt() == 12,
          "int params round numbers, map booleans and parseInt strings");
    const QJsonObject boolSpec{{QStringLiteral("type"), QStringLiteral("boolean")}};
    check(nm::convertParameterForUniform(0, boolSpec, nullptr) == QJsonValue(false)
              && nm::convertParameterForUniform(QStringLiteral("x"), boolSpec, nullptr) == QJsonValue(true),
          "boolean params use JS truthiness");
    const QJsonObject enumSpec{{QStringLiteral("type"), QStringLiteral("int")},
                               {QStringLiteral("enum"), QStringLiteral("filter.feedback.blendMode")}};
    const QJsonValue mode = nm::convertParameterForUniform(QStringLiteral("multiply"), enumSpec, &registry.enums());
    check(mode.toInt() == 11, "enum names resolve through the registry enums");
}

void testFeedbackPersistsAcrossUpdates(nm::EffectRegistry& registry) {
    const char* source = "search synth, filter\nsolid(color: #ff0000).feedback(mix: 50).write(o0)\nrender(o0)";

    nm::Backend persistent;
    persistent.setup(nullptr, kDataRoot, QSize(16, 16));
    nm::Graph graph = compile(registry, source);
    for (int i = 0; i < 4; ++i) persistent.render(graph, 0.0);

    // Swap to a newly compiled graph with the same structure (blue source).
    nm::Graph blue = compile(registry,
        "search synth, filter\nsolid(color: #0000ff).feedback(mix: 50).write(o0)\nrender(o0)");
    persistent.render(blue, 0.0);
    const Rgba swapped = pixel(persistent.readSurface(), 8, 8);

    nm::Backend fresh;
    fresh.setup(nullptr, kDataRoot, QSize(16, 16));
    fresh.render(blue, 0.0);
    const Rgba baseline = pixel(fresh.readSurface(), 8, 8);
    std::printf("  swapped=(%d,%d,%d) fresh=(%d,%d,%d)\n", swapped.r, swapped.g, swapped.b,
                baseline.r, baseline.g, baseline.b);
    check(swapped.r > baseline.r + 32, "a recompiled graph with the same structure keeps the feedback surface");

    // Live parameter update on the same graph also keeps it.
    nm::Backend live;
    live.setup(nullptr, kDataRoot, QSize(16, 16));
    for (int i = 0; i < 4; ++i) live.render(graph, 0.0);
    live.applyStepParameterValues(graph, registry,
        QJsonObject{{QStringLiteral("step_0"), QJsonObject{{QStringLiteral("color"), QStringLiteral("#0000ff")}}}});
    live.render(graph, 0.0);
    const Rgba updated = pixel(live.readSurface(), 8, 8);
    check(updated.r == swapped.r && updated.g == swapped.g && updated.b == swapped.b,
          "a live parameter update matches the recompiled-graph swap exactly");
}

void testEngineTime(nm::EffectRegistry& registry) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(8, 8));
    const nm::Graph graph = compile(registry, "search synth\nsolid().write(o0)\nrender(o0)");

    // Sinks receive the frame inside render(), after every pass ran and
    // before the frame counter advances, so this capture sees the engine
    // uniforms the passes of that frame were bound with.
    struct EngineCapture : public nm::OutputSink {
        const nm::Backend* backend = nullptr;
        QJsonObject last;
        void configure(const nm::OutputDescriptor&) override {}
        bool submit(const nm::GpuSurface&, double) override {
            last = nm::BackendTestAccess::engineUniforms(*backend);
            return true;
        }
        void close(bool) override {}
    };
    auto capture = std::make_shared<EngineCapture>();
    capture->backend = &backend;
    backend.addSink(capture);
    auto engineAfter = [&](double t) {
        backend.render(graph, t);
        return capture->last;
    };
    QJsonObject g = engineAfter(0.25);
    check(g.value(QStringLiteral("deltaTime")).toDouble() == 0.0 && g.value(QStringLiteral("frame")).toInt() == 0,
          "first frame: deltaTime 0, frame 0");
    check(backend.frameIndex() == 1 && backend.lastTime() == 0.25, "frameIndex and lastTime advance after render");
    g = engineAfter(0.5);
    check(g.value(QStringLiteral("deltaTime")).toDouble() == 0.25 && g.value(QStringLiteral("frame")).toInt() == 1,
          "deltaTime is the normalized time step; frame counts prior renders");
    g = engineAfter(0.1);
    check(g.value(QStringLiteral("deltaTime")).toDouble() == 1.0 / 60.0 / 10.0,
          "a backwards (wrapped) time step uses 1/600");
    backend.syncTime(0.3);
    g = engineAfter(0.3);
    check(g.value(QStringLiteral("deltaTime")).toDouble() == 0.0, "syncTime makes a paused re-render have deltaTime 0");
    check(std::fabs(nm::Backend::normalizedLoopTime(25.0) - 0.5) < 1e-12
              && std::fabs(nm::Backend::normalizedLoopTime(3.0, 4.0) - 0.75) < 1e-12,
          "normalizedLoopTime wraps elapsed seconds by the loop duration (default 10 s)");
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    nm::EffectRegistry registry;
    registry.loadAll(kDataRoot);

    testExternalTextureIds(registry);
    testMediaUpload(registry);
    testTextUpload(registry);
    testLiveParameters(registry);
    testFeedbackPersistsAcrossUpdates(registry);
    testEngineTime(registry);

    std::printf("%d failure(s)\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
