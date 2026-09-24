// examples/quick/main.cpp — nm::NoisemakerItem in a Qt Quick window.
//
//   quick [program.dsl]                       shows the program (default: a
//                                             built-in noise and text program);
//                                             Space pauses and resumes.
//   quick --selfcheck [out.png] [program.dsl] renders 30 frames at 512x512,
//                                             resizes the window to 640x360 and
//                                             renders 30 more, breaks the
//                                             program and restores it, then
//                                             saves the window to out.png
//                                             (default quick.png in the system
//                                             temp directory). Exits 0 (pass)
//                                             or 1 (fail); gives up after 20 s.

#include "quick/noisemaker_item.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QQuickItem>
#include <QQuickView>
#include <QSet>
#include <QSurfaceFormat>
#include <QTimer>

#include <cstdio>
#include <functional>
#include <memory>

namespace {

const char* const kDefaultProgram =
    "search synth, filter\n"
    "noise(seed: 3, speed: 40).text(text: \"Noisemaker for Qt\", size: 0.08, posY: 0.85).write(o0)\n"
    "render(o0)\n";

constexpr int kSelfCheckFrames = 30;
constexpr int kSelfCheckTimeoutMillis = 20000;

QString dataRoot() {
    const QByteArray fromEnvironment = qgetenv("NOISEMAKER_QT_DATA_ROOT");
    return fromEnvironment.isEmpty() ? QStringLiteral(NM_DATA_ROOT) : QString::fromLocal8Bit(fromEnvironment);
}

bool checkPhase(QQuickView& view, nm::NoisemakerItem* item, const char* phase, QImage* frameOut) {
    const QImage frame = view.grabWindow();
    QSet<QRgb> distinct;
    for (int y = 0; y < frame.height(); y += 2) {
        for (int x = 0; x < frame.width(); x += 2) distinct.insert(frame.pixel(x, y));
    }
    const QSize expected = view.size() * view.devicePixelRatio();
    std::printf("SELFCHECK %s: window=%dx%d grab=%dx%d frames=%d distinct_pixels=%d error='%s'\n", phase,
                view.width(), view.height(), frame.width(), frame.height(), item->frameCount(),
                static_cast<int>(distinct.size()), qPrintable(item->errorString()));
    const bool ok = frame.size() == expected && distinct.size() >= 2 && item->errorString().isEmpty();
    if (!ok) std::fprintf(stderr, "SELFCHECK FAIL: %s\n", phase);
    *frameOut = frame;
    return ok;
}

// Calls `then` once the item has rendered `count` more frames of its
// current program.
void afterFrames(nm::NoisemakerItem* item, int count, std::function<void()> then) {
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = QObject::connect(item, &nm::NoisemakerItem::frameRendered, item,
                                   [item, count, connection, then]() {
        if (item->frameCount() < count) return;
        QObject::disconnect(*connection);
        then();
    });
}

} // namespace

int main(int argc, char** argv) {
    // NoisemakerItem needs the OpenGL scene graph with a 4.1 core context.
    QSurfaceFormat format;
    format.setVersion(4, 1);
    format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);

    QGuiApplication app(argc, argv);
    nm::registerQmlTypes();

    QStringList args = app.arguments();
    args.removeFirst();
    const bool selfCheck = !args.isEmpty() && args.first() == QStringLiteral("--selfcheck");
    QString outPath = QDir::temp().filePath(QStringLiteral("quick.png"));
    if (selfCheck) {
        args.removeFirst();
        if (!args.isEmpty() && args.first().endsWith(QStringLiteral(".png"))) outPath = args.takeFirst();
    }
    QString program = QString::fromUtf8(kDefaultProgram);
    if (!args.isEmpty()) {
        QFile file(args.first());
        if (!file.open(QIODevice::ReadOnly)) {
            std::fprintf(stderr, "cannot open DSL program '%s'\n", qPrintable(args.first()));
            return 1;
        }
        program = QString::fromUtf8(file.readAll());
    }

    QQuickView view;
    view.setTitle(QStringLiteral("noisemaker-for-qt quick"));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setInitialProperties({{QStringLiteral("dataRoot"), dataRoot()}, {QStringLiteral("program"), program}});
    view.setSource(QUrl(QStringLiteral("qrc:/main.qml")));
    if (view.status() != QQuickView::Ready) return 1;
    view.resize(512, 512);
    auto* item = view.rootObject()->findChild<nm::NoisemakerItem*>(QStringLiteral("noisemaker"));
    view.show();
    if (!selfCheck) return app.exec();

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &app, [&app]() {
        std::fprintf(stderr, "SELFCHECK FAIL: timed out after %d ms\n", kSelfCheckTimeoutMillis);
        app.exit(1);
    });
    timeout.start(kSelfCheckTimeoutMillis);
    int rc = 1;
    QImage frame;
    afterFrames(item, kSelfCheckFrames, [&]() {
        if (!checkPhase(view, item, "initial", &frame)) return app.exit(1);
        view.resize(640, 360);
        const int resizedFrom = item->frameCount();
        afterFrames(item, resizedFrom + kSelfCheckFrames, [&]() {
            if (!checkPhase(view, item, "resized", &frame)) return app.exit(1);
            // Break the program, then restore it.
            auto broken = std::make_shared<QMetaObject::Connection>();
            *broken = QObject::connect(item, &nm::NoisemakerItem::errorStringChanged, item, [&, broken]() {
                if (item->errorString().isEmpty()) return;
                QObject::disconnect(*broken);
                std::printf("SELFCHECK broken program: error='%s'\n", qPrintable(item->errorString()));
                item->setProgram(program);
                afterFrames(item, kSelfCheckFrames, [&]() {
                    if (!checkPhase(view, item, "recovered", &frame)) return app.exit(1);
                    if (!frame.save(outPath)) {
                        std::fprintf(stderr, "SELFCHECK FAIL: could not save %s\n", qPrintable(outPath));
                        return app.exit(1);
                    }
                    std::printf("SELFCHECK PASS (saved %s)\n", qPrintable(outPath));
                    rc = 0;
                    app.exit(0);
                });
            });
            item->setProgram(QStringLiteral("search synth\nnoize().write(o0)\nrender(o0)\n"));
        });
    });
    app.exec();
    return rc;
}
