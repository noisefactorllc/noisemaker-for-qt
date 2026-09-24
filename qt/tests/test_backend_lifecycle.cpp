// nm::Backend lifecycle for embedding hosts: resize(), the render surface
// texture for GPU presentation, and teardown on owned and host-owned
// contexts. Plain executable, run from the repo root (WORKING_DIRECTORY in
// CMakeLists.txt).

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"

#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QSurfaceFormat>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

const QString kDataRoot = QStringLiteral("qt/noisemaker");

// A feedback program: each frame blends the previous frame, so its output
// depends on how many frames the surfaces have lived.
const char* const kFeedback =
    "search synth, filter, mixer\n"
    "noise(seed: 3).blendMode(tex: read(o1), mode: mix, mix: 40).write(o1)\n"
    "render(o1)";

int maxDifference(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return 256;
    int worst = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y);
            const QRgb q = b.pixel(x, y);
            worst = std::max({worst, std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                              std::abs(qBlue(p) - qBlue(q)), std::abs(qAlpha(p) - qAlpha(q))});
        }
    }
    return worst;
}

QImage renderFresh(const nm::Graph& graph, QSize size, int frames) {
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, size);
    for (int i = 0; i < frames; ++i) backend.render(graph, 0.25 + i / 600.0);
    return backend.readSurface();
}

void testResize(nm::EffectRegistry& registry) {
    const nm::Graph graph = nm::compileGraph(QString::fromUtf8(kFeedback), registry);
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(64, 64));
    for (int i = 0; i < 5; ++i) backend.render(graph, 0.25 + i / 600.0);
    check(backend.readSurface().size() == QSize(64, 64), "renders at the setup size");

    backend.resize(QSize(96, 40));
    for (int i = 0; i < 3; ++i) backend.render(graph, 0.25 + i / 600.0);
    const QImage resized = backend.readSurface();
    check(resized.size() == QSize(96, 40), "resize() changes the size of the next frame");
    const int diff = maxDifference(resized, renderFresh(graph, QSize(96, 40), 3));
    std::printf("  resized vs fresh backend after 3 frames: max diff %d\n", diff);
    check(diff == 0, "after resize() feedback starts over, like a fresh Backend at that size");

    backend.resize(QSize(64, 64));
    backend.render(graph, 0.25);
    check(maxDifference(backend.readSurface(), renderFresh(graph, QSize(64, 64), 1)) == 0,
          "resizing back renders the first frame again");

    bool threw = false;
    try {
        backend.resize(QSize(0, 10));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "resize() refuses an empty size");
    nm::Backend unset;
    threw = false;
    try {
        unset.resize(QSize(8, 8));
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "resize() before setup() throws");
}

void testSurfaceTexture(nm::EffectRegistry& registry) {
    const nm::Graph graph = nm::compileGraph(QStringLiteral(
        "search synth\nsolid(color: #ff8000).write(o0)\nrender(o0)"), registry);
    nm::Backend backend;
    backend.setup(nullptr, kDataRoot, QSize(32, 16));
    check(backend.renderSurfaceTexture().texture == 0, "no render surface texture before the first render");
    backend.render(graph, 0.0);
    const nm::Backend::SurfaceTexture surface = backend.renderSurfaceTexture();
    check(surface.texture != 0 && surface.size == QSize(32, 16), "render() exposes the presented texture and its size");

    // Read the texture through a framebuffer of our own, the way a host
    // presenting it would, and compare with readSurface().
    QOpenGLFunctions_4_1_Core f;
    f.initializeOpenGLFunctions();
    GLuint fbo = 0;
    f.glGenFramebuffers(1, &fbo);
    f.glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
    f.glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface.texture, 0);
    std::vector<unsigned char> pixels(32 * 16 * 4);
    f.glPixelStorei(GL_PACK_ALIGNMENT, 1);
    f.glReadPixels(0, 0, 32, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    f.glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    f.glDeleteFramebuffers(1, &fbo);
    std::printf("  texel (0,0) = %d %d %d %d\n", pixels[0], pixels[1], pixels[2], pixels[3]);
    check(pixels[0] == 255 && std::abs(pixels[1] - 128) <= 1 && pixels[2] == 0 && pixels[3] == 255
              && f.glGetError() == GL_NO_ERROR,
          "the texture holds the rendered frame");

    backend.resize(QSize(8, 8));
    check(backend.renderSurfaceTexture().texture == 0, "resize() drops the old texture until the next render");
}

