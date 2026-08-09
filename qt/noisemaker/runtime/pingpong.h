#pragma once

#include <QHash>
#include <QJsonObject>
#include <QSet>
#include <QString>

#include "graph.h"

namespace nm {

// The family "hazard rule" (ARCHITECTURE.md "Feedback / state"; reference
// Pipeline.js swapBuffers()/updateFrameSurfaceBindings() combined with the
// createSurfaces() always-double-buffer default; cross-checked structurally
// against godot nm_backend.gd `_pingpong_surfaces`, which is the port this
// function is a byte-for-byte port OF): a `global_<name>` surface needs a
// physical read/write pair iff a pass reads it AT OR BEFORE its own first
// write --
//   (a) same-pass in-place read+write (e.g. an MRT agent-update pass that
//       both samples and writes global_xyz), or
//   (b) a read in a pass at-or-before the pass index that first writes the
//       surface (feedback / same-pass-seed hazard).
// A surface written then read only by a STRICTLY LATER pass (a forward
// dependency, e.g. a blit's output read by a downstream mixer) is a single
// flat texture and is NOT double-buffered. Returns the set of BARE names
// (the `global_` prefix stripped) needing double-buffering.
QSet<QString> computeHazardSurfaces(const Graph& graph);

// reference Pipeline.swapBuffers()'s isStateSurface / godot
// `_is_state_surface` (case-sensitive, ported verbatim): state surfaces
// (particle/sim state -- xyz, vel, rgba, trail, anything containing the
// substring "state"/"State", or the node-scoped `(xyz|vel|rgba|
// points_trail)_node_N` variants a multi-agent-pipeline graph produces)
// PERSIST their final frame bindings across frames (no end-of-frame swap
// -- the sim continues from wherever it left off). Everything else
// (display surfaces, e.g. o0-o7) TOGGLES read<->write at end of frame.
bool isStateSurface(const QString& bareName);

// reference Pipeline.collectDefaultUniforms() / godot `_merge_uniforms`:
// merges every pass's `uniforms` object across the whole graph, in pass
// order, last-write-wins on key collision. This is the "live uniform"
// context `resolveDimension`'s `param`/`screenDivide` branches consult
// (surface.h) and the source `resolveRepeatCount` reads a string `repeat`
// name from when the OWNING pass itself carries no matching key (in
// practice every pass of a compiled effect carries the full resolved
// global-param set, so a same-node lookup already succeeds; this merge is
// the reference's own behavior regardless, including its known quirk of
// unscoped-name collisions across multiple chains of the same effect --
// matched deliberately, not accidentally).
QJsonObject mergeAllPassUniforms(const Graph& graph);

// Owns the ping-pong bookkeeping for one Backend across the lifetime of a
// graph: which `global_*` bare names are hazards (computed once per
// render() call from the current graph), the CROSS-FRAME persistent
// read/write binding per hazard surface (reference `this.surfaces` /
// godot `_surfaces`), and the CURRENT FRAME's transient read/write
// binding (reference `frameReadTextures`/`frameWriteTextures` / godot
// `_frame_read`/`_frame_write`). Physical GPU surfaces are NOT owned
// here -- callers allocate/cache them (keyed by the `physicalRead()`/
// `physicalWrite()` strings this class returns) via SurfaceCache; this
// class only ever manipulates texId-shaped strings.
class PingPongState {
public:
    // (Re)computes the hazard set for `graph`. Cheap (a linear scan of the
    // graph's passes) and safe to call every render(), which is what
    // Backend does -- a Backend instance is constructed fresh per graph in
    // practice (nm-render), but recomputing defensively means a
    // hypothetical host that reuses one Backend across graphs never reads
    // a stale hazard set.
    void syncGraph(const Graph& graph);

    // `bareName` is a texId with any `global_` prefix already stripped
    // (Backend does this once per texId before calling in).
    bool isHazard(const QString& bareName) const;

    // reference/04 §10 step 4 / godot `_begin_frame`: seed this frame's
    // read/write maps from the persistent record for every hazard bare
    // name, allocating a fresh {A,B} physical-key pair (read=A, write=B)
    // the first time a bare name is ever seen.
    void beginFrame();

    // Valid only between beginFrame()/endFrame() (or, for the LAST frame
    // rendered, until the next beginFrame() -- endFrame() deliberately does
    // NOT clear these, so a caller's readSurface() after the final render()
    // of a settle loop still sees this frame's fresh bindings).
    QString physicalRead(const QString& bareName) const;
    QString physicalWrite(const QString& bareName) const;

    // reference §10.2 updateFrameSurfaceBindings / godot
    // `_update_frame_bindings`: after a pass writes a hazard surface,
    // subsequent reads within the SAME frame see the just-written buffer,
    // and the next write to it targets whatever was being read before.
    // Iterates `pass.outputs` directly (raw texId strings), stripping any
    // `global_` prefix itself.
    void updateFrameBindings(const Pass& pass);

    // reference §10.6 adoptIterationBindings / godot
    // `_adopt_iteration_bindings`: between iterations of a `repeat: N`
    // pass, mirror this iteration's already-advanced frame-local bindings
    // into the persistent record. Deliberately does NOT recompute the swap
    // from the persistent record -- a preceding non-repeat pass (e.g. a
    // seed pass earlier in the same frame) may have advanced only the
    // frame-local maps, leaving the persistent record stale; re-deriving
    // from it here would clobber the correct binding. Call after EVERY
    // iteration of a `repeat > 1` pass (matching the reference exactly),
    // not only the last.
    void adoptIterationBindings(const Pass& pass);

    // reference §10.7 swapBuffers / godot `_end_frame`: state surfaces
    // persist their final frame bindings (no swap); display surfaces
    // toggle read<->write. Does NOT clear the frame-local maps (see
    // physicalRead/physicalWrite doc) -- the next beginFrame() overwrites
    // every hazard bare name's entry before anything reads it again.
    void endFrame();

private:
    struct Binding {
        QString read;
        QString write;
    };

    QSet<QString> m_hazardBareNames;
    QHash<QString, Binding> m_persistent;
    QHash<QString, QString> m_frameRead;
    QHash<QString, QString> m_frameWrite;
};

} // namespace nm
