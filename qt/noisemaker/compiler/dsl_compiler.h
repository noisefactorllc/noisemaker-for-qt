#pragma once

// dsl_compiler.h — the orchestrator. Port of the REFERENCE
// shaders/src/runtime/compiler.js `compileGraph()` (source hash,
// extractTextureSpecs, error-throwing on diagnostics/expand errors) FUSED
// with tools/export-graph.mjs's `normalizeGraph()` (the GRAPH-JSON-SCHEMA.md
// normalization — passType/namespace/func/progName/defines, compile-time-
// define promotion out of uniforms). Cross-checked against
// godot/addons/noisemaker/compiler/graph/orchestrator.gd (`build_graph` /
// `_normalize_graph` / `_hash_source`) — both stages fused into ONE call
// there too, matching this file's own design.
//
// Runs lex -> parse -> validate -> expand -> allocateResources ->
// extractTextureSpecs -> normalizeGraph, producing the EXACT JSON shape
// tools/export-graph.mjs / tools/dump-graph.mjs produce (the GRAPH parity
// gate's oracle format, docs/GRAPH-JSON-SCHEMA.md) — then, for the
// task-brief-literal `nm::compileGraph -> nm::Graph` entry point,
// round-trips that JSON through the RENDERER's own
// `nm::Graph::fromJson()` (qt/noisemaker/runtime/graph.h, read-only here)
// so `--dsl` rendering is GUARANTEED byte-identical to
// `--graph <exported-from-the-same-source>`: both paths funnel through
// the identical parser, so the only way they could diverge is if this
// module's JSON differs from the oracle's — exactly what the GRAPH gate
// (parity/check_graph.mjs) already checks.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <stdexcept>

#include "../runtime/graph.h"

namespace nm {

class EffectRegistry;

// The objects compiler.js compileGraph() throws: {code:
// 'ERR_COMPILATION_FAILED', diagnostics} when validate() reports an
// error-severity diagnostic, and {code: 'ERR_EXPANSION_FAILED', errors} when
// expand() returns errors. what() is the text the reference formats for them
// (compiler.js formatError, which recompile() logs; Noisedeck's demo-ui
// formatCompilationError builds the same text for ERR_COMPILATION_FAILED):
// each error-severity diagnostic's message plus " (line L, col C)" when it
// has a location, joined by "; ", or each expand error's message.
class CompilationError : public std::runtime_error {
public:
    CompilationError(const QString& code, const QJsonArray& diagnostics, const QJsonArray& errors);

    // "ERR_COMPILATION_FAILED" or "ERR_EXPANSION_FAILED".
    const QString& code() const { return code_; }
    // Every validate() diagnostic, warnings included (ERR_COMPILATION_FAILED).
    const QJsonArray& diagnostics() const { return diagnostics_; }
    // expand()'s errors, [{message[, step]}] (ERR_EXPANSION_FAILED).
    const QJsonArray& errors() const { return errors_; }

    // compiler.js formatError for the two codes above.
    static QString format(const QString& code, const QJsonArray& diagnostics, const QJsonArray& errors);

private:
    QString code_;
    QJsonArray diagnostics_;
    QJsonArray errors_;
};

// JS `hashSource` (compiler.js): `hash=((hash<<5)-hash)+charCodeAt(i)`,
// ToInt32 each step (`hash & hash`), then `.toString(36)`. Uses uint32_t
// arithmetic throughout (well-defined wraparound, unlike signed overflow)
// and reinterprets as int32 only at the final base36-conversion step —
// bit-for-bit equivalent to JS's ToInt32 idiom. Godot's `_hash_source` /
// `_to_base36` (orchestrator.gd) independently implement the identical
// algorithm; cross-checked line-for-line.
QString hashSource(const QString& source);

// lex -> parse -> validate -> expand -> allocateResources ->
// extractTextureSpecs -> normalizeGraph, returning the normalized GRAPH
// JSON (docs/GRAPH-JSON-SCHEMA.md) — the EXACT shape
// tools/dump-graph.mjs's oracle produces for the same `source` and
// effect catalog. Throws nm::CompilationError mirroring compiler.js's
// ERR_COMPILATION_FAILED (a validate() diagnostic with severity:"error")
// / ERR_EXPANSION_FAILED (expand() returned errors); nm::DslSyntaxError /
// nm::UnsupportedDsl propagate unchanged from the lex/parse/validate/
// expand stages.
QJsonObject compileGraphJson(const QString& source, EffectRegistry& registry,
                             const QJsonObject& options = QJsonObject());

// compileGraphJson() then nm::Graph::fromJson() on the serialized bytes —
// see file header for why this guarantees --dsl/--graph byte-identity.
nm::Graph compileGraph(const QString& source, EffectRegistry& registry,
                       const QJsonObject& options = QJsonObject());

// Task-brief-literal signature (loads a fresh EffectRegistry from
// EffectRegistry::defaultDataRoot() internally — the SAME per-invocation-
// process pattern every other dump_*.cpp tool in this repo already uses;
// nm-render is always invoked from the repo root by the driving
// parity/*.mjs scripts / this task's --dsl flag).
nm::Graph compileGraph(const QString& source, const QJsonObject& options = QJsonObject());

} // namespace nm