void testInterleavedOwnedContexts(nm::EffectRegistry& registry) {
    const nm::Graph red = nm::compileGraph(QStringLiteral("search synth\nsolid(color: #ff0000).write(o0)\nrender(o0)"), registry);
    const nm::Graph blue = nm::compileGraph(QStringLiteral("search synth\nsolid(color: #0000ff).write(o0)\nrender(o0)"), registry);
    nm::Backend a;
    a.setup(nullptr, kDataRoot, QSize(8, 8));
    nm::Backend b;
    b.setup(nullptr, kDataRoot, QSize(8, 8));
    a.render(red, 0.0);
    b.render(blue, 0.0);
    a.render(red, 0.0);
    const QRgb fromA = a.readSurface().pixel(4, 4);
    const QRgb fromB = b.readSurface().pixel(4, 4);
    check(fromA == qRgba(255, 0, 0, 255) && fromB == qRgba(0, 0, 255, 255),
          "two Backends with their own contexts take turns on one thread");
}

void testOwnedTeardown(nm::EffectRegistry& registry) {
    const nm::Graph graph = nm::compileGraph(QString::fromUtf8(kFeedback), registry);
    bool ok = true;
    for (int cycle = 0; cycle < 20; ++cycle) {
        auto backend = std::make_unique<nm::Backend>();
        backend->setup(nullptr, kDataRoot, QSize(48, 48));
        backend->render(graph, 0.5);
        ok = ok && backend->readSurface().size() == QSize(48, 48);
        backend.reset();
    }
    check(ok, "20 create / render / destroy cycles on owned contexts");
}

void testHostContextTeardown(nm::EffectRegistry& registry) {
    QOpenGLContext context;
    context.setFormat(QSurfaceFormat::defaultFormat());
    QOffscreenSurface surface;
    if (!context.create()) {
        check(false, "host context creation");
        return;
    }
    surface.setFormat(context.format());
    surface.create();
    context.makeCurrent(&surface);
    QOpenGLFunctions_4_1_Core f;
    f.initializeOpenGLFunctions();

    const nm::Graph graph = nm::compileGraph(QStringLiteral(
        "search synth, filter\nsolid(color: #000000).text(text: \"x\").write(o0)\nrender(o0)"), registry);
    std::vector<GLuint> textures;
    bool ok = true;
    for (int cycle = 0; cycle < 10; ++cycle) {
        nm::Backend backend;
        backend.setup(&context, kDataRoot, QSize(32, 32));
        backend.render(graph, 0.0);
        textures.push_back(backend.renderSurfaceTexture().texture);
        backend.releaseGl();
        backend.releaseGl(); // idempotent
        ok = ok && f.glGetError() == GL_NO_ERROR;
    }
    bool freed = true;
    for (GLuint texture : textures) freed = freed && !f.glIsTexture(texture);
    check(ok, "10 setup / render / releaseGl cycles on one host context leave no GL error");
    check(freed, "releaseGl() deletes the Backend's textures in the host context");
    check(QOpenGLContext::currentContext() == &context, "the host context stays current");
    context.doneCurrent();
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc, argv);
    nm::EffectRegistry registry;
    registry.loadAll(kDataRoot);

    testResize(registry);
    testSurfaceTexture(registry);
    testInterleavedOwnedContexts(registry);
    testOwnedTeardown(registry);
    testHostContextTeardown(registry);

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
