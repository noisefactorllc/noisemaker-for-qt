// examples/viewer/main.cpp — the port's "smallest honest demonstration of
// embedding" (ARCHITECTURE.md "Integration surface"): a live, animated
// QOpenGLWidget render of the compiler's live-DSL path (see viewer.h/.cpp).
//
// Two modes:
//   viewer               shows the animated render in a normal window.
//   viewer --selfcheck   runs offscreen-positioned for ~3s, grabs a
//                        screenshot to /tmp/viewer.png, asserts it is
//                        non-flat (>=2 distinct pixel values) and that no
//                        GL error was observed, and exits 0 (pass) or 1
//                        (fail) accordingly.

#include "viewer.h"

#include <QApplication>
#include <QImage>
#include <QRect>
#include <QScreen>
#include <QSet>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>

namespace {

constexpr int kSelfCheckMillis = 3000;

int runSelfCheck(Viewer& viewer) {
    const QImage frame = viewer.grab().toImage(); // grab() is non-const (may repaint)
    const QString outPath = QStringLiteral("/tmp/viewer.png");
    const bool saved = frame.save(outPath);

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
    const int distinctCount = static_cast<int>(distinctPixels.size()); // qsizetype -> int; well within
                                                                        // range for any image this example renders
    const bool nonFlat = distinctCount >= 2;
    const bool noGlErrors = (glErr == 0);

    std::fprintf(stdout,
                  "SELFCHECK: ready=%s saved=%s path=%s size=%dx%d distinct_pixels=%d "
                  "nonFlat=%s glError=0x%04x noGlErrors=%s\n",
                  viewer.isReady() ? "true" : "false", saved ? "true" : "false",
                  outPath.toUtf8().constData(), frame.width(), frame.height(), distinctCount,
                  nonFlat ? "true" : "false", glErr, noGlErrors ? "true" : "false");

    if (!viewer.isReady()) {
        std::fprintf(stderr, "SELFCHECK FAIL: viewer never became ready (see initializeGL stderr above)\n");
        return 1;
    }
    if (!saved) {
        std::fprintf(stderr, "SELFCHECK FAIL: could not save %s\n", outPath.toUtf8().constData());
        return 1;
    }
    if (!nonFlat) {
        std::fprintf(stderr, "SELFCHECK FAIL: image is flat (%d distinct pixel value(s))\n", distinctCount);
        return 1;
    }
    if (!noGlErrors) {
        std::fprintf(stderr, "SELFCHECK FAIL: GL error 0x%04x observed during render\n", glErr);
        return 1;
    }
    std::fprintf(stdout, "SELFCHECK PASS\n");
    return 0;
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
    const bool selfCheck = args.contains(QStringLiteral("--selfcheck"));

    Viewer viewer;
    viewer.setWindowTitle(QStringLiteral("noisemaker-for-qt viewer"));
    if (selfCheck) {
        // Offscreen-positioned, per the brief: just past the right edge of
        // the union of all real screens, computed from actual QScreen
        // geometry rather than an arbitrary large constant.
        //
        // Disclosed platform limitation (verified, not silently
        // papered over): on this machine's cocoa QPA plugin, ANY window
        // position that does not intersect a real screen -- this
        // deliberately-computed one included, and a naive
        // move(-10000,-10000) equally -- is corrected back onto the
        // primary screen ("outside any known screen, using primary
        // screen", stderr), so the window IS briefly visible during
        // --selfcheck despite this call. It is still real, GL-backed,
        // and non-interactive -- Qt::Tool and other window-flag
        // combinations were tried and made no difference (see task
        // report); this appears to be Cocoa window-manager policy, not
        // something an application can override via QWidget placement
        // APIs. Left in place because it is still the technically
        // correct ask and is harmless -- the actual pass/fail gate below
        // (non-flat render, no GL error, exit code) is unaffected either
        // way.
        QRect virtualGeometry;
        for (const QScreen* screen : QGuiApplication::screens()) {
            virtualGeometry = virtualGeometry.united(screen->geometry());
        }
        viewer.move(virtualGeometry.right() + 100, virtualGeometry.top());
    }
    viewer.show();

    if (selfCheck) {
        int rc = 2;
        QTimer::singleShot(kSelfCheckMillis, &app, [&app, &viewer, &rc]() {
            rc = runSelfCheck(viewer);
            app.exit(rc);
        });
        app.exec();
        return rc;
    }

    return app.exec();
}
