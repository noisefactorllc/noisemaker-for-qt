#pragma once

// async_overlay.h -- the reference Effect.asyncInit contract, ported for the
// three effects that use it at the pinned reference: filter/fibers,
// filter/scratches and filter/strayHair. Each traces worms on the CPU
// (worm_tracer.h) into a canvas (stroke_canvas.h) and uploads the canvas as
// its overlayTex.
//
// Reference lifecycle (shaders/src/runtime/pipeline.js): initAsyncEffects()
// runs every asyncInit after texture allocation (resize, compile,
// recompile) with params = {...globalUniforms}, and checkAsyncRegen()
// re-runs one node, 300 ms after a change, with the step's own parameter
// values. The trace is progressive: the canvas is uploaded after every
// third worm, so a reader sees a partial overlay until the trace completes.
//
// The port generates the quiescent state: the overlay the reference shows
// once the step values have been applied and the trace has finished. By
// default (OverlayTraceMode::Synchronous, backend.h) it generates inside
// nm::Backend::render(), before the passes that sample it, whenever the
// node's seed, density or the render size changed:
//   - A render stays a pure function of the graph, its parameters and the
//     time. Offline hosts (nm-render, the export kit, the tests) get the
//     completed overlay on the first frame. The parity goldens capture the
//     same state once the minter waits for the trace (GAP-026).
//   - The trace costs, on an Apple M4 (shared, load average 13 to 33),
//     0.1 s for fibers at 256x256 and 2.8 s at 1920x1080, 0.5 s for
//     scratches and 0.04 s for strayHair at 1920x1080. The reference in
//     Chromium 153 on the same machine completes fibers in 4.4 s at
//     256x256 and 33 s at 1920x1080.
// A live host cannot stop for that long on every edit or resize (GAP-038),
// so OverlayTraceMode::Background traces on a worker thread and keeps the
// previous overlay until the new one completes; the reference instead shows
// its canvas cleared and then filling in. Neither mode needs the
// reference's 300 ms debounce, which keeps a dragged slider from restarting
// its progressive trace: a synchronous render shows each change at once,
// and a background trace that a newer change supersedes is cancelled.
//
// Parameters. The reference passes `params` as a plain object and reads it
// with JavaScript semantics: `params.seed || 1` and
// `params.density !== undefined ? params.density : <default>`. The port
// reads the node's pass uniforms the same way (asyncInitParams()). A
// non-numeric value (an automation descriptor) becomes NaN, which draws
// nothing, as in the reference. When neither seed nor density holds a
// scalar, the reference never re-traces with the step values and keeps the
// initAsyncEffects trace, which reads the pipeline's global uniforms; the
// port does the same. The port re-traces whenever those values change,
// whichever route changed them (applyStepParameterValues, setUniform, a new
// graph); the reference re-traces from applyStepParameterValues and the
// demo's program state, and keeps a stale overlay after setUniform.

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "worm_tracer.h"

namespace nm {

class Backend;
class StrokeCanvas;
struct Graph;

// True when the reference definition of `effectKey` ("namespace.func") has
// an asyncInit that this port implements.
bool hasAsyncInit(const QString& effectKey);

// The texture names an effect's asyncInit uploads ("overlayTex"). The
// graph-scoped texture id is `${nodeId}_${name}`, as in the reference.
QStringList asyncInitTextures(const QString& effectKey);

// The graph-scoped ids of every asyncInit texture in `graph`
// ("node_1_overlayTex"), in pass order. A host texture set under one of
// these ids replaces the generated overlay (Backend::setExternalTexture).
QStringList asyncOverlayTextureIds(const Graph& graph);

// The reference `params` object for the async node that owns `uniforms`
// (the first pass of the node): the step values when seed or density holds
// a scalar, otherwise `globalUniforms`.
QJsonObject asyncInitParams(const QString& effectKey, const QJsonObject& uniforms,
                            const QJsonObject& globalUniforms);

// Runs the port of `effectKey`'s asyncInit on `canvas`, which the caller
// sizes to the render size. `onUpdate(name)` is called wherever the
// reference calls updateTexture(name, canvas).
TraceResult runAsyncInit(const QString& effectKey, StrokeCanvas& canvas, const QJsonObject& params,
                         const std::function<bool()>& isCancelled = {},
                         const std::function<void(const QString&)>& onUpdate = {});

// The completed overlay of `effectKey` at `size`: straight-alpha RGBA8,
// row 0 at the top, the bytes the reference uploads with flipY = true.
std::vector<std::uint8_t> generateAsyncOverlay(const QString& effectKey, QSize size,
                                               const QJsonObject& params);

// Per-Backend bookkeeping: which node overlays are current, and the
// background traces of OverlayTraceMode::Background (backend.h).
//
// Background traces run the tracer and the canvas (worm_tracer.h,
// stroke_canvas.h) on a worker thread each; they touch no GL and no Backend
// state, and work on their own deep copy of the parameters. sync(), on the
// render thread, uploads a completed trace, but only when it still matches
// the node's current effect, parameters and size: a newer change cancels
// the older trace and its pixels are never uploaded.
class AsyncOverlays {
public:
    AsyncOverlays();
    // Cancels every background trace and joins its thread.
    ~AsyncOverlays();
    AsyncOverlays(const AsyncOverlays&) = delete;
    AsyncOverlays& operator=(const AsyncOverlays&) = delete;

    // false (the default): sync() traces to completion before it returns.
    // true: sync() starts background traces and never waits for one.
    void setBackground(bool background) { m_background = background; }
    bool background() const { return m_background; }

    // Brings the overlay of every async node in `graph` up to date with its
    // effect, parameters and the render size, and removes the textures of
    // nodes no longer in the graph. A node whose texture the host supplied
    // is skipped, and a host texture is never removed. In the background
    // mode a node keeps its previous overlay while its new trace runs, and
    // shows a transparent overlay before its first trace at the current
    // size completes. The Backend's GL context must be current. Returns the
    // number of overlays uploaded from completed traces.
    int sync(Backend& backend, const Graph& graph, QSize size, const QJsonObject& globalUniforms);

    // True while a background trace runs, or has completed and awaits its
    // upload by the next sync().
    bool pending() const;

    // Blocks until every running background trace has finished. The next
    // sync() uploads the completed ones.
    void wait();

    // Cancels every background trace, joins it, and forgets every node
    // (after the Backend released its GL objects).
    void clear();

    // Ids of the generated textures this object keeps, in no particular order.
    QStringList textureIds() const;

private:
    struct Job;
    struct Node {
        // The overlay uploaded under textureIds.
        QString effectKey;
        QSize size;
        QJsonObject params;
        bool placeholder = false; // transparent, uploaded before the first trace completed
        QStringList textureIds;
        // The background trace for the node's current state, if any.
        std::unique_ptr<Job> job;
    };
    static void startJob(Job& job);
    void retire(std::unique_ptr<Job> job);

    std::unordered_map<QString, Node> m_nodes;  // nodeId -> overlay and trace
    std::vector<std::unique_ptr<Job>> m_retired; // cancelled traces, joined when they end
    bool m_background = false;
};

} // namespace nm
