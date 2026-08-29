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
