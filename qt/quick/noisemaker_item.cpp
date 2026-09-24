#include "noisemaker_item.h"

#include "compiler/dsl_compiler.h"
#include "compiler/effect_registry.h"
#include "runtime/backend.h"
#include "runtime/text_texture.h"

#include <QDir>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QPointer>
#include <QQuickOpenGLUtils>
#include <QQuickWindow>
#include <QtQml/qqml.h>

#include <cmath>
#include <exception>
#include <memory>
#include <optional>

namespace nm {

namespace {

QString effectiveDataRoot(const QString& dataRoot) {
    return dataRoot.isEmpty() ? EffectRegistry::defaultDataRoot() : dataRoot;
}

} // namespace

// Lives on the scene graph's render thread with its OpenGL context current
// in every call. synchronize() runs while the GUI thread is blocked, so it
// is the only place that reads the item.
class NoisemakerItemRenderer : public QQuickFramebufferObject::Renderer {
public:
    ~NoisemakerItemRenderer() override {
        if (!QOpenGLContext::currentContext()) return;
        if (m_readFbo != 0) QOpenGLContext::currentContext()->extraFunctions()->glDeleteFramebuffers(1, &m_readFbo);
        if (m_backend) m_backend->releaseGl();
    }

    QOpenGLFramebufferObject* createFramebufferObject(const QSize& size) override {
        m_size = size;
        return new QOpenGLFramebufferObject(size);
    }

    void synchronize(QQuickFramebufferObject* item) override {
        auto* noisemaker = static_cast<NoisemakerItem*>(item);
        m_item = noisemaker;
        m_program = noisemaker->m_program;
        m_dataRoot = effectiveDataRoot(noisemaker->m_dataRoot);
        m_generation = noisemaker->m_generation;
        m_running = noisemaker->m_running;
        m_time = noisemaker->time();
    }

    void render() override {
        QOpenGLContext* context = QOpenGLContext::currentContext();
        QOpenGLExtraFunctions* f = context->extraFunctions();
        if (m_compiledGeneration != m_generation) {
            m_compiledGeneration = m_generation;
            m_graph.reset();
            m_error.clear();
            prepare(context);
        } else if (m_graph && m_backendSize != m_size) {
            try {
                m_backend->resize(m_size);
                m_backendSize = m_size;
                updateTextTextures(*m_backend, *m_graph, m_backendRoot, m_size);
            } catch (const std::exception& error) {
                fail(error.what());
            }
        }

        bool rendered = false;
        if (m_graph) {
            try {
                m_backend->render(*m_graph, m_time);
                rendered = true;
            } catch (const std::exception& error) {
                fail(error.what());
            }
        }

        framebufferObject()->bind();
        if (rendered) {
            // The Backend's surface is bottom-up (GL convention). The Qt 6
            // scene graph samples the item's framebuffer texture with row 0
            // at the top, so blit it flipped (measured: test_quick_item
            // "text at posY 0.15 appears at the top").
            const Backend::SurfaceTexture surface = m_backend->renderSurfaceTexture();
            if (m_readFbo == 0) f->glGenFramebuffers(1, &m_readFbo);
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_readFbo);
            f->glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, surface.texture, 0);
            f->glBlitFramebuffer(0, 0, surface.size.width(), surface.size.height(), 0, m_size.height(), m_size.width(),
                                 0, GL_COLOR_BUFFER_BIT, GL_LINEAR);
            f->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        } else {
            f->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            f->glClear(GL_COLOR_BUFFER_BIT);
        }
        QQuickOpenGLUtils::resetOpenGLState();

        if (m_item) {
            QMetaObject::invokeMethod(m_item, "reportFrame", Qt::QueuedConnection,
                                      Q_ARG(quint64, m_compiledGeneration),
                                      Q_ARG(QString, rendered ? QString() : m_error));
        }
        if (rendered && m_running) update();
    }

