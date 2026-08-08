#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QSize>
#include <QString>

#include <memory>

#include "graph.h"
#include "surface.h"

class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLFunctions_4_1_Core;

namespace nm {

// QOpenGL executor for graphs whose passes are all `passType:"effect"`
// (fullscreen-triangle draws) or `passType:"blit"` — the T3 scope. Mirrors
// reference WebGL2 backend semantics (ARCHITECTURE.md "Runtime model"
// table; reference/05-webgl2-backend.md). MRT, points/billboards, feedback
// ping-pong, `repeat` looping, and UBOs are explicitly NOT implemented here
// — see the T5 task brief, which extends this class without changing this
// public API.
class Backend {
public:
    Backend();
    ~Backend();

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    // `context`: an existing, already-current QOpenGLContext to render
    // through, or nullptr to have the Backend create and own its own
    // 4.1-core context + QOffscreenSurface. `dataRoot`: filesystem root
    // containing `shaders/effects/<ns>/<func>/<prog>.frag` (= "qt/noisemaker").
    // `size`: render target dimensions. Throws std::runtime_error if
    // context/surface creation or function resolution fails.
    void setup(QOpenGLContext* context, const QString& dataRoot, QSize size);

    // Executes every pass in `graph.passes`, in order, at normalized time
    // `t`. Engine uniforms (time/resolution/tileOffset/fullResolution/
    // aspectRatio/renderScale) are recomputed from `t` and `size` on every
    // call. Does not internally loop for settle frames — callers that need
    // N settle iterations call this N times (see nm-render `--frames`).
    void render(const Graph& graph, double t);

    // Reads back the texture bound to `graph.renderSurface` from the most
    // recent render() call. Top-down, RGBA8888, round(v*255), no gamma
    // (PORTING-GUIDE.md "GL state parity rules"). Throws std::runtime_error
    // if no graph has been rendered yet / the surface was never written.
    QImage readSurface() const;

private:
    struct CompiledProgram {
        unsigned int handle = 0;
        QHash<QString, int> uniformLocations;          // GLint
        QHash<QString, unsigned int> uniformTypes;     // GLenum
    };

    void createFullscreenVao();
    const CompiledProgram& programFor(const Pass& pass);
    QByteArray loadEffectSource(const Pass& pass) const;
    void executePass(const Graph& graph, const Pass& pass);
    void bindTextures(const Graph& graph, const CompiledProgram& program, const Pass& pass);
    void bindUniforms(const CompiledProgram& program, const Pass& pass);
    void setUniformValue(int location, unsigned int glType, const QJsonValue& value);
    QJsonObject engineUniforms() const;

    QOpenGLContext* m_context = nullptr;
    QOpenGLContext* m_ownedContext = nullptr;
    QOffscreenSurface* m_ownedSurface = nullptr;
    QOpenGLFunctions_4_1_Core* m_gl = nullptr;

    QString m_dataRoot;
    QSize m_size;
    unsigned int m_fullscreenVao = 0;
    unsigned int m_fullscreenVbo = 0;

    QHash<QString, CompiledProgram> m_programs; // cache key -> compiled program
    std::unique_ptr<SurfaceCache> m_surfaces;   // texId -> GpuSurface registry
    QString m_currentRenderSurface;             // last graph.renderSurface seen by render()
    double m_time = 0.0;
};

} // namespace nm
