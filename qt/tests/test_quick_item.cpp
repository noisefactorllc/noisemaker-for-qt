// nm::NoisemakerItem (qt/quick/noisemaker_item.h) in a QML scene rendered
// offscreen through QQuickRenderControl on an OpenGL 4.1 core context:
// program changes, orientation, errors and recovery, resize, paused time,
// and teardown. Plain executable, run from the repo root
// (WORKING_DIRECTORY in CMakeLists.txt).

#include "../quick/noisemaker_item.h"

#include "../noisemaker/compiler/dsl_compiler.h"
#include "../noisemaker/compiler/effect_registry.h"
#include "../noisemaker/runtime/backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_1_Core>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSurfaceFormat>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition) ++g_failures;
}

const char* const kRed = "search synth\nsolid(color: #ff0000).write(o0)\nrender(o0)";
const char* const kBlue = "search synth\nsolid(color: #0000ff).write(o0)\nrender(o0)";

// A QML scene rendered into a texture of our own.
class Scene {
public:
    bool create(QSize size) {
        m_context.setFormat(QSurfaceFormat::defaultFormat());
        if (!m_context.create()) return false;
        m_surface.setFormat(m_context.format());
        m_surface.create();
        if (!m_context.makeCurrent(&m_surface)) return false;
        m_gl.initializeOpenGLFunctions();
        m_window = std::make_unique<QQuickWindow>(&m_control);
        m_window->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&m_context));
        m_window->setColor(Qt::transparent); // so the item's own pixels are what the scene holds
        if (!m_control.initialize()) return false;

        QQmlComponent component(&m_engine);
        component.setData("import Noisemaker 1.0\nNoisemakerItem {}\n", QUrl());
        m_item.reset(qobject_cast<nm::NoisemakerItem*>(component.create()));
        if (!m_item) {
            std::printf("  %s\n", qPrintable(component.errorString()));
            return false;
        }
        m_item->setParentItem(m_window->contentItem());
        m_item->setDataRoot(QDir(QStringLiteral("qt/noisemaker")).absolutePath());
        resize(size);
        return true;
    }

    ~Scene() {
        m_context.makeCurrent(&m_surface);
        m_item.reset();
        m_window.reset();
        if (m_texture) m_gl.glDeleteTextures(1, &m_texture);
    }

    void resize(QSize size) {
        m_context.makeCurrent(&m_surface);
        if (m_texture) m_gl.glDeleteTextures(1, &m_texture);
        m_gl.glGenTextures(1, &m_texture);
        m_gl.glBindTexture(GL_TEXTURE_2D, m_texture);
        m_gl.glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.width(), size.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        m_gl.glBindTexture(GL_TEXTURE_2D, 0);
        m_window->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(m_texture, size));
        m_window->setGeometry(0, 0, size.width(), size.height());
        m_window->contentItem()->setSize(size);
        m_item->setSize(size);
        m_size = size;
    }

    // Renders one frame, delivers the item's queued frame report, and reads
    // the scene back top-down.
    QImage frame() {
        m_context.makeCurrent(&m_surface);
        m_control.polishItems();
        m_control.beginFrame();
        m_control.sync();
        m_control.render();
        m_control.endFrame();
        GLuint fbo = 0;
        m_gl.glGenFramebuffers(1, &fbo);
        m_gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        m_gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_texture, 0);
        QImage image(m_size, QImage::Format_RGBA8888);
        m_gl.glPixelStorei(GL_PACK_ALIGNMENT, 1);
        for (int y = 0; y < m_size.height(); ++y) {
            m_gl.glReadPixels(0, m_size.height() - 1 - y, m_size.width(), 1, GL_RGBA, GL_UNSIGNED_BYTE, image.scanLine(y));
        }
        m_gl.glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_gl.glDeleteFramebuffers(1, &fbo);
        m_lastGlError = m_gl.glGetError();
        QCoreApplication::processEvents();
        return image;
    }

    nm::NoisemakerItem* item() { return m_item.get(); }
    unsigned int lastGlError() const { return m_lastGlError; }

private:
    QOpenGLContext m_context;
    QOffscreenSurface m_surface;
    QOpenGLFunctions_4_1_Core m_gl;
    QQuickRenderControl m_control;
    std::unique_ptr<QQuickWindow> m_window;
    QQmlEngine m_engine;
    std::unique_ptr<nm::NoisemakerItem> m_item;
    GLuint m_texture = 0;
    QSize m_size;
    unsigned int m_lastGlError = 0;
};

bool uniform(const QImage& image, QRgb color) {
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (image.pixel(x, y) != color) return false;
        }
    }
    return true;
}

