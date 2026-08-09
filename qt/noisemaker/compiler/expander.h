#pragma once

// expander.h — Logical Graph (validate() plans) -> Render Graph (passes).
// Port of the REFERENCE shaders/src/runtime/expander.js (source of truth,
// 1190 lines, read in full) + shaders/src/runtime/palette-expansion.js,
// matching reference/03-expander.md section-for-section. Cross-checked
// against godot/addons/noisemaker/compiler/graph/expander.gd and
// td/noisemaker/compiler/lang/expander.py — both agree with the JS on
// every point below; where TD's own header comments flagged a reference
// subtlety (TEXTURE_ARG_KINDS incl. vol/geo/pipeline; the defines SORT KEY
// — see below), this port re-verified directly against the live JS source
// rather than trusting either prior port's word.
//
// PARITY-CRITICAL DEVIATION FROM THE TASK BRIEF (verified, not assumed):
// the brief states compile-time defines are "sorted by define name". The
// ACTUAL reference (expander.js) sorts `Object.keys(effectDef.globals)`
// (the camelCase GLOBAL key) and inserts into `compileTimeDefines` in THAT
// order; `Object.entries(compileTimeDefines)` (which builds the program-
// cache-key suffix) then iterates in JS's insertion order = global-name-
// sorted order, NOT define-name-sorted order. These two orders genuinely
// DIFFER for real, corpus-relevant effects — e.g.
// classicNoisedeck/noise.json's globals sorted give
// [colorMode,loopOffset,metric,refractMode,type], i.e. defines order
// [COLOR_MODE,LOOP_OFFSET,METRIC,REFRACT_MODE,NOISE_TYPE] — REFRACT_MODE
// before NOISE_TYPE, the OPPOSITE of a define-name sort (verified directly
// against qt/noisemaker/effects/classicNoisedeck/noise.json; TD's own
// expander.py independently documents the identical finding). This port
// sorts by GLOBAL name (matching the reference exactly); see expander.cpp
// `collectDefines()`.
//
// Qt's QJsonObject iterates alphabetically (not insertion order — see
// effect_registry.cpp's objectKeyOrder() comment), which for THIS
// catalog's all-lowercase-leading camelCase global names coincides exactly
// with JS's default (no-comparator) Array.sort() ordinal string sort — but
// `collectDefines()` still calls an explicit std::sort on the extracted
// key list rather than relying on that coincidence, so correctness does
// not depend on an unstated Qt implementation detail.
//
// Pure data transformation — no GPU calls, no floating-point pixel math.

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPair>
#include <QString>
#include <QVector>

#include "validator.h"  // nm::UnsupportedDsl — reused as-is (see below), not redeclared.

namespace nm {

class EffectRegistry;

// Compute/MRT PassDef fields (entryPoint/workgroups/storageBuffers/
// storageTextures actually present) throw the SAME nm::UnsupportedDsl
// validator.h already declares — this expander is staged exactly like
// TD's, which imports one shared UnsupportedDsl class into BOTH its
// validator and expander modules (`from .validator import UnsupportedDsl`,
// td/noisemaker/compiler/lang/expander.py line 26) rather than minting a
// second, subsystem-local exception type with an identical contract
// (never silently wrong — fail loud). reference/03 survey area (no field
// in this catalog's PassDefs currently sets these — a real compute
// pipeline is out of scope for this first cut).

// One expanded GPU pass in the RAW (pre-normalization) shape
// shaders/src/runtime/expander.js itself produces (reference/03 §2.1
// effect passes, §2.2 blit passes) — i.e. BEFORE tools/export-graph.mjs's
// normalizeGraph() derives passType/progName/promotes defines/etc. Both
// the EXPAND gate's raw dump (qt/tests/expand_dump.cpp) and the GRAPH
// gate's normalized dump (dsl_compiler.cpp) are built FROM this shape, so
// it carries every field either consumer needs.
//
// Optional fields absent in the reference's own object literal for a given
// pass kind (see effect-vs-blit shape notes below) use
// QJsonValue(QJsonValue::Undefined) as their sentinel (T9's established
// idiom, effect_registry.h) so RAW serialization can OMIT the key exactly
// where JS's `JSON.stringify` would drop an `undefined`-valued property —
// never emit a JSON `null` in its place.
struct ExpandedPass {
    QString id;
    bool isBlit = false;  // true for _write/_write3d/final blit passes (reference/03 §2.2)

    QString program;  // program cache id ('blit' for blit passes)

