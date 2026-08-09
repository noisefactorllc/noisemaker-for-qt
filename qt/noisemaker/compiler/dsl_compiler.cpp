#include "dsl_compiler.h"

#include "effect_registry.h"
#include "expander.h"
#include "lexer.h"
#include "parser.h"
#include "resources.h"
#include "validator.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace nm {

namespace {

// ---------------------------------------------------------------------
// hashSource (compiler.js) — see dsl_compiler.h for the algorithm note.
// ---------------------------------------------------------------------

QString toBase36(int32_t n) {
    if (n == 0) return QStringLiteral("0");
    const bool neg = n < 0;
    // Avoid negating INT32_MIN (UB): compute the magnitude in uint32_t.
    uint32_t mag = neg ? (0u - static_cast<uint32_t>(n)) : static_cast<uint32_t>(n);
    static const char kDigits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    QString s;
    while (mag > 0) {
        s.prepend(QLatin1Char(kDigits[mag % 36]));
        mag /= 36;
    }
    return neg ? (QLatin1Char('-') + s) : s;
}

// ---------------------------------------------------------------------
// JS truthy / template-literal-stringification helpers (compiler.js
// `effectSpec.width || 'screen'` etc.; export-graph.mjs's
// `` `${pass.effectNamespace}.${pass.effectFunc}` `` defineMap key).
// ---------------------------------------------------------------------

bool jsTruthy(const QJsonValue& v) {
    if (v.isUndefined() || v.isNull()) return false;
    if (v.isBool()) return v.toBool();
    if (v.isDouble()) return v.toDouble() != 0.0;
    if (v.isString()) return !v.toString().isEmpty();
    return true; // arrays/objects are always truthy in JS, even empty ones
}

QString jsNumberToString(double v) {
    if (v == 0.0) return QStringLiteral("0");
    if (std::isnan(v)) return QStringLiteral("NaN");
    if (std::isinf(v)) return v > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), v);
    return QString::fromLatin1(buf, static_cast<int>(result.ptr - buf));
}

// `${v}` template-literal stringification: null/undefined -> "null" (JS
// coerces BOTH to the literal string "null" inside a template literal —
// actually `${undefined}` is "undefined", but effectNamespace/effectFunc
// on an effect pass are ALWAYS either a real string or explicit JSON null
// (expander.cpp always inserts one or the other, never leaves them
// Undefined) — the "undefined" case is structurally unreachable here, so
// only the null->"null" mapping matters in practice).
QString jsTemplateStringOf(const QJsonValue& v) {
    if (v.isNull() || v.isUndefined()) return QStringLiteral("null");
    if (v.isString()) return v.toString();
    if (v.isDouble()) return jsNumberToString(v.toDouble());
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    return QString();
}

// ---------------------------------------------------------------------
// extractTextureSpecs (compiler.js)
// ---------------------------------------------------------------------

QJsonObject extractTextureSpecs(const QVector<ExpandedPass>& passes, const QJsonObject& textureSpecs) {
    QJsonObject textures;
    for (auto it = textureSpecs.constBegin(); it != textureSpecs.constEnd(); ++it) {
        const QJsonObject effectSpec = it.value().toObject();
        QJsonObject spec;
        const QJsonValue widthVal = effectSpec.value(QStringLiteral("width"));
        spec.insert(QStringLiteral("width"), jsTruthy(widthVal) ? widthVal : QJsonValue(QStringLiteral("screen")));
        const QJsonValue heightVal = effectSpec.value(QStringLiteral("height"));
        spec.insert(QStringLiteral("height"), jsTruthy(heightVal) ? heightVal : QJsonValue(QStringLiteral("screen")));
        const QJsonValue formatVal = effectSpec.value(QStringLiteral("format"));
        spec.insert(QStringLiteral("format"), jsTruthy(formatVal) ? formatVal : QJsonValue(QStringLiteral("rgba16f")));
        QJsonArray usage2d{QStringLiteral("render"), QStringLiteral("sample"), QStringLiteral("copySrc"), QStringLiteral("copyDst")};
        spec.insert(QStringLiteral("usage"), usage2d);

        if (jsTruthy(effectSpec.value(QStringLiteral("is3D")))) {
            const QJsonValue depthVal = effectSpec.value(QStringLiteral("depth"));
            QJsonValue depth = jsTruthy(depthVal) ? depthVal : QJsonValue();
            if (!jsTruthy(depth)) depth = jsTruthy(widthVal) ? widthVal : QJsonValue(64);
            spec.insert(QStringLiteral("depth"), depth);
            spec.insert(QStringLiteral("is3D"), true);
            QJsonArray usage3d{QStringLiteral("storage"), QStringLiteral("sample"), QStringLiteral("copySrc"), QStringLiteral("copyDst")};
            spec.insert(QStringLiteral("usage"), usage3d);
        }
        textures.insert(it.key(), spec);
    }

    for (const ExpandedPass& pass : passes) {
        for (const auto& kv : pass.outputs) {
            const QString& texId = kv.second;
            if (texId.startsWith(QStringLiteral("global_"))) continue;
            if (textures.contains(texId)) continue;
            QJsonObject spec;
            spec.insert(QStringLiteral("width"), QStringLiteral("screen"));
            spec.insert(QStringLiteral("height"), QStringLiteral("screen"));
            spec.insert(QStringLiteral("format"), QStringLiteral("rgba16f"));
            spec.insert(QStringLiteral("usage"),
                        QJsonArray{QStringLiteral("render"), QStringLiteral("sample"), QStringLiteral("copySrc"), QStringLiteral("copyDst")});
            textures.insert(texId, spec);
        }
    }
    return textures;
}

