#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/runtime/device_limits.h"
#include "../noisemaker/runtime/backend.h"
#include "../noisemaker/runtime/surface.h"

#include <QGuiApplication>
#include <QHash>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QSurfaceFormat>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        std::printf("FAIL: %s\n", description);
        ++g_failures;
    }
}

nm::TextureSpec textureSpec(const QString& format) {
    nm::TextureSpec spec;
    spec.width = QStringLiteral("screen");
    spec.height = QStringLiteral("screen");
    spec.format = format;
    return spec;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    {
        nm::Graph constrained;
        nm::Pass pass;
        pass.uniforms = {
            {QStringLiteral("volumeSize"), 128},
            {QStringLiteral("volumeSize_chain_0"), 128},
            {QStringLiteral("volumeSize_node_0"), 128},
            {QStringLiteral("unrelated"), 128},
        };
        constrained.passes.append(pass);
        nm::Graph exactFit = constrained;

        nm::detail::clampGraphVolumeSizes(constrained, 8192);
        nm::detail::clampGraphVolumeSizes(exactFit, 16384);

        const QJsonObject constrainedUniforms = constrained.passes.front().uniforms;
        const QJsonObject exactUniforms = exactFit.passes.front().uniforms;
        check(constrainedUniforms.value(QStringLiteral("volumeSize")).toInt() == 64,
              "unscoped volumeSize clamps to the largest fitting power of two");
        check(constrainedUniforms.value(QStringLiteral("volumeSize_chain_0")).toInt() == 64,
              "chain-scoped volumeSize clamps to the texture limit");
        check(constrainedUniforms.value(QStringLiteral("volumeSize_node_0")).toInt() == 64,
              "node-scoped volumeSize clamps to the texture limit");
        check(constrainedUniforms.value(QStringLiteral("unrelated")).toInt() == 128,
              "unrelated uniforms remain unchanged");
        check(exactUniforms.value(QStringLiteral("volumeSize")).toInt() == 128,
              "an exactly fitting volume atlas remains unchanged");
    }

    {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setVersion(4, 1);
        QOpenGLContext context;
        context.setFormat(format);
        const bool contextCreated = context.create();
        check(contextCreated, "external OpenGL context is available");

        QOffscreenSurface surface;
        surface.setFormat(context.format());
        surface.create();
        const bool current = contextCreated && surface.isValid() && context.makeCurrent(&surface);
        check(current, "external OpenGL context can be made current");
        if (current) {
            QOpenGLFunctions_4_1_Core gl;
            const bool functionsReady = gl.initializeOpenGLFunctions();
            check(functionsReady, "OpenGL 4.1 functions initialize for the live regression");
            if (functionsReady) {
                unsigned int framebuffers[2] = {0, 0};
                gl.glGenFramebuffers(2, framebuffers);
                gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
                gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);

                nm::Backend backend;
                backend.setup(&context, QStringLiteral("qt/noisemaker"), QSize(8, 8));
                int restoredRead = 0;
                int restoredDraw = 0;
                gl.glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &restoredRead);
                gl.glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &restoredDraw);
                check(restoredRead == static_cast<int>(framebuffers[0]),
                      "device probing restores the caller's read framebuffer binding");
                check(restoredDraw == static_cast<int>(framebuffers[1]),
                      "device probing restores the caller's draw framebuffer binding");

                gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
                const nm::Graph oversized = nm::compileGraph(QStringLiteral(
                    "search synth3d, render\n"
                    "noise3d(volumeSize: 1024).render3d().write(o0)\n"
                    "render(o0)\n"));
                auto callerVolumeSizesAre = [&oversized](int expected) {
                    bool found = false;
                    for (const nm::Pass& pass : oversized.passes) {
                        for (auto it = pass.uniforms.begin(); it != pass.uniforms.end(); ++it) {
                            if (it.key() == QStringLiteral("volumeSize")
                                || it.key().startsWith(QStringLiteral("volumeSize_chain_"))
                                || it.key().startsWith(QStringLiteral("volumeSize_node_"))) {
                                found = true;
                                if (it.value().toInt() != expected) return false;
                            }
                        }
                    }
                    return found;
                };
                bool repeatedRenderSucceeded = true;
                try {
                    backend.render(oversized, 0.25);
                    backend.render(oversized, 0.25);
                } catch (const std::exception&) {
                    repeatedRenderSucceeded = false;
                }
                check(repeatedRenderSucceeded,
                      "two constrained volume renders succeed through the real backend");
                check(callerVolumeSizesAre(1024),
                      "repeated constrained renders do not mutate the caller's graph");
                if (repeatedRenderSucceeded) {
                    check(backend.readSurface().size() == QSize(8, 8),
                          "the constrained render produces the requested output extent");
                }
                backend.releaseGl();

                nm::Graph aliasGraph;
                aliasGraph.textures.insert(
                    QStringLiteral("alias"), textureSpec(QStringLiteral("rgba32float")));
                nm::SurfaceCache cache(&gl);
                const nm::GpuSurface& alias = cache.get(
                    aliasGraph, QStringLiteral("alias"), QSize(2, 2));
                gl.glBindTexture(GL_TEXTURE_2D, alias.texture);
                int internalFormat = 0;
                gl.glGetTexLevelParameteriv(
                    GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &internalFormat);
                gl.glBindTexture(GL_TEXTURE_2D, 0);
                check(internalFormat == GL_RGBA32F,
                      "rgba32float allocates the same full-precision format as rgba32f");

                // Dynamic recreation when format changes on the same texId
                nm::Graph dynamicGraph;
                dynamicGraph.textures.insert(
                    QStringLiteral("dyn"), textureSpec(QStringLiteral("rgba32f")));
                const nm::GpuSurface& dynFirst = cache.get(
                    dynamicGraph, QStringLiteral("dyn"), QSize(2, 2));
                check(dynFirst.format == QStringLiteral("rgba32f"),
                      "initial texture format matches spec");

                dynamicGraph.textures[QStringLiteral("dyn")].format = QStringLiteral("rgba16f");
                const nm::GpuSurface& dynSecond = cache.get(
                    dynamicGraph, QStringLiteral("dyn"), QSize(2, 2));
                check(dynSecond.format == QStringLiteral("rgba16f"),
                      "recreated texture format reflects updated spec");
                gl.glBindTexture(GL_TEXTURE_2D, dynSecond.texture);
                int dynInternalFormat = 0;
                gl.glGetTexLevelParameteriv(
                    GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &dynInternalFormat);
                gl.glBindTexture(GL_TEXTURE_2D, 0);
                // The recreated texture must have the storage this driver
                // gives any GL_RGBA16F allocation. Apple's Software Renderer
                // (the GL on GPU-less macOS VMs, e.g. GitHub-hosted runners)
                // stores and reports GL_RGBA16F as GL_RGBA32F; Apple GPUs
                // report GL_RGBA16F. A fresh RGBA16F probe texture gives the
                // expected value, and it may never be less precise.
                GLuint probe = 0;
                gl.glGenTextures(1, &probe);
                gl.glBindTexture(GL_TEXTURE_2D, probe);
                gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 2, 2, 0, GL_RGBA, GL_HALF_FLOAT, nullptr);
                int probeInternalFormat = 0;
                gl.glGetTexLevelParameteriv(
                    GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &probeInternalFormat);
                gl.glBindTexture(GL_TEXTURE_2D, 0);
                gl.glDeleteTextures(1, &probe);
                std::printf("  GL_RENDERER %s: RGBA16F probe reports 0x%04x, recreated texture 0x%04x\n",
                            reinterpret_cast<const char*>(gl.glGetString(GL_RENDERER)),
                            probeInternalFormat, dynInternalFormat);
                check(probeInternalFormat == GL_RGBA16F || probeInternalFormat == GL_RGBA32F,
                      "the driver stores RGBA16F at half or full float precision");
                check(dynInternalFormat == probeInternalFormat,
                      "recreated texture has the driver's RGBA16F storage");

                const nm::GpuSurface& dynThird = cache.get(
                    dynamicGraph, QStringLiteral("dyn"), QSize(2, 2));
                check(dynThird.texture == dynSecond.texture,
                      "matching surface is reused without recreation");

                // Aliased / hazard surface recreation on format change
                const nm::GpuSurface& aliasFirst = cache.getAliased(
                    QStringLiteral("global_dyn#a"), dynamicGraph, QStringLiteral("dyn"), QSize(2, 2));
                check(aliasFirst.format == QStringLiteral("rgba16f"),
                      "aliased surface inherits format from spec");

                dynamicGraph.textures[QStringLiteral("dyn")].format = QStringLiteral("rgba8");
                const nm::GpuSurface& aliasSecond = cache.getAliased(
                    QStringLiteral("global_dyn#a"), dynamicGraph, QStringLiteral("dyn"), QSize(2, 2));
                check(aliasSecond.format == QStringLiteral("rgba8"),
                      "aliased surface recreates when spec format changes");
                gl.glBindTexture(GL_TEXTURE_2D, aliasSecond.texture);
                int aliasInternalFormat = 0;
                gl.glGetTexLevelParameteriv(
                    GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &aliasInternalFormat);
                gl.glBindTexture(GL_TEXTURE_2D, 0);
                check(aliasInternalFormat == GL_RGBA8,
                      "recreated aliased texture has GL_RGBA8 internal format");

                // GAP-007 structured diagnostics (reference dd4606ea):
                // an unknown spec format keeps the rgba8 fallback but
                // records a deduplicated ERR_UNKNOWN_FORMAT_FALLBACK.
                nm::Graph unknownFormatGraph;
                unknownFormatGraph.textures.insert(
                    QStringLiteral("fmt"), textureSpec(QStringLiteral("banana")));
                const nm::GpuSurface& fmtFirst = cache.get(
                    unknownFormatGraph, QStringLiteral("fmt"), QSize(2, 2));
                check(fmtFirst.format == QStringLiteral("banana"),
                      "unknown spec format is carried verbatim on the surface");
                gl.glBindTexture(GL_TEXTURE_2D, fmtFirst.texture);
                int fmtInternalFormat = 0;
                gl.glGetTexLevelParameteriv(
                    GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &fmtInternalFormat);
                gl.glBindTexture(GL_TEXTURE_2D, 0);
                check(fmtInternalFormat == GL_RGBA8,
                      "unknown format still allocates the rgba8 fallback");
                check(cache.diagnostics().records.size() == 1,
                      "one unknown format fallback is recorded");
                check(cache.diagnostics().records.first().code == QStringLiteral("ERR_UNKNOWN_FORMAT_FALLBACK"),
                      "the format fallback record carries the GAP-007 code");
                check(cache.diagnostics().records.first().backend == QStringLiteral("qt-gl"),
                      "the format fallback record names the native backend");
                check(cache.diagnostics().records.first().stage == QStringLiteral("texture-create"),
                      "the format fallback record uses the reference stage");
                check(cache.diagnostics().records.first().format == QStringLiteral("banana"),
                      "the format fallback record carries the unknown format");
                check(cache.diagnostics().records.first().fallback == QStringLiteral("rgba8"),
                      "the format fallback record carries the fallback");
                (void)cache.get(unknownFormatGraph, QStringLiteral("fmt"), QSize(2, 2));
                check(cache.diagnostics().records.size() == 1,
                      "the same format fallback is deduplicated");

                // Unknown dimension forms keep the screen-size fallback but
                // record a deduplicated ERR_DIMENSION_FALLBACK.
                nm::Graph unknownDimGraph;
                nm::TextureSpec unknownDimSpec = textureSpec(QStringLiteral("rgba16f"));
                unknownDimSpec.width = QJsonValue(QStringLiteral("zoom"));
                unknownDimGraph.textures.insert(QStringLiteral("dim"), unknownDimSpec);
                const nm::GpuSurface& dimFirst = cache.get(
                    unknownDimGraph, QStringLiteral("dim"), QSize(4, 2));
                check(dimFirst.width == 4 && dimFirst.height == 2,
                      "unknown dimension form still falls back to screen size");
                check(cache.diagnostics().records.size() == 2,
                      "the dimension fallback is recorded after the format one");
                check(cache.diagnostics().records.last().code == QStringLiteral("ERR_DIMENSION_FALLBACK"),
                      "the dimension fallback record carries the GAP-007 code");
                check(cache.diagnostics().records.last().stage == QStringLiteral("dimension"),
                      "the dimension fallback record uses the reference stage");
                check(cache.diagnostics().records.last().spec == QStringLiteral("zoom"),
                      "the dimension fallback record carries the unknown spec");
                check(cache.diagnostics().records.last().fallback == QStringLiteral("screen"),
                      "the dimension fallback record carries the fallback");
                (void)cache.get(unknownDimGraph, QStringLiteral("dim"), QSize(4, 2));
                check(cache.diagnostics().records.size() == 2,
                      "the same dimension fallback is deduplicated");

                // Known formats and recognized dimension keywords add
                // nothing (upstream a0e9bbff: 'input'/'resolution' are
                // recognized forms, resolved like screen/auto).
                nm::Graph knownGraph;
                nm::TextureSpec knownSpec = textureSpec(QStringLiteral("rgba32f"));
                knownSpec.width = QJsonValue(QStringLiteral("input"));
                knownGraph.textures.insert(QStringLiteral("known"), knownSpec);
                const nm::GpuSurface& known = cache.get(
                    knownGraph, QStringLiteral("known"), QSize(3, 3));
                check(known.width == 3 && known.height == 3,
                      "the 'input' dimension keyword resolves to the screen dimension");
                check(cache.diagnostics().records.size() == 2,
                      "known formats and recognized dimension keywords add no diagnostic");

                cache.clearDiagnostics();
                check(cache.diagnostics().records.isEmpty(),
                      "clearDiagnostics empties the collector");

                cache.releaseAll();

                gl.glDeleteFramebuffers(2, framebuffers);
            }
            context.doneCurrent();
        }
    }

    {
        nm::Graph constrained;
        constrained.textures.insert(QStringLiteral("xyz"), textureSpec(QStringLiteral("rgba32f")));
        constrained.textures.insert(QStringLiteral("vel"), textureSpec(QStringLiteral("rgba32float")));
        constrained.textures.insert(QStringLiteral("rgba"), textureSpec(QStringLiteral("rgba8")));
        nm::Pass pass;
        pass.id = QStringLiteral("pointsEmit:init");
        pass.outputs = {
            {QStringLiteral("outRGBA"), QStringLiteral("rgba")},
            {QStringLiteral("outVel"), QStringLiteral("vel")},
            {QStringLiteral("outXYZ"), QStringLiteral("xyz")},
        };
        constrained.passes.append(pass);
        nm::Graph desktop = constrained;
        const QHash<QString, int> locations = {
            {QStringLiteral("outXYZ"), 0},
            {QStringLiteral("outVel"), 1},
            {QStringLiteral("outRGBA"), 2},
        };

        nm::detail::applyMrtFormatBudget(constrained, constrained.passes.front(), locations, 32);
        nm::detail::applyMrtFormatBudget(desktop, desktop.passes.front(), locations, 64);

        check(constrained.textures.value(QStringLiteral("xyz")).format == QStringLiteral("rgba32f"),
              "MRT fitting preserves the first linked attachment at full precision");
        check(constrained.textures.value(QStringLiteral("vel")).format == QStringLiteral("rgba16f"),
              "MRT fitting demotes the trailing float attachment that exceeds the budget");
        check(constrained.textures.value(QStringLiteral("rgba")).format == QStringLiteral("rgba8"),
              "MRT fitting preserves the byte attachment");
        check(desktop.textures.value(QStringLiteral("vel")).format == QStringLiteral("rgba32float"),
              "an MRT within the desktop budget remains unchanged");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_device_limits)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_device_limits)\n", g_failures);
    return 1;
}