void testItem() {
    Scene scene;
    if (!scene.create(QSize(64, 64))) {
        check(false, "the offscreen QML scene initializes");
        return;
    }
    nm::NoisemakerItem* item = scene.item();

    item->setProgram(QString::fromUtf8(kRed));
    QImage image = scene.frame();
    check(uniform(image, qRgba(255, 0, 0, 255)) && item->errorString().isEmpty() && item->frameCount() == 1,
          "the item renders its program into the scene");

    item->setProgram(QString::fromUtf8(kBlue));
    image = scene.frame();
    check(uniform(image, qRgba(0, 0, 255, 255)) && item->frameCount() == 1, "a new program renders on the next frame");

    item->setProgram(QStringLiteral(
        "search synth, filter\nsolid(color: #000000).text(text: \"TOP\", size: 0.25, posY: 0.15).write(o0)\nrender(o0)"));
    image = scene.frame();
    int top = 0, bottom = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qRed(image.pixel(x, y)) > 200) (y < 32 ? top : bottom)++;
        }
    }
    std::printf("  text pixels top=%d bottom=%d\n", top, bottom);
    check(top > 40 && bottom == 0, "text at posY 0.15 appears at the top of the scene (upright, text drawn)");

    item->setProgram(QStringLiteral("search synth\nnoize().write(o0)\nrender(o0)"));
    image = scene.frame();
    std::printf("  errorString: %s\n", qPrintable(item->errorString()));
    check(!item->errorString().isEmpty() && uniform(image, qRgba(0, 0, 0, 0)) && item->frameCount() == 0,
          "a program that does not compile reports errorString and draws transparent pixels");
    item->setProgram(QString::fromUtf8(kRed));
    image = scene.frame();
    check(item->errorString().isEmpty() && uniform(image, qRgba(255, 0, 0, 255)),
          "fixing the program clears errorString and renders again");

    scene.resize(QSize(96, 40));
    image = scene.frame();
    check(image.size() == QSize(96, 40) && uniform(image, qRgba(255, 0, 0, 255)) && scene.lastGlError() == 0,
          "a resized item renders at its new size");

    const QString goodRoot = item->dataRoot();
    item->setDataRoot(QStringLiteral("/nonexistent/noisemaker-data"));
    scene.frame();
    std::printf("  errorString: %s\n", qPrintable(item->errorString()));
    check(item->errorString().contains(QStringLiteral("data root")), "a data root without effects/ and shaders/ is named in errorString");
    item->setDataRoot(goodRoot);
    image = scene.frame();
    check(item->errorString().isEmpty() && uniform(image, qRgba(255, 0, 0, 255)), "restoring the data root recovers");

    // meshLoader starts with the built-in sphere: the lit mesh covers the
    // middle of the frame, over meshRender's background.
    item->setProgram(QStringLiteral("search render\nmeshLoader().meshRender().write(o0)\nrender(o0)"));
    image = scene.frame();
    const QRgb middle = image.pixel(48, 20);
    const QRgb corner = image.pixel(1, 1);
    std::printf("  mesh middle=(%d,%d,%d) corner=(%d,%d,%d)\n", qRed(middle), qGreen(middle), qBlue(middle),
                qRed(corner), qGreen(corner), qBlue(corner));
    check(item->errorString().isEmpty() && middle != corner, "a meshLoader step renders the built-in mesh");

    // Paused at a time: the frame equals a Backend render at that time.
    const QString noise = QStringLiteral("search synth\nnoise(seed: 7).write(o0)\nrender(o0)");
    item->setProgram(noise);
    item->setRunning(false);
    item->setTime(0.25);
    image = scene.frame();
    nm::EffectRegistry registry;
    registry.loadAll(goodRoot);
    const nm::Graph graph = nm::compileGraph(noise, registry);
    nm::Backend backend;
    backend.setup(nullptr, goodRoot, QSize(96, 40));
    backend.render(graph, 0.25);
    const QImage expected = backend.readSurface();
    int worst = 0;
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 96; ++x) {
            const QRgb a = image.pixel(x, y);
            const QRgb b = expected.pixel(x, y);
            worst = std::max({worst, std::abs(qRed(a) - qRed(b)), std::abs(qGreen(a) - qGreen(b)), std::abs(qBlue(a) - qBlue(b))});
        }
    }
    std::printf("  paused frame vs Backend::readSurface: max diff %d\n", worst);
    check(!item->running() && item->time() == 0.25 && worst <= 1,
          "running false renders at `time`, matching a Backend render (float-to-8-bit rounding: 1 level)");
}

} // namespace

int main(int argc, char** argv) {
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    nm::registerQmlTypes();

    for (int round = 0; round < 3; ++round) {
        std::printf("round %d\n", round + 1);
        testItem(); // three full create / use / destroy cycles
    }

    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILED", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