// ---------------------------------------------------------------------
// normalizeGraph (export-graph.mjs) — the GRAPH-JSON-SCHEMA.md shape.
// ---------------------------------------------------------------------

QString deriveProgName(const ExpandedPass& pass) {
    QString s = pass.program;
    if (!pass.nodeId.isUndefined() && pass.nodeId.isString()) {
        const QString nodePrefix = pass.nodeId.toString() + QLatin1Char('_');
        if (s.startsWith(nodePrefix)) s = s.mid(nodePrefix.size());
    }
    const int suffixIdx = s.indexOf(QStringLiteral("__"));
    if (suffixIdx > 0) s = s.left(suffixIdx);
    if (s.isEmpty()) {
        s = (pass.effectFunc.isString() && !pass.effectFunc.toString().isEmpty()) ? pass.effectFunc.toString()
                                                                                   : QStringLiteral("main");
    }
    return s;
}

QJsonObject definesForPass(const ExpandedPass& pass, const QJsonObject& programs) {
    const QJsonValue progVal = programs.value(pass.program);
    if (!progVal.isObject()) return QJsonObject();
    const QJsonValue definesVal = progVal.toObject().value(QStringLiteral("defines"));
    return definesVal.isObject() ? definesVal.toObject() : QJsonObject();
}