    // --- effect-pass-only fields (always QJsonValue::Undefined on a blit
    // pass — blit's own JS object literal never has these keys at all) ---
    QJsonValue entryPoint = QJsonValue(QJsonValue::Undefined);
    QJsonValue drawMode = QJsonValue(QJsonValue::Undefined);
    QJsonValue drawBuffers = QJsonValue(QJsonValue::Undefined);
    QJsonValue count = QJsonValue(QJsonValue::Undefined);
    QJsonValue countUniform = QJsonValue(QJsonValue::Undefined);
    QJsonValue repeat = QJsonValue(QJsonValue::Undefined);
    QJsonValue blend = QJsonValue(QJsonValue::Undefined);
    QJsonValue workgroups = QJsonValue(QJsonValue::Undefined);
    QJsonValue storageBuffers = QJsonValue(QJsonValue::Undefined);
    QJsonValue storageTextures = QJsonValue(QJsonValue::Undefined);

    // inputs/outputs: ORDER PRESERVED in DECLARATION order (recovered from
    // raw effect-JSON text for effect passes; blit passes always have
    // exactly one input {"src":...} / one output {"color":...}).
    // resources.cpp's allocateResources() depends on this order for
    // correct phys_N assignment (reference/04 §1.3).
    QVector<QPair<QString, QString>> inputs;
    QVector<QPair<QString, QString>> outputs;

    QJsonObject uniforms;  // name -> literal value (bool/double/string/array/{type:'Oscillator',...})

    // --- metadata (effect passes: always present; blit passes: see
    // effectKey/effectFunc/effectNamespace notes) ---
    QJsonValue effectKey = QJsonValue(QJsonValue::Undefined);       // undefined for blit
    QJsonValue effectFunc = QJsonValue(QJsonValue::Undefined);      // undefined for blit (normalizer supplies 'blit')
    QJsonValue effectNamespace = QJsonValue(QJsonValue::Undefined); // undefined for blit; JSON null if effectDef.namespace unset
    QJsonValue nodeId = QJsonValue(QJsonValue::Undefined);          // undefined ONLY for the final-chain blit
    QJsonValue stepIndex = QJsonValue(QJsonValue::Undefined);       // undefined ONLY for the final-chain blit

    bool inheritsVolumeSize = false;  // emitted only if true (both raw + normalized shapes)

    QJsonObject uniformSpecs;  // effect passes only; absent (empty + not emitted) on blit
    bool hasUniformSpecs = false;

    QJsonObject scopedParams;  // emitted only if non-empty
};

// Full expand() result (reference/03 §1: `{ passes, errors, programs,
// textureSpecs, renderSurface }`).
struct ExpandResult {
    QVector<ExpandedPass> passes;
    QJsonArray errors;         // [{message[, step]}] — step echoed verbatim on "Effect not found"
    QJsonObject programs;      // uniqueProgName -> {...shaders, uniformLayout, defines} (RAW shape)
    QJsonObject textureSpecs;  // virtualTexId -> spec (RAW; width/height may carry scoped {param} refs)
    QJsonValue renderSurface = QJsonValue(QJsonValue::Undefined);  // string, or JSON null if unresolved
};

// Expands `validated` (nm::validate()'s output: {plans, diagnostics,
// render, vars, searchNamespaces}) into a Render Graph. `registry` supplies
// effect definitions (getEffect), the std enum tree (for member-typed
// global DEFAULTS only — reference/03 §1: the expander's own local
// resolveEnum walks ONLY the fixed std tree, never the dynamic per-effect
// choices/project tree; by the time a DSL value reaches here the validator
// has already resolved any dynamic choice to its integer), and declaration-
// ordered global names (registry.getOp()->args) + raw JSON text
// (registry.rawJson()) for the two order-sensitive iterations documented
// in expander.cpp.
//
// Throws UnsupportedDslExpand for compute/MRT PassDef fields actually
// present (entryPoint/workgroups/storageBuffers/storageTextures) —
// reference/03 survey area; never silently wrong (validator.h's family
// contract, reused here since expander.js has no interpreter fallback for
// these fields either — a real port would need compute-pipeline support
// this first cut does not have).
ExpandResult expand(const QJsonObject& validated, EffectRegistry& registry);

// Serializes one ExpandedPass to the exact RAW shape expander.js's own
// object literals produce for that pass kind (reference/03 §2.1 effect /
// §2.2 blit) — omitting every QJsonValue::Undefined field, matching
// `JSON.stringify`'s undefined-drops-the-key semantics. Used by both the
// EXPAND gate's raw dump (qt/tests/expand_dump.cpp) and internally by
// dsl_compiler.cpp's normalization (reference/03's raw shape IS
// export-graph.mjs's normalizePass() input).
QJsonObject toRawPassJson(const ExpandedPass& pass);

} // namespace nm
