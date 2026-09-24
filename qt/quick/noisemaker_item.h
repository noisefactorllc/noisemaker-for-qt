#pragma once

// noisemaker_item.h -- NoisemakerItem, a Qt Quick item that compiles a DSL
// program and shows its live render. It is a QQuickFramebufferObject, so it
// needs the OpenGL scene graph backend with a 4.1 core profile context.
// Before creating the QGuiApplication:
//
//   QSurfaceFormat format;
//   format.setVersion(4, 1);
//   format.setProfile(QSurfaceFormat::CoreProfile);
//   QSurfaceFormat::setDefaultFormat(format);
//   QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
//
// then register the type and use it from QML:
//
//   nm::registerQmlTypes();              // import Noisemaker 1.0
//   NoisemakerItem {
//       dataRoot: "<NOISEMAKER_QT_DATA_ROOT>"
//       program: "search synth\nnoise().write(o0)\nrender(o0)"
//   }
//
// The item renders at its size in device pixels, re-rendering every frame
// while `running`. Changing `program` recompiles it; a new size or data
// root starts the program's surfaces over, as the reference
// Pipeline.resize does. filter/text steps are drawn with
// nm::updateTextTextures. Compile and setup errors appear in
// `errorString`, and the item then shows transparent pixels. Like the
// reference canvas, the output is composited as premultiplied alpha.

#include <QElapsedTimer>
#include <QQuickFramebufferObject>
#include <QString>

namespace nm {

class NoisemakerItem : public QQuickFramebufferObject {
    Q_OBJECT
    // DSL source.
    Q_PROPERTY(QString program READ program WRITE setProgram NOTIFY programChanged)
    // Directory holding shaders/, effects/ and fonts/: the installed
    // package's NOISEMAKER_QT_DATA_ROOT. Empty uses
    // nm::EffectRegistry::defaultDataRoot() (the NOISEMAKER_QT_DATA_ROOT
    // environment variable, else ./qt/noisemaker).
    Q_PROPERTY(QString dataRoot READ dataRoot WRITE setDataRoot NOTIFY dataRootChanged)
    // Animate. While false, every frame renders at `time`.
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    // Normalized loop time, 0 <= time < 1. While running it advances by
    // elapsed seconds / loopDuration; timeChanged is emitted only when
    // `time` is set.
    Q_PROPERTY(double time READ time WRITE setTime NOTIFY timeChanged)
    // Seconds per loop (the reference host's default is 10).
    Q_PROPERTY(double loopDuration READ loopDuration WRITE setLoopDuration NOTIFY loopDurationChanged)
    // The last compile or setup error; empty while the program renders.
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)
    // Frames rendered for the current program.
    Q_PROPERTY(int frameCount READ frameCount NOTIFY frameRendered)

public:
    explicit NoisemakerItem(QQuickItem* parent = nullptr);

    QString program() const { return m_program; }
    void setProgram(const QString& program);
    QString dataRoot() const { return m_dataRoot; }
    void setDataRoot(const QString& dataRoot);
    bool running() const { return m_running; }
    void setRunning(bool running);
    double time() const;
    void setTime(double time);
    double loopDuration() const { return m_loopDuration; }
    void setLoopDuration(double seconds);
    QString errorString() const { return m_errorString; }
    int frameCount() const { return m_frameCount; }

    Renderer* createRenderer() const override;

signals:
    void programChanged();
    void dataRootChanged();
    void runningChanged();
    void timeChanged();
    void loopDurationChanged();
    void errorStringChanged();
    void frameRendered();

private:
    friend class NoisemakerItemRenderer;

    // Called on the GUI thread (queued from the render thread).
    Q_INVOKABLE void reportFrame(quint64 generation, const QString& error);

    QString m_program;
    QString m_dataRoot;
    bool m_running = true;
    double m_baseTime = 0.0;   // time when m_clock last restarted
    double m_loopDuration = 10.0;
    QElapsedTimer m_clock;
    quint64 m_generation = 1;  // increments with each program or data root change
    QString m_errorString;
    int m_frameCount = 0;
};

// Registers NoisemakerItem with QML as `uri` major.minor, e.g.
// `import Noisemaker 1.0`. Call once before loading QML.
void registerQmlTypes(const char* uri = "Noisemaker", int versionMajor = 1, int versionMinor = 0);

} // namespace nm