QJsonObject normalizePass(const ExpandedPass& pass, const QJsonObject& programs, const QJsonObject& defineMap) {
    const bool isBlit = pass.isBlit;
    QJsonObject out;
    out.insert(QStringLiteral("id"), pass.id);
    out.insert(QStringLiteral("passType"), isBlit ? QStringLiteral("blit") : QStringLiteral("effect"));
    out.insert(QStringLiteral("namespace"),
               isBlit ? QJsonValue(QJsonValue::Null) : (pass.effectNamespace.isUndefined() ? QJsonValue(QJsonValue::Null) : pass.effectNamespace));
    out.insert(QStringLiteral("func"),
               isBlit ? QJsonValue(QStringLiteral("blit")) : (pass.effectFunc.isUndefined() ? QJsonValue(QJsonValue::Null) : pass.effectFunc));
    out.insert(QStringLiteral("progName"), isBlit ? QStringLiteral("blit") : deriveProgName(pass));
    out.insert(QStringLiteral("program"), pass.program.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(pass.program));
    out.insert(QStringLiteral("defines"), isBlit ? QJsonObject() : definesForPass(pass, programs));

    QJsonObject inputs;
    for (const auto& kv : pass.inputs) inputs.insert(kv.first, kv.second);
    out.insert(QStringLiteral("inputs"), inputs);
    QJsonObject outputs;
    for (const auto& kv : pass.outputs) outputs.insert(kv.first, kv.second);
    out.insert(QStringLiteral("outputs"), outputs);
    out.insert(QStringLiteral("uniforms"), pass.uniforms);
    out.insert(QStringLiteral("uniformSpecs"), pass.hasUniformSpecs ? pass.uniformSpecs : QJsonObject());

    // Promote compile-time-define globals from uniforms into defines, BY
    // THEIR GLOBAL KEY (not their resolved uniform name — export-graph.mjs
    // `normalizePass`: `if (globalKey in out.uniforms)`, matching whatever
    // key `defineMap[...]` itself uses, which IS the raw global key —
    // verified directly against export-graph.mjs's own defineMap-building
    // loop: `defs[key] = spec.define` where `key` is the `globals` object
    // key). This WOULD differ from the pass's actual uniforms KEY if a
    // define-carrying global declared a `.uniform` name distinct from its
    // own key — checked directly against the full 210-effect catalog: 3
    // globals carry both `.define` AND `.uniform` (filter/lowPoly.json
    // borderWidth/lightIntensity, synth/perlin.json dimensions), and in
    // EVERY case `.uniform` is set to the SAME string as the global's own
    // key (a no-op redundant declaration) — so `globalKey in out.uniforms`
    // still finds the right entry for all three. Were the reference ever
    // to add a global whose `.uniform` genuinely diverges from its key,
    // the reference's OWN promotion would silently no-op for it (the value
    // would stay a plain runtime uniform under its `.uniform` name) — this
    // port mirrors that exact behavior (bug-for-bug, not "fixed"), it just
    // happens to be unreachable on the current catalog.
    if (!isBlit) {
        const QString dmKey = jsTemplateStringOf(pass.effectNamespace) + QLatin1Char('.') + jsTemplateStringOf(pass.effectFunc);
        const QJsonValue dmVal = defineMap.value(dmKey);
        if (dmVal.isObject()) {
            const QJsonObject dm = dmVal.toObject();
            QJsonObject uniforms = out.value(QStringLiteral("uniforms")).toObject();
            QJsonObject defines = out.value(QStringLiteral("defines")).toObject();
            for (auto it = dm.constBegin(); it != dm.constEnd(); ++it) {
                const QString globalKey = it.key();
                const QString defineName = it.value().toString();
                if (!uniforms.contains(globalKey)) continue;
                const QJsonValue v = uniforms.value(globalKey);
                const int coerced = v.isBool() ? (v.toBool() ? 1 : 0) : static_cast<int>(std::trunc(v.toDouble()));
                defines.insert(defineName, coerced);
                uniforms.remove(globalKey);
            }
            out.insert(QStringLiteral("uniforms"), uniforms);
            out.insert(QStringLiteral("defines"), defines);
        }
    }

    if (!pass.drawMode.isUndefined()) out.insert(QStringLiteral("drawMode"), pass.drawMode);
    if (!pass.count.isUndefined()) out.insert(QStringLiteral("count"), pass.count);
    if (!pass.countUniform.isUndefined()) out.insert(QStringLiteral("countUniform"), pass.countUniform);
    if (!pass.drawBuffers.isUndefined()) out.insert(QStringLiteral("drawBuffers"), pass.drawBuffers);
    if (!pass.blend.isUndefined()) out.insert(QStringLiteral("blend"), pass.blend);
    if (!pass.repeat.isUndefined()) out.insert(QStringLiteral("repeat"), pass.repeat);
    // `clear` is never produced by this Expander (reference/03 survey —
    // the reference expander.js itself never sets pass.clear either).

    out.insert(QStringLiteral("effectKey"), pass.effectKey.isUndefined() ? QJsonValue(QJsonValue::Null) : pass.effectKey);
    out.insert(QStringLiteral("nodeId"), pass.nodeId.isUndefined() ? QJsonValue(QJsonValue::Null) : pass.nodeId);
    if (!pass.stepIndex.isUndefined()) out.insert(QStringLiteral("stepIndex"), pass.stepIndex);
    if (pass.inheritsVolumeSize) out.insert(QStringLiteral("inheritsVolumeSize"), true);
    out.insert(QStringLiteral("scopedParams"), pass.scopedParams.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(pass.scopedParams));

    return out;
}

