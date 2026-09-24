#pragma once

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

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

class EffectRegistry;

// Options for Backend::updateTextureFromSource (reference backend
// updateTextureFromSource `options`). flipY defaults to true, as in the
// reference backends. The reference demo host passes flipY=false for
// synth/media (its shader flips v itself) and flipY=true for filter/text.
struct ExternalTextureOptions {
    bool flipY = true;
};

// Result of a mesh load or upload (reference CanvasRenderer.loadOBJFromString/
// loadOBJFromURL: {success, vertexCount, error}).
struct MeshLoadResult {
    bool success = false;
    int vertexCount = 0; // vertices stored in the mesh textures
    QString error;       // empty on success
};

// One entry of an effect definition's `builtinMeshes`, in definition order.
// `path` is absolute: the definition's data-root-relative path (e.g.
// "share/meshes/sphere.obj") resolved against the Backend's dataRoot.
struct BuiltinMesh {
    QString name;
    QString path;
};

// A graph step whose effect reads a host-supplied mesh (definition
// `externalMesh`, e.g. render/meshLoader -> "mesh0").
struct ExternalMeshInput {
    QString meshId;
    int stepIndex = -1;
    QString effectKey;
    QVector<BuiltinMesh> builtinMeshes;
};

// QOpenGL executor for the full render-graph runtime model
// (ARCHITECTURE.md "Runtime model" table; reference/05-webgl2-backend.md):
// fullscreen-triangle effect passes, `passType:"blit"`, MRT, agent points/
// billboards, triangle meshes (`drawMode:"triangles"`), feedback ping-pong
// (the family hazard rule, pingpong.h), `repeat: N` intra-frame loops, and
// the one std140 UBO effect (`synth/remap`). Callers drive
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
    // MIDI channels are keyed "1".."16" (or a zero-based array). Each
    // channel carries cc, cc14, nrpn and polyPressure indexed by number,
    // pitchBend (default 8192), pressure, and heldNotes entries with
    // key/velocity/time/order. Maps use JSON objects; arrays are also accepted.
    // MPE chooses the newest held note in mpeZones.lower/upper member counts
    // (default 15). Optional ports hold connected/name/state; unscopedState
    // supplies notes without a port when aggregating MPE across sources.
    // Audio has legacy band fields, optional selected `devices`, and
    // defaultChannels keyed "1".."32" (or a zero-based array). Channel band
    // states carry low/mid/high/vol/raw/rawReady; unavailable channels are
    // omitted. The host owns capture, connection state and snapshot resets.
    // nm::MidiState::snapshot() (midi_state.h) and nm::AudioState /
    // nm::AudioInput::snapshot() (audio_state.h) produce these objects from
    // raw MIDI bytes and audio samples. Their clockCount, keys, waveform and
    // spectrum also feed the midiClockCount, midiNoteGrid, audioWaveform and
    // audioSpectrum engine inputs, as reference updateGlobalUniforms does.
    void setMidiState(const QJsonObject& state);
    void setAudioState(const QJsonObject& state);

    // Resolve a compiler-produced automation descriptor at normalized time.
    // Non-automation values pass through unchanged; paramSpec {min,max}
    // scales the normalized automation output for its consumer uniform.
    QJsonValue resolveUniformValue(const QJsonValue& value, double normalizedTime,
                                   const QJsonObject& paramSpec = {}) const;

    // Capture requirements derived recursively from every pass uniform.
    QJsonObject getAudioInputRequirements(const Graph& graph) const;

    // ------------------------------------------------ external textures
    // Effects that declare `externalTexture` (synth/media -> imageTex,
    // filter/text -> textTex) read a host-supplied texture whose id is
    // "<externalTexture>_step_<stepIndex>" (reference expander.js). Until
    // the host supplies one, the pass samples the 1x1 transparent-black
    // default texture, as the reference bindTextures() does.
    static QString externalTextureId(const QString& externalTexture, int stepIndex);
    // Every external texture id the graph's passes read, in pass order.
    static QStringList externalTextureIds(const Graph& graph);

    // Uploads host pixels as the RGBA8 texture `texId` (reference
    // updateTextureFromSource): LINEAR filtering, CLAMP_TO_EDGE wrap,
    // straight (non-premultiplied) alpha. flipY=false puts source row 0 at
    // texture row 0 (v = 0); flipY=true puts it at the top row (v = 1).
    // Reallocates when the size changes. Returns the uploaded size, or an
    // empty size (0x0) for an empty source. The Backend's GL context must be
    // current (the same contract as render()).
    QSize updateTextureFromSource(const QString& texId, const QImage& source,
                                  const ExternalTextureOptions& options = {});
    // Same, from tightly or loosely packed RGBA8 rows. `bytesPerLine` is the
    // source row stride and must be at least width * 4.
    QSize updateTextureFromSource(const QString& texId, const void* rgba8, int width, int height,
                                  int bytesPerLine, const ExternalTextureOptions& options = {});
    // Zero-copy variant: binds a host-owned GL_TEXTURE_2D (valid in this
    // Backend's context or a context sharing with it) as `texId`. The
    // texture is sampled as-is and never deleted by the Backend. Replaces
    // any previously uploaded texture for the same id.
    void setExternalTexture(const QString& texId, unsigned int glTexture, QSize size);
    // Forgets `texId`; the pass returns to the default texture. Deletes the
    // GL texture only if the Backend created it. Needs a current context
    // when it deletes.
    void removeExternalTexture(const QString& texId);

    // ------------------------------------------------ meshes
    // Mesh surfaces (reference pipeline.js createSurfaces): mesh0..mesh7, each
    // three 256x256 RGBA32F textures global_<meshId>_positions, _normals and
    // _uvs, all zero until a mesh is loaded. render/meshRender draws
    // triangles from them and render/meshLoader previews them. A pass input
    // such as "global_mesh0_positions_chain_0" resolves to the unscoped
    // texture, as reference bindTextures() does. Meshes stay loaded across
    // render() calls and graphs.
    //
    // loadOBJFromString and loadOBJFromFile mirror the reference
    // CanvasRenderer.loadOBJFromString and loadOBJFromURL: parse
    // (obj_parser.h), pack into 256x256 texels (more than 65536 vertices are
    // truncated with a warning), keep the packed data, and upload it. The
    // kept data is uploaded again after releaseGl() and setup(), as the
    // reference re-uploads its mesh cache. Both return {false, 0,
    // "Pipeline not ready"} before setup() and {false, 0, "Failed to load
    // OBJ: <reason>"} for an unreadable file. The GL context must be current,
    // as for render().
    MeshLoadResult loadOBJFromString(const QString& objText, const QString& meshId = QStringLiteral("mesh0"));
    MeshLoadResult loadOBJFromFile(const QString& path, const QString& meshId = QStringLiteral("mesh0"));
    // Reference backend uploadMeshData: packed RGBA32F texels (width *
    // height * 4 floats per array; positions carry w = 1 for a used vertex).
    // Positions and normals upload as RGBA32F, uvs as RGBA16F when the
    // texture is created here (mesh0..mesh7 already exist as RGBA32F, as in
    // the reference). Not kept for re-upload. Returns {false, 0, <reason>}
    // before setup(), for a non-positive size, or for a short array.
    MeshLoadResult uploadMeshData(const QString& meshId, const std::vector<float>& positionData,
                                  const std::vector<float>& normalData, const std::vector<float>& uvData,
                                  int width, int height, int vertexCount);
    // Every step whose effect definition declares `externalMesh`, in pass
    // order, with the effect's built-in meshes. The engine itself loads no
    // mesh (the mesh textures stay zero, as in the reference engine). The
    // reference demo host loads builtinMeshes.first() into meshId for each
    // such step; a host that wants the same default does the same.
    QVector<ExternalMeshInput> externalMeshes(const Graph& graph) const;

    // ------------------------------------------------ live parameters
    // Reference canvas.js applyStepParameterValues: `stepParameterValues`
    // maps "step_N" to {paramName: value}. For each pass of step N, each
    // param resolves through its effect globals spec (uniform name,
    // convertParameterForUniform) and overwrites pass.uniforms. It skips
    // automation values, surface params, colorModeUniform-controlled
    // uniforms, uniforms the pass does not carry, and inherited volumeSize.
    // It propagates to scopedParams names across the chain and expands
    // `palette` params. Returns the number of pass uniform writes. Feedback
    // and ping-pong surfaces persist because they are keyed by texture id.
    int applyStepParameterValues(Graph& graph, const EffectRegistry& registry,
                                 const QJsonObject& stepParameterValues) const;

    // Reference Pipeline.setUniform: stores a global uniform (bound to any
    // program that declares it and whose pass does not carry it) and writes
    // `value` into every pass that carries `name`, except automation values.
    // An unscoped name also updates its _node_N / _chain_N variants.
    // stateSize is capped at 2048 and volumeSize is clamped to the device.
    // An integer `palette` expands into the classicNoisedeck palette
    // uniforms. Engine uniforms (time, resolution, ...) always win.
    void setUniform(Graph& graph, const QString& name, const QJsonValue& value);

    // ------------------------------------------------ engine time
    // render(t) takes normalized loop time t in [0, 1). The reference host
    // derives it as (elapsedSeconds % loopDuration) / loopDuration with a
    // default loop duration of 10 s.
    static double normalizedLoopTime(double elapsedSeconds, double loopDurationSeconds = 10.0);
    // Engine globals per render(t), as reference Pipeline.render:
    // deltaTime = t - lastTime (0 on the first frame or while lastTime is
    // 0; 1/600 when t wrapped below lastTime), and frame = number of
    // render() calls before this one.
    // Sets lastTime without rendering (reference syncTime), so a paused
    // host re-rendering at t gets deltaTime 0.
    void syncTime(double time);
    double lastTime() const { return m_lastTime; }
    qint64 frameIndex() const { return m_frameIndex; }

    std::function<void()> addSink(const std::shared_ptr<OutputSink>& sink);
    void removeSink(OutputSink* sink);
    SinkStats sinkStats(const OutputSink* sink) const;
    bool shouldDeferRender();
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
    bool shouldSkipPass(const Pass& pass) const;
    int resolvePointCount(const Graph& graph, const Pass& pass);
    int resolveTriangleVertexCount(const Graph& graph, const Pass& pass);
    struct MeshTexture {
        unsigned int handle = 0;
        int width = 0;
        int height = 0;
    };
    const MeshTexture* findMeshTexture(const QString& texId);
    bool createZeroMeshTextures(const QString& meshId);
    void uploadMeshTexture(const QString& texId, const float* data, int width, int height,
                           unsigned int internalFormat);
    MeshLoadResult loadPackedMesh(const QString& meshId, std::vector<float> positionData,
                                  std::vector<float> normalData, std::vector<float> uvData,
                                  int vertexCount);
    void renderInternal(
        const Graph& graph,
        double t,
        std::optional<double> presentationTimestamp);
    GpuSurface& resolveInputSurface(const Graph& graph, const QString& texId);
    GpuSurface& resolveOutputSurface(const Graph& graph, const QString& texId);
    QString currentRenderSurfaceId() const;
    const GpuSurface* currentRenderSurface() const;
    QJsonObject loadEffectUniformLayout(const QString& ns, const QString& func);
    void uploadMidiNoteGrid();

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
    struct ExternalTexture {
        unsigned int handle = 0;
        int width = 0;
        int height = 0;
        bool owned = false; // created by updateTextureFromSource (deleted by the Backend)
    };
    QHash<QString, ExternalTexture> m_externalTextures;
    unsigned int m_midiNoteGridTexture = 0;        // 128x16 RGBA32F, created when a pass reads midiNoteGrid
    QHash<QString, MeshTexture> m_meshTextures;    // "global_<meshId>_<positions|normals|uvs>" -> texture
    struct CachedMesh {
        std::vector<float> positionData;
        std::vector<float> normalData;
        std::vector<float> uvData;
        int vertexCount = 0;
    };
    QHash<QString, CachedMesh> m_meshCache;        // meshId -> last loaded OBJ (reference canvas _meshCache)
    QJsonObject m_globalUniforms;                  // host setUniform() globals; engine values override
    double m_lastTime = 0.0;
    double m_deltaTime = 0.0;
    qint64 m_frameIndex = 0;
    QJsonObject m_midiState;
    QJsonObject m_audioState;
    SinkManager m_sinkManager;
    std::vector<std::weak_ptr<FrameExportQueue>> m_frameExportQueues;
};

} // namespace nm
