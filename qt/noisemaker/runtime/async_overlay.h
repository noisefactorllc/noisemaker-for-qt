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
// once the step values have been applied and the trace has finished. It
// generates synchronously, inside nm::Backend::render(), before the passes
// that sample it, whenever the node's seed, density or the render size
// changed:
//   - A render stays a pure function of the graph, its parameters and the
//     time. Offline hosts (nm-render, the export kit, the tests) get the
//     completed overlay on the first frame. The parity goldens capture the
//     same state once the minter waits for the trace (GAP-026).
//   - A live host pays the trace once, on the frame after a change, instead
//     of showing a partial overlay while the trace runs. The port is faster
//     than the reference's progressive trace: on an Apple M4 (shared,
//     load average 13 to 33) fibers takes 0.1 s at 256x256 and 2.8 s at
//     1920x1080, scratches 0.5 s and strayHair 0.04 s at 1920x1080, where
//     the reference in Chromium 153 on the same machine completes fibers in
//     4.4 s at 256x256 and 33 s at 1920x1080.
// The reference's 300 ms debounce keeps a dragged slider from restarting a
// progressive trace; a synchronous trace needs none, and a render after a
// change always shows that change.
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

#include <QHash>
#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
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

// Per-Backend bookkeeping: which node overlays are current.
class AsyncOverlays {
public:
    // Regenerates and uploads the overlay of every async node in `graph`
    // whose effect, parameters or render size changed since its last
    // upload, and removes the textures of nodes no longer in the graph.
    // A node whose texture the host supplied is skipped, and a host texture
    // is never removed. The Backend's GL context must be current. Returns
    // the number of overlays generated.
    int sync(Backend& backend, const Graph& graph, QSize size, const QJsonObject& globalUniforms);

    // Forgets every node (after the Backend released its GL objects).
    void clear() { m_nodes.clear(); }

    // Ids of the generated textures this object keeps, in no particular order.
    QStringList textureIds() const;

private:
    struct Node {
        QString effectKey;
        QSize size;
        QJsonObject params;
        QStringList textureIds;
    };
    QHash<QString, Node> m_nodes; // nodeId -> last upload
};

} // namespace nm