QJsonObject normalizePrograms(const QJsonObject& programs) {
    QJsonObject out;
    for (auto it = programs.constBegin(); it != programs.constEnd(); ++it) {
        const QJsonObject prog = it.value().toObject();
        QJsonObject entry;
        const QJsonValue ul = prog.value(QStringLiteral("uniformLayout"));
        entry.insert(QStringLiteral("uniformLayout"), ul.isObject() ? ul : QJsonValue(QJsonObject()));
        const QJsonValue df = prog.value(QStringLiteral("defines"));
        entry.insert(QStringLiteral("defines"), df.isObject() ? df : QJsonValue(QJsonObject()));
        out.insert(it.key(), entry);
    }
    return out;
}

QJsonObject normalizeGraph(const QString& id, const QString& source, const QJsonValue& renderSurface,
                            const QVector<ExpandedPass>& passes, const QJsonObject& allocations,
                            const QJsonObject& textures, const QJsonObject& programs, const QJsonObject& defineMap) {
    QJsonObject out;
    out.insert(QStringLiteral("id"), id);
    out.insert(QStringLiteral("source"), source);
    out.insert(QStringLiteral("renderSurface"), (renderSurface.isUndefined() || renderSurface.isNull())
                                                     ? QJsonValue(QJsonValue::Null)
                                                     : renderSurface);
    QJsonArray normPasses;
    for (const ExpandedPass& p : passes) normPasses.append(normalizePass(p, programs, defineMap));
    out.insert(QStringLiteral("passes"), normPasses);
    out.insert(QStringLiteral("allocations"), allocations);
    out.insert(QStringLiteral("textures"), textures);
    out.insert(QStringLiteral("programs"), normalizePrograms(programs));
    return out;
}

} // namespace

QString hashSource(const QString& source) {
    uint32_t hash = 0;
    for (const QChar ch : source) {
        const uint32_t c = static_cast<uint32_t>(ch.unicode());
        // JS: hash = ((hash << 5) - hash) + c; hash = hash & hash (ToInt32
        // truncation). uint32_t wraparound arithmetic (well-defined, unlike
        // signed overflow) reproduces this bit-for-bit.
        hash = (hash << 5) - hash + c;
    }
    return toBase36(static_cast<int32_t>(hash));
}

QJsonObject compileGraphJson(const QString& source, EffectRegistry& registry) {
    const QJsonArray tokens = nm::lex(source);
    const QJsonObject ast = nm::parse(tokens);
    const QJsonObject validated = nm::validate(ast, registry);

    const QJsonArray diagnostics = validated.value(QStringLiteral("diagnostics")).toArray();
    for (const QJsonValue& dv : diagnostics) {
        if (dv.toObject().value(QStringLiteral("severity")).toString() == QStringLiteral("error")) {
            throw std::runtime_error("ERR_COMPILATION_FAILED: validate() reported an error-severity diagnostic");
        }
    }

    ExpandResult expanded = nm::expand(validated, registry);
    if (!expanded.errors.isEmpty()) {
        throw std::runtime_error("ERR_EXPANSION_FAILED: expand() reported one or more errors");
    }

    QVector<PassIO> passIOs;
    passIOs.reserve(expanded.passes.size());
    for (const ExpandedPass& p : expanded.passes) {
        PassIO io;
        io.inputs.reserve(p.inputs.size());
        for (const auto& kv : p.inputs) io.inputs.append(kv.second);
        io.outputs.reserve(p.outputs.size());
        for (const auto& kv : p.outputs) io.outputs.append(kv.second);
        passIOs.append(io);
    }
    const QMap<QString, QString> allocationsMap = nm::allocateResources(passIOs);
    QJsonObject allocations;
    for (auto it = allocationsMap.constBegin(); it != allocationsMap.constEnd(); ++it) {
        allocations.insert(it.key(), it.value());
    }

    const QJsonObject textures = extractTextureSpecs(expanded.passes, expanded.textureSpecs);
    const QString id = hashSource(source);

    return normalizeGraph(id, source, expanded.renderSurface, expanded.passes, allocations, textures,
                           expanded.programs, registry.defineMap());
}

nm::Graph compileGraph(const QString& source, EffectRegistry& registry) {
    const QJsonObject graphJson = compileGraphJson(source, registry);
    const QJsonDocument doc(graphJson);
    return nm::Graph::fromJson(doc.toJson(QJsonDocument::Compact));
}

nm::Graph compileGraph(const QString& source) {
    EffectRegistry registry;
    registry.loadAll(EffectRegistry::defaultDataRoot());
    return compileGraph(source, registry);
}

} // namespace nm
