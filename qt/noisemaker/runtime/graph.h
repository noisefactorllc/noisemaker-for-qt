#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

namespace nm {

// One entry of `graph.textures`: docs/GRAPH-JSON-SCHEMA.md "TextureSpec &
// dimensions" / reference/04-resources-pipeline.md §0. `width`/`height`/
// `depth` are kept as raw QJsonValue and resolved against a screen size at
// texture-creation time (see surface.h resolveDimension()) — a number,
// "screen"/"auto", a percent string, or a {param|screenDivide|scale}
// object.
struct TextureSpec {
    QJsonValue width;
    QJsonValue height;
    QJsonValue depth;   // present only for is3D specs
    bool is3D = false;
    QString format = QStringLiteral("rgba16f");
    QStringList usage;
};

// One executable step of a render graph (docs/GRAPH-JSON-SCHEMA.md "Pass
// (normalized)"). Field set matches the T3 brief's interface contract
// (id, progName, effectKey, passType, drawMode, inputs, outputs, uniforms,
// defines, repeat) plus the extra fields the backend needs to resolve the
// shader file / program cache key (effectNamespace, func, program, nodeId)
// — cheap to carry since they're already present in the source JSON.
//
// `inputs`/`outputs`/`uniforms`/`defines` are kept as QJsonObject (matching
// assembleShader's fixed `defines` parameter type exactly, and avoiding a
// pointless conversion for the others). Iteration order of these follows
// QJsonObject's own order, not necessarily the original JSON text order —
// see shader_assembly.h's note; texture-unit assignment order and uniform
// bind order are consequently also QJsonObject-order, which does not change
// rendered output (each named sampler/uniform still resolves to the correct
// unit/value regardless of iteration order).
struct Pass {
    QString id;
    QString passType;        // "effect" | "blit"
    QString effectNamespace; // pass.namespace in the JSON; empty for blit
    QString func;            // "blit" for blit passes
    QString progName;        // bare program basename; "blit" for blit passes
    QString program;         // program cache id (node-prefixed, define-suffixed)
    QString effectKey;       // "namespace.func"; empty for blit
    QString nodeId;
    QString drawMode;        // e.g. "points"; T3 only executes the fullscreen-triangle default
    QJsonObject defines;     // #define KEY VALUE, compile-time constants
    QJsonObject inputs;      // samplerName -> texId
    QJsonObject outputs;     // outName -> texId
    QJsonObject uniforms;    // name -> literal value
    QJsonObject uniformSpecs; // name -> {min,max}, used to scale automation values
    QJsonValue repeat;       // int | uniform-name string | undefined
    QJsonValue drawBuffers;  // int | undefined; >1 signals MRT alongside outputs.size()>1
    QJsonValue count;        // int | "input"/"auto"/"screen" string | undefined (agent vertex count)
    QJsonValue blend;        // bool | [srcFactor, dstFactor] string pair | undefined
};

// The compiled render graph consumed by nm::Backend:
// `{passes[], allocations{}, textures{}, renderSurface, id, source}` per
// docs/GRAPH-JSON-SCHEMA.md. The `programs` bookkeeping object (holding
// only the "blit" cache-traceability entry) is intentionally not retained
// — per the schema, the backend never reads shader source from it; the
// blit program is a runtime-owned constant (see backend.cpp).
struct Graph {
    QString id;
    QString source;
    QString renderSurface;                 // e.g. "o0"; empty if the graph presents nothing
    QVector<Pass> passes;                  // execution order
    QHash<QString, QString> allocations;   // virtual texId -> phys_N (kept for traceability only;
                                            // NOT used for GPU texture aliasing — see task report)
    QHash<QString, TextureSpec> textures;  // texId -> spec

    // Parses graph JSON as produced by tools/export-graph.mjs / the live
    // compiler's normalizeGraph (docs/GRAPH-JSON-SCHEMA.md). Throws
    // std::runtime_error with a descriptive message on any schema
    // violation: invalid JSON, wrong top-level shape, or a malformed pass
    // entry.
    static Graph fromJson(const QByteArray& json);
};

} // namespace nm
