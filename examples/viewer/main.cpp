// examples/viewer/main.cpp — the port's "smallest honest demonstration of
// embedding" (ARCHITECTURE.md "Integration surface"): a live, animated,
// resizable QOpenGLWidget render of the compiler's live-DSL path (see
// viewer.h/.cpp).
//
// Two modes:
//   viewer                        shows the animated render in a window.
//   viewer --selfcheck [out.png]  renders 30 frames at 512x512, resizes the
//                                 window to 640x360 and renders 30 more,
//                                 then grabs the window to out.png (default
//                                 viewer.png in the system temp directory).
//                                 Each phase must produce a non-flat image
//                                 of the window's size with no GL error.
//                                 Exits 0 (pass) or 1 (fail); gives up
//                                 after 20 s without frames.

#include "viewer.h"

#include <QApplication>
#include <QDir>
#include <QImage>
#include <QRect>
#include <QScreen>
#include <QSet>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

constexpr int kSelfCheckFrames = 30;
constexpr int kSelfCheckTimeoutMillis = 20000;

// Grabs the window and checks one phase. Returns true on pass.
bool checkPhase(Viewer& viewer, const char* phase, QImage* frameOut) {
    const QImage frame = viewer.grab().toImage(); // grab() is non-const (may repaint)
    // QImage::pixel() normalizes through whatever the grabbed image's
    // actual storage format is, so this is correct regardless of that
    // format -- we only need distinct GROUPING, not a specific channel
    // layout.
    QSet<QRgb> distinctPixels;
    for (int y = 0; y < frame.height(); ++y) {
        for (int x = 0; x < frame.width(); ++x) {
            distinctPixels.insert(frame.pixel(x, y));
        }
    }
    const unsigned int glErr = viewer.lastGlError();
    const int distinctCount = static_cast<int>(distinctPixels.size());
    const QSize expected = viewer.renderSize();
    const bool sized = frame.size() == expected;
    const bool nonFlat = distinctCount >= 2;
    std::fprintf(stdout,
                 "SELFCHECK %s: ready=%s window=%dx%d render=%dx%d grab=%dx%d frames=%d distinct_pixels=%d "
                 "glError=0x%04x\n",
                 phase, viewer.isReady() ? "true" : "false", viewer.width(), viewer.height(), expected.width(),
                 expected.height(), frame.width(), frame.height(), viewer.framesRendered(), distinctCount, glErr);
    if (!viewer.isReady()) {
        std::fprintf(stderr, "SELFCHECK FAIL: viewer never became ready (see initializeGL stderr above)\n");
        return false;
    }
    if (!sized) {
        std::fprintf(stderr, "SELFCHECK FAIL: %s grab is %dx%d, the render size is %dx%d\n", phase, frame.width(),
                     frame.height(), expected.width(), expected.height());
        return false;
    }
    if (!nonFlat) {
        std::fprintf(stderr, "SELFCHECK FAIL: %s image is flat (%d distinct pixel value(s))\n", phase, distinctCount);
        return false;
    }
    if (glErr != 0) {
        std::fprintf(stderr, "SELFCHECK FAIL: GL error 0x%04x observed during render\n", glErr);
        return false;
    }
    *frameOut = frame;
    return true;
}

// Calls `then` once `viewer` has presented `count` more frames.
void afterFrames(Viewer& viewer, int count, std::function<void()> then) {
    const int target = viewer.framesRendered() + count;
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(&viewer, &QOpenGLWidget::frameSwapped, &viewer,
                                   [&viewer, target, connection, then]() {
        if (viewer.framesRendered() < target) return;
        QObject::disconnect(*connection);
        then();
    });
}

} // namespace

int main(int argc, char** argv) {
    // Must be set before QApplication is constructed (some platforms
    // create native structures at that point) -- standard QOpenGLWidget
    // convention. 4.1 core matches what nm::Backend requires
    // (QOpenGLFunctions_4_1_Core -- qt/noisemaker/runtime/backend.h/.cpp).
    QSurfaceFormat fmt;
    fmt.setVersion(4, 1);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    const int selfCheckIndex = args.indexOf(QStringLiteral("--selfcheck"));
    const bool selfCheck = selfCheckIndex >= 0;
    const QString outPath = selfCheck && selfCheckIndex + 1 < args.size()
        ? args.at(selfCheckIndex + 1)
        : QDir::temp().filePath(QStringLiteral("viewer.png"));

    Viewer viewer;
    viewer.setWindowTitle(QStringLiteral("noisemaker-for-qt viewer"));
    if (selfCheck) {
        // Offscreen-positioned: just past the right edge of the union of
        // all real screens. On macOS the cocoa QPA plugin moves a window
        // that intersects no screen back onto the primary screen ("outside
        // any known screen, using primary screen"), so the window is
        // briefly visible there; the checks below are unaffected.
        QRect virtualGeometry;
        for (const QScreen* screen : QGuiApplication::screens()) {
            virtualGeometry = virtualGeometry.united(screen->geometry());
        }
        viewer.move(virtualGeometry.right() + 100, virtualGeometry.top());
    }
    viewer.show();

    if (!selfCheck) return app.exec();

    int rc = 1;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&app]() {
        std::fprintf(stderr, "SELFCHECK FAIL: no frames within %d ms\n", kSelfCheckTimeoutMillis);
        app.exit(1);
    });
    timeout.start(kSelfCheckTimeoutMillis);
    afterFrames(viewer, kSelfCheckFrames, [&]() {
        QImage first;
        if (!checkPhase(viewer, "initial", &first)) {
            app.exit(1);
            return;
        }
        viewer.resize(640, 360);
        afterFrames(viewer, kSelfCheckFrames, [&]() {
            QImage resized;
            if (!checkPhase(viewer, "resized", &resized)) {
                app.exit(1);
                return;
            }
            if (!resized.save(outPath)) {
                std::fprintf(stderr, "SELFCHECK FAIL: could not save %s\n", outPath.toUtf8().constData());
                app.exit(1);
                return;
            }
            std::fprintf(stdout, "SELFCHECK PASS (saved %s)\n", outPath.toUtf8().constData());
            rc = 0;
            app.exit(0);
        });
    });
    app.exec();
    return rc;
}