private:
    void fail(const QString& error) {
        m_error = QStringLiteral("NoisemakerItem: ") + error;
        m_graph.reset();
        qWarning("%s", qPrintable(m_error));
    }

    // (Re)creates what the current program needs: the effect registry and
    // Backend for the data root, then the compiled graph and its text.
    void prepare(QOpenGLContext* context) {
        try {
            const QSurfaceFormat format = context->format();
            if (format.version() < qMakePair(4, 1) || format.profile() != QSurfaceFormat::CoreProfile) {
                throw std::runtime_error(
                    QStringLiteral("needs an OpenGL 4.1 core profile context, got OpenGL %1.%2%3; call "
                                   "QSurfaceFormat::setDefaultFormat() with version 4.1 and CoreProfile before "
                                   "creating the QGuiApplication")
                        .arg(format.majorVersion()).arg(format.minorVersion())
                        .arg(format.profile() == QSurfaceFormat::CoreProfile ? QStringLiteral(" core") : QString())
                        .toStdString());
            }
            if (!m_backend || m_backendRoot != m_dataRoot) {
                const QDir root(m_dataRoot);
                if (!root.exists(QStringLiteral("effects")) || !root.exists(QStringLiteral("shaders"))) {
                    throw std::runtime_error(
                        QStringLiteral("data root '%1' has no effects/ and shaders/ directories; set dataRoot to "
                                       "the package's NOISEMAKER_QT_DATA_ROOT")
                            .arg(m_dataRoot).toStdString());
                }
                if (m_backend) m_backend->releaseGl();
                m_backend.reset();
                m_registry = std::make_unique<EffectRegistry>();
                m_registry->loadAll(m_dataRoot);
                auto backend = std::make_unique<Backend>();
                backend->setup(context, m_dataRoot, m_size);
                m_backend = std::move(backend);
                m_backendRoot = m_dataRoot;
                m_backendSize = m_size;
            } else if (m_backendSize != m_size) {
                m_backend->resize(m_size);
                m_backendSize = m_size;
            }
            m_graph = compileGraph(m_program, *m_registry);
            updateTextTextures(*m_backend, *m_graph, m_backendRoot, m_size);
            // As the reference demo host does: each meshLoader-style step
            // starts with its effect's first built-in mesh.
            for (const ExternalMeshInput& input : m_backend->externalMeshes(*m_graph)) {
                if (input.builtinMeshes.isEmpty()) continue;
                const MeshLoadResult mesh = m_backend->loadOBJFromFile(input.builtinMeshes.first().path, input.meshId);
                if (!mesh.success) throw std::runtime_error(mesh.error.toStdString());
            }
        } catch (const std::exception& error) {
            fail(error.what());
        }
    }

    QPointer<NoisemakerItem> m_item;
    QString m_program;
    QString m_dataRoot;
    quint64 m_generation = 0;
    quint64 m_compiledGeneration = 0;
    bool m_running = true;
    double m_time = 0.0;
    QSize m_size;        // the item's framebuffer size
    QSize m_backendSize; // the size the Backend renders at

    std::unique_ptr<EffectRegistry> m_registry;
    std::unique_ptr<Backend> m_backend;
    QString m_backendRoot;
    std::optional<Graph> m_graph;
    QString m_error;
    unsigned int m_readFbo = 0;
};

NoisemakerItem::NoisemakerItem(QQuickItem* parent) : QQuickFramebufferObject(parent) {
    m_clock.start();
    if (QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL) {
        m_errorString = QStringLiteral(
            "NoisemakerItem: needs the OpenGL scene graph; call "
            "QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL) before creating the window");
    }
}

QQuickFramebufferObject::Renderer* NoisemakerItem::createRenderer() const {
    return new NoisemakerItemRenderer();
}

void NoisemakerItem::setProgram(const QString& program) {
    if (program == m_program) return;
    m_program = program;
    ++m_generation;
    m_frameCount = 0;
    emit programChanged();
    update();
}

void NoisemakerItem::setDataRoot(const QString& dataRoot) {
    if (dataRoot == m_dataRoot) return;
    m_dataRoot = dataRoot;
    ++m_generation;
    m_frameCount = 0;
    emit dataRootChanged();
    update();
}

double NoisemakerItem::time() const {
    if (!m_running) return m_baseTime;
    const double t = m_baseTime + m_clock.elapsed() / 1000.0 / m_loopDuration;
    return t - std::floor(t);
}

void NoisemakerItem::setRunning(bool running) {
    if (running == m_running) return;
    m_baseTime = time();
    m_clock.restart();
    m_running = running;
    emit runningChanged();
    update();
}

void NoisemakerItem::setTime(double time) {
    m_baseTime = time - std::floor(time);
    m_clock.restart();
    emit timeChanged();
    update();
}

void NoisemakerItem::setLoopDuration(double seconds) {
    if (!(seconds > 0.0) || seconds == m_loopDuration) return;
    m_baseTime = time();
    m_clock.restart();
    m_loopDuration = seconds;
    emit loopDurationChanged();
}

void NoisemakerItem::reportFrame(quint64 generation, const QString& error) {
    if (generation != m_generation) return; // a frame of an earlier program
    if (error != m_errorString) {
        m_errorString = error;
        emit errorStringChanged();
    }
    if (error.isEmpty()) {
        ++m_frameCount;
        emit frameRendered();
    }
}

void registerQmlTypes(const char* uri, int versionMajor, int versionMinor) {
    qmlRegisterType<NoisemakerItem>(uri, versionMajor, versionMinor, "NoisemakerItem");
}

} // namespace nm
