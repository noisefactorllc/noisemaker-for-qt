#pragma once

// resources.h — liveness analysis + linear-scan texture pooling. Port of the
// REFERENCE shaders/src/runtime/resources.js (source of truth, reference/04
// §1), cross-checked against godot/addons/noisemaker/compiler/graph/
// resources.gd and td/noisemaker/compiler/graph/resources.py — all three
// agree exactly (this module is small and has no interpretation gaps).
//
// This is a register-allocation analog: virtual texture ids are "values",
// physical slots "phys_N" are "registers". Pure/deterministic — no backend
// calls, no float math, no RNG. Decoupled from nm's own Pass/ExpandedPass
// shape (a plain ordered `PassIO` list of input/output texId VALUES) so it
// stays a direct, independently-testable mirror of resources.js's own
// `analyzeLiveness(passes)` / `allocateResources(passes)`, which only ever
// touch `pass.inputs`/`pass.outputs` VALUES, never any other pass field.
//
// PARITY-CRITICAL (reference/04 §1.3 — verified against this repo's own
// 210-effect catalog: 21 passes across 12 effects declare 2+ output
// attachments in one pass, e.g. points/physarum.json's agent pass
// `{outXYZ, outVel, outRGBA}`, and the JSON's OWN key order is NOT already
// alphabetical for 9 of those 12 — so this is a live, corpus-exercised
// hazard, not a theoretical one):
//   - Output allocation happens BEFORE input release within the same pass.
//   - Allocation/release order within one pass follows the ORIGINAL
//     declaration order of `pass.outputs`/`pass.inputs` (JS object
//     insertion order = source JSON text order) — callers MUST populate
//     `PassIO::inputs`/`outputs` in that exact order, not QJsonObject's
//     (alphabetical) iteration order. See expander.cpp's use of the raw-
//     JSON-text key-order recovery for exactly this reason.
//   - A texture that is both read and written at the same pass index does
//     NOT reuse its own slot at that index (`availableAfter < i`, strict).
//   - The free-slot search picks the FIRST freeList entry with
//     `availableAfter < i` (lowest freeList position, i.e. earliest release
//     among still-eligible entries), not lowest physical id.
//   - Only `global_`-prefixed ids are excluded from pooling (infinite-lived).

#include <QString>
#include <QVector>
#include <QMap>

namespace nm {

// One pass's texture-id traffic, in DECLARATION order (see hazard note
// above). Mirrors the reference's `pass.inputs`/`pass.outputs` VALUES only
// — attachment/uniform NAMES (the object keys) are irrelevant to resource
// allocation and intentionally not carried here.
struct PassIO {
    QVector<QString> inputs;
    QVector<QString> outputs;
};

struct TexLifetime {
    int start = 0;
    int end = 0;
};

// reference/04 §1.1. `start` = first pass index where a (non-`global_`)
// texId is read or written; `end` = last such index. Inputs and outputs
// both count toward liveness at the same index.
QMap<QString, TexLifetime> analyzeLiveness(const QVector<PassIO>& passes);

// reference/04 §1.2. Returns virtualId -> "phys_N". Key/iteration order of
// the returned map is irrelevant to every consumer (the GRAPH parity gate
// compares `allocations` as a plain object: content, not order); only the
// assigned "phys_N" VALUE strings are parity-significant, and those depend
// on `passes` traversal + within-pass declaration order as described above.
QMap<QString, QString> allocateResources(const QVector<PassIO>& passes);

} // namespace nm
