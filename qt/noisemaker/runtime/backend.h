#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QSize>
#include <QString>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "agents.h"
#include "frame_export.h"
#include "graph.h"
#include "output_sink.h"
#include "pingpong.h"
#include "surface.h"

class QOpenGLContext;
class QOffscreenSurface;
class QOpenGLFunctions_4_1_Core;

namespace nm {

// QOpenGL executor for the full render-graph runtime model
// (ARCHITECTURE.md "Runtime model" table; reference/05-webgl2-backend.md):
// fullscreen-triangle effect passes, `passType:"blit"`, MRT, agent points/
// billboards, feedback ping-pong (the family hazard rule, pingpong.h),
// `repeat: N` intra-frame loops, and the one std140 UBO effect
// (`synth/remap`). The public API is unchanged from T3/T4 — callers drive
// settle/timed-sample semantics externally by calling render(t) repeatedly
// (see nm-render `--frames` / `--samples`); this class does not decide how
// many frames a graph needs on its own.
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
    void render(const Graph& graph, double t, double presentationTimestamp);

    // Host-owned external-input snapshots consumed by midi()/audio()
    // automation descriptors. The JSON shape mirrors the reference runtime:
    // MIDI has channels keyed "1".."16" and optional selected `ports`;
    // audio has legacy band fields and optional selected `devices`.
    void setMidiState(const QJsonObject& state);
    void setAudioState(const QJsonObject& state);

    // Resolve a compiler-produced automation descriptor at normalized time.
    // Non-automation values pass through unchanged; paramSpec {min,max}
    // scales the normalized automation output for its consumer uniform.
    QJsonValue resolveUniformValue(const QJsonValue& value, double normalizedTime,
                                   const QJsonObject& paramSpec = {}) const;

    // Capture requirements derived recursively from every pass uniform.
    QJsonObject getAudioInputRequirements(const Graph& graph) const;

    std::function<void()> addSink(const std::shared_ptr<OutputSink>& sink);
    void removeSink(OutputSink* sink);
    SinkStats sinkStats(const OutputSink* sink) const;
    std::shared_ptr<FrameExportQueue> createFrameExportQueue(
        FrameExportOptions options = {});

    // Reads back the texture bound to `graph.renderSurface` from the most
    // recent render() call. Top-down, RGBA8888, round(v*255), no gamma
    // (PORTING-GUIDE.md "GL state parity rules"). Throws std::runtime_error
    // if no graph has been rendered yet / the surface was never written.
    QImage readSurface() const;

    // Deletes every GL object this Backend owns (SurfaceCache textures/
    // FBOs, compiled programs + their UBOs, the fullscreen/empty VAOs/VBO)
    // and leaves the object in the same state as a freshly-default-
    // constructed one w.r.t. every field it touches (m_gl null, m_surfaces
    // null, m_programs empty, VAO/VBO ids zeroed) — safe to call setup()
    // on again, or to simply let the Backend be destroyed afterward.
    //
    // Idempotent: guards on `m_gl` at entry and nulls it at exit, so a
    // second call is a no-op. Requires the CALLER to already have a valid
    // context current (unlike the owned-context destructor path, this
    // function never calls makeCurrent()/doneCurrent() itself — it doesn't
    // know which surface to bind, and for an externally-owned context that
    // is the caller's responsibility, not this Backend's).
    //
    // Exists specifically for hosts embedding Backend against a context
    // THEY own and control the lifetime of (e.g. a QOpenGLWidget) — setup()
    // never asserts sole ownership of that context, but until this
    // function existed there was no way for such a host to free this
    // Backend's GPU resources before the context itself went away (the
    // destructor only does real GL cleanup on the OWNED-context path; see
    // backend.cpp). Host contract: call this while the doomed context is
    // still current and valid — e.g. connected to
    // QOpenGLContext::aboutToBeDestroyed() — not after.
    void releaseGl();

private:
    friend struct BackendTestAccess;

    struct CompiledProgram {
        unsigned int handle = 0;
        QHash<QString, int> uniformLocations;          // GLint
        QHash<QString, unsigned int> uniformTypes;     // GLenum
        // synth/remap's std140 UBO (ARCHITECTURE.md "Uniforms"; detected
        // generically -- see programFor()). Every other program in the
        // corpus has hasUbo == false and the three fields below stay zero.
        bool hasUbo = false;
        unsigned int uboBuffer = 0;
        int uboBlockSize = 0;
    };

    void createFullscreenVao();
    void createEmptyVao();
    const CompiledProgram& programFor(const Pass& pass);
    int probeColorBytesPerSample();
    bool applyMrtFormatBudgets(Graph& graph);
    QByteArray loadEffectSource(const Pass& pass) const;
    QByteArray loadVertexSource(const Pass& pass) const;
    void executePass(const Graph& graph, const Pass& pass);
    void bindTextures(const Graph& graph, const CompiledProgram& program, const Pass& pass);
    void bindUniforms(const CompiledProgram& program, const Pass& pass);
    void bindUniformBlock(const CompiledProgram& program, const Pass& pass);
    void setUniformValue(int location, unsigned int glType, const QJsonValue& value);
    QJsonObject engineUniforms() const;
    int resolveRepeatCount(const Pass& pass) const;
    int resolvePointCount(const Graph& graph, const Pass& pass);
    void renderInternal(
        const Graph& graph,
        double t,
        std::optional<double> presentationTimestamp);
    GpuSurface& resolveInputSurface(const Graph& graph, const QString& texId);
    GpuSurface& resolveOutputSurface(const Graph& graph, const QString& texId);
    QString currentRenderSurfaceId() const;
    const GpuSurface* currentRenderSurface() const;
    QJsonObject loadEffectUniformLayout(const QString& ns, const QString& func);

    QOpenGLContext* m_context = nullptr;
    QOpenGLContext* m_ownedContext = nullptr;
    QOffscreenSurface* m_ownedSurface = nullptr;
    QOpenGLFunctions_4_1_Core* m_gl = nullptr;

    QString m_dataRoot;
    QSize m_size;
    unsigned int m_fullscreenVao = 0;
    unsigned int m_fullscreenVbo = 0;
    unsigned int m_emptyVao = 0; // no attributes/buffer; agent passes draw by gl_VertexID alone
    int m_maxTextureUnits = 16; // GL-guaranteed minimum; refined in setup() via GL_MAX_TEXTURE_IMAGE_UNITS
    int m_maxTextureSize = 0;
    int m_maxColorBytesPerSample = 0;
    bool m_warnedVolumeClamp = false;
    bool m_warnedMrtDemotion = false;

    QHash<QString, CompiledProgram> m_programs; // cache key -> compiled program
    std::unique_ptr<SurfaceCache> m_surfaces;   // texId -> GpuSurface registry
    QString m_currentRenderSurface;             // last graph.renderSurface seen by render()
    double m_time = 0.0;

    PingPongState m_pingpong;                     // cross-frame ping-pong bookkeeping (pingpong.h)
    QJsonObject m_mergedUniforms;                  // this render()'s graph-wide uniform merge
    QHash<QString, QJsonObject> m_uniformLayoutCache; // "ns/func" -> effect JSON's uniformLayout ({} if none)
    QJsonObject m_midiState;
    QJsonObject m_audioState;
    SinkManager m_sinkManager;
    std::vector<std::weak_ptr<FrameExportQueue>> m_frameExportQueues;
};

} // namespace nm
