#include "expander.h"

#include "effect_registry.h"
#include "dim.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <charconv>
#include <cmath>

namespace nm {

namespace {

// ---------------------------------------------------------------------
// Small value helpers (reference/03 §1.1 ArgValue / §4.5 stringification)
// ---------------------------------------------------------------------

const QSet<QString>& textureArgKinds() {
    // TEXTURE_ARG_KINDS, expander.js line 6. NOT the abbreviated
    // "{temp,output,source,feedback,xyz,vel,rgba}" list reference/03 §4.8
    // itself writes out — the LIVE source's actual Set also has 'vol',
    // 'geo', and 'pipeline' (added by upstream commit ad984822, per TD's
    // expander.py header comment, independently corroborated by reading
    // expander.js directly: `new Set(['temp','output','source','feedback',
    // 'vol','geo','xyz','vel','rgba','pipeline'])`). Missing vol/geo/
    // pipeline here would leak a 3D generator's `source`/`geoSource` (or a
    // heightMap-style `{kind:'pipeline'}` synthesized default) into
    // pass.uniforms as a raw {kind,name} object.
    static const QSet<QString> kinds = {
        QStringLiteral("temp"), QStringLiteral("output"), QStringLiteral("source"),
        QStringLiteral("feedback"), QStringLiteral("vol"), QStringLiteral("geo"),
        QStringLiteral("xyz"), QStringLiteral("vel"), QStringLiteral("rgba"),
        QStringLiteral("pipeline"),
    };
    return kinds;
}

bool isTextureArg(const QJsonValue& arg) {
    if (!arg.isObject()) return false;
    return textureArgKinds().contains(arg.toObject().value(QStringLiteral("kind")).toString());
}

// `(isObjectArg && arg.value !== undefined) ? arg.value : arg` — every
// call site in expander.js resolves an arg this same way.
QJsonValue resolveArgValue(const QJsonValue& arg) {
    if (arg.isObject()) {
        const QJsonObject o = arg.toObject();
        if (o.contains(QStringLiteral("value"))) return o.value(QStringLiteral("value"));
    }
    return arg;
}

bool isMissing(const QJsonValue& v) { return v.isUndefined() || v.isNull(); }

// The expander's OWN local `resolveEnum` closure (expander.js lines
// 120-131) walks ONLY the fixed std-enum tree — `let node = stdEnums`,
// never the dynamic per-effect project/choices tree. This is deliberately
// narrower than Validator::resolveEnum (project-before-std); by the time a
// value reaches the expander, any per-effect `choices` member has ALREADY
// been resolved to its integer by the validator, so only fixed std paths
// (e.g. a `member`-typed global's STRING default like "oscKind.sine")
// remain to resolve here.
QJsonValue resolveEnumStd(const QJsonObject& stdTree, const QString& path) {
    const QStringList parts = path.split(QLatin1Char('.'));
    if (parts.isEmpty()) return QJsonValue(QJsonValue::Undefined);
    QJsonValue node = stdTree;
    for (const QString& part : parts) {
        if (!node.isObject()) return QJsonValue(QJsonValue::Undefined);
        const QJsonObject obj = node.toObject();
        if (!obj.contains(part)) return QJsonValue(QJsonValue::Undefined);
        node = obj.value(part);
    }
    if (node.isObject()) {
        const QJsonObject leaf = node.toObject();
        if (leaf.contains(QStringLiteral("value"))) return leaf.value(QStringLiteral("value"));
    }
    return QJsonValue(QJsonValue::Undefined);
}

// def.type==='member' && typeof value==='string' -> try resolveEnumStd,
// keep original value if resolution fails (reference/03 §4.5/§4.7/§4.9).
QJsonValue resolveMemberIfNeeded(const QJsonObject& stdTree, const QJsonObject& def, QJsonValue value) {
    if (def.value(QStringLiteral("type")).toString() == QStringLiteral("member") && value.isString()) {
        const QJsonValue resolved = resolveEnumStd(stdTree, value.toString());
        if (!isMissing(resolved)) return resolved;
    }
    return value;
}

// JS `String(number)`: shortest round-trippable decimal, no trailing
// ".0" for integer-valued doubles; `-0` prints as "0" (T9's validator.cpp
// established this exact guard for the same underlying JS-parity need).
QString jsNumberToString(double v) {
    if (v == 0.0) return QStringLiteral("0");
    if (std::isnan(v)) return QStringLiteral("NaN");
    if (std::isinf(v)) return v > 0 ? QStringLiteral("Infinity") : QStringLiteral("-Infinity");
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), v);
    return QString::fromLatin1(buf, static_cast<int>(result.ptr - buf));
}

QString jsStringOf(const QJsonValue& v) {
    if (v.isBool()) return v.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    if (v.isDouble()) return jsNumberToString(v.toDouble());
    if (v.isString()) return v.toString();
    return QString();
}

// ---------------------------------------------------------------------
// Declaration-order recovery (QJsonObject does not preserve source order
// — effect_registry.cpp's objectKeyOrder() comment; T9's own established
// hazard). Two independent needs here:
//   (a) `effectDef.globals` iteration order, wherever a "first write wins"
//       rule could depend on it (reference/03 §4.7, §4.9 steps 5/6/9).
//       Solved by reusing T9's ALREADY-CORRECT declaration order, exposed
//       via EffectRegistry::getOp()->args (proven exact by test_registry.cpp).
//   (b) `passDef.inputs`/`passDef.outputs` iteration order, needed so
//       resources.cpp's allocateResources() assigns the same phys_N the
//       reference does for a MULTI-attachment pass (reference/04 §1.3).
//       Verified corpus-relevant: 21 passes across 12 effects declare 2+
//       output attachments, 9 of which are NOT already alphabetical in
//       their source JSON (e.g. points/physarum.json's agent pass
//       `{outXYZ,outVel,outRGBA}` — physarum.dsl is a real corpus fixture).
//       Solved with a small raw-text scanner here, parallel to (but
//       independent of, since it is file-local in effect_registry.cpp)
//       T9's objectKeyOrder().
// ---------------------------------------------------------------------

QStringList orderedGlobalKeys(EffectRegistry& registry, const QString& effectKey, const QJsonObject& globals) {
    const OpSpec* op = registry.getOp(effectKey);
    if (op) {
        QStringList keys;
        keys.reserve(op->args.size());
        for (const ParamDef& pd : op->args) keys.append(pd.name);
        if (keys.size() == globals.size()) return keys;
    }
    // Defensive fallback (should not happen — every effectKey reaching the
    // expander was already resolved through the same registry's ops_
    // table by the validator). Alphabetical is still a VALID key set, just
    // not guaranteed order-faithful for the rare same-uniform-name-alias
    // case (reference/03 §4.7/§4.9 step 5's "only if still undefined"
    // first-write-wins guard).
    return globals.keys();
}

// Recovers the declaration-order key list of a JSON object's DIRECT
// children from raw text, given the exact character span `[start,end)`
// that brace-delimits that object (end points one past the closing '}').
// Quote/escape-aware; does not descend into nested object/array values.
// Mirrors effect_registry.cpp's objectKeyOrder() scanning loop exactly
// (duplicated here — that one is file-local to a different translation
// unit — but operating on a caller-supplied span instead of searching for
// a named top-level key, so it works for ANY of the repeated "inputs"/
// "outputs" objects nested inside the `passes` array).
QStringList directKeyOrder(const QString& text, int start, int end) {
    QStringList keys;
    int i = start;
    int depth = 0;
    while (i < end) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) {
            const int stringStart = i;
            ++i;
            while (i < end) {
                if (text.at(i) == QLatin1Char('\\')) { i += 2; continue; }
                if (text.at(i) == QLatin1Char('"')) { ++i; break; }
                ++i;
            }
            if (depth == 0) {
                int j = i;
                while (j < end && text.at(j).isSpace()) ++j;
                if (j < end && text.at(j) == QLatin1Char(':')) {
                    keys.append(text.mid(stringStart + 1, i - stringStart - 2));
                }
            }
            continue;
        }
        if (c == QLatin1Char('{') || c == QLatin1Char('[')) { ++depth; ++i; continue; }
        if (c == QLatin1Char('}') || c == QLatin1Char(']')) { --depth; ++i; continue; }
        ++i;
    }
    return keys;
}

// Given raw effect-JSON text, returns [{inputsOrder, outputsOrder}] — one
// entry per element of the top-level `passes` array, IN ARRAY ORDER
// (QJsonArray order is reliable; only QJsonObject key order is not — see
// file header). Returns an empty list if `passes` cannot be located
// (defensive; callers fall back to QJsonObject's own — alphabetical —
// order, which is still a VALID mapping, just not guaranteed phys_N-exact).
struct PassKeyOrder {
    QStringList inputs;
    QStringList outputs;
};

QVector<PassKeyOrder> passKeyOrders(const QString& text) {
    QVector<PassKeyOrder> out;
    const QString marker = QStringLiteral("\"passes\"");
    const int markerIdx = text.indexOf(marker);
    if (markerIdx < 0) return out;
    const int openBracket = text.indexOf(QLatin1Char('['), markerIdx + marker.size());
    if (openBracket < 0) return out;

    int i = openBracket + 1;
    int depth = 1;
    const int n = text.size();
    int passStart = -1;
    int passDepthAtStart = 0;
    while (i < n && depth > 0) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) {
            ++i;
            while (i < n) {
                if (text.at(i) == QLatin1Char('\\')) { i += 2; continue; }
                if (text.at(i) == QLatin1Char('"')) { ++i; break; }
                ++i;
            }
            continue;
        }
        if (c == QLatin1Char('{')) {
            if (depth == 1 && passStart < 0) { passStart = i + 1; passDepthAtStart = depth; }
            ++depth;
            ++i;
            continue;
        }
        if (c == QLatin1Char('[')) { ++depth; ++i; continue; }
        if (c == QLatin1Char(']') || c == QLatin1Char('}')) {
            --depth;
            ++i;
            if (c == QLatin1Char('}') && depth == passDepthAtStart && passStart >= 0) {
                PassKeyOrder pko;
                pko.inputs = QStringList();
                pko.outputs = QStringList();
                // Locate "inputs"/"outputs" markers within [passStart, i-1)
                // and recover THEIR direct-child key order via directKeyOrder.
                const int passEnd = i - 1; // one past the closing '}' body (exclusive of '}')
                const QString passText = text.mid(passStart, passEnd - passStart);
                for (const QString& fieldName : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
                    const QString fieldMarker = QStringLiteral("\"%1\"").arg(fieldName);
                    const int fIdx = passText.indexOf(fieldMarker);
                    if (fIdx < 0) continue;
                    const int fOpen = passText.indexOf(QLatin1Char('{'), fIdx + fieldMarker.size());
                    if (fOpen < 0) continue;
                    // find matching close brace
                    int d = 1, k = fOpen + 1;
                    while (k < passText.size() && d > 0) {
                        const QChar fc = passText.at(k);
                        if (fc == QLatin1Char('"')) {
                            ++k;
                            while (k < passText.size()) {
                                if (passText.at(k) == QLatin1Char('\\')) { k += 2; continue; }
                                if (passText.at(k) == QLatin1Char('"')) { ++k; break; }
                                ++k;
                            }
                            continue;
                        }
                        if (fc == QLatin1Char('{') || fc == QLatin1Char('[')) ++d;
                        else if (fc == QLatin1Char('}') || fc == QLatin1Char(']')) --d;
                        ++k;
                    }
                    const QStringList order = directKeyOrder(passText, fOpen + 1, k - 1);
                    if (fieldName == QStringLiteral("inputs")) pko.inputs = order;
                    else pko.outputs = order;
                }
                out.append(pko);
                passStart = -1;
            }
            continue;
        }
        ++i;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------------
// Expander — per-expand() mutable state, mirroring the reference's
// per-plan local variables (expander.js `expand()`'s closure locals;
// TD's `_Expander` class fields, verified to be the same shape).
// ---------------------------------------------------------------------

namespace {

class Expander {
public:
    explicit Expander(EffectRegistry& registry) : registry_(registry) {}

    ExpandResult run(const QJsonArray& plans, const QJsonValue& renderDirective) {
        for (int planIndex = 0; planIndex < plans.size(); ++planIndex) {
            expandPlan(plans.at(planIndex).toObject(), planIndex);
        }
        ExpandResult result;
        if (!isMissing(renderDirective) && renderDirective.isString() && !renderDirective.toString().isEmpty()) {
            result.renderSurface = renderDirective;
        } else if (!lastWrittenSurface_.isEmpty()) {
            result.renderSurface = lastWrittenSurface_;
        } else {
            QJsonObject err;
            err.insert(QStringLiteral("message"),
                       QStringLiteral("No render surface specified and no write() found - add render(oN) or write(oN)"));
            errors_.append(err);
            result.renderSurface = QJsonValue(QJsonValue::Null);
        }
        result.passes = passes_;
        result.errors = errors_;
        result.programs = programs_;
        result.textureSpecs = textureSpecs_;
        return result;
    }

private:
    EffectRegistry& registry_;
    QVector<ExpandedPass> passes_;
    QJsonArray errors_;
    QJsonObject programs_;
    QJsonObject textureSpecs_;
    QMap<QString, QString> textureMap_;
    QString lastWrittenSurface_;
    bool blitEnsured_ = false;

    // per-plan state (reset in expandPlan)
    QString currentInput_;
    QString currentInput3d_;
    QString currentInputGeo_;
    QString currentInputXyz_;
    QString currentInputVel_;
    QString currentInputRgba_;
    QJsonValue lastInlineWriteTarget_ = QJsonValue(QJsonValue::Null);
    QString currentParticlePipelineId_;
    QJsonObject pipelineUniforms_;
    QString chainScopeId_;

    static QString nodeIdOf(int temp) { return QStringLiteral("node_%1").arg(temp); }

    void ensureBlitProgram() {
        if (programs_.contains(QStringLiteral("blit"))) return;
        // Only structural presence matters for parity (RAW dump includes
        // shader source too, but neither gate nor the Qt renderer reads
        // shader text FROM the graph — the renderer resolves blit's GLSL
        // by a fixed, backend-owned constant; reference/03 §2.3 notes the
        // built-in program body verbatim for documentation only). Store a
        // minimal-but-honest program entry: empty uniformLayout + defines,
        // matching what every OTHER port's parity gate actually compares
        // (uniformLayout/defines only — see dsl_compiler.cpp's
        // normalizePrograms, which is the sole downstream consumer).
        QJsonObject blit;
        blit.insert(QStringLiteral("uniformLayout"), QJsonObject());
        blit.insert(QStringLiteral("defines"), QJsonObject());
        programs_.insert(QStringLiteral("blit"), blit);
    }

    void registerPassthrough(const QString& nodeId) {
        if (!currentInput_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_out"), currentInput_);
        if (!currentInput3d_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_out3d"), currentInput3d_);
        if (!currentInputGeo_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_outGeo"), currentInputGeo_);
        if (!currentInputXyz_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_outXyz"), currentInputXyz_);
        if (!currentInputVel_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_outVel"), currentInputVel_);
        if (!currentInputRgba_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_outRgba"), currentInputRgba_);
    }

    QString scopeParticleTex(const QString& texName) const {
        if (currentParticlePipelineId_.isEmpty()) return texName;
        static const QRegularExpression re(QStringLiteral("^global_(xyz|vel|rgba|points_trail|life_data)$"));
        if (re.match(texName).hasMatch()) return texName + QLatin1Char('_') + currentParticlePipelineId_;
        return texName;
    }

    QString scopeChainTex(const QString& texName) const {
        const QString particleResult = scopeParticleTex(texName);
        if (particleResult != texName) return particleResult;
        if (texName.startsWith(QStringLiteral("global_"))) return texName + QLatin1Char('_') + chainScopeId_;
        return texName;
    }

    ExpandedPass makeBlit(const QString& id, const QString& src, const QString& dst,
                           const QString& nodeId, int stepTemp, bool hasNodeId) {
        ExpandedPass p;
        p.id = id;
        p.isBlit = true;
        p.program = QStringLiteral("blit");
        p.inputs.append({QStringLiteral("src"), src});
        p.outputs.append({QStringLiteral("color"), dst});
        p.uniforms = QJsonObject();
        if (hasNodeId) {
            p.nodeId = nodeId;
            p.stepIndex = stepTemp;
        }
        return p;
    }

    void expandPlan(const QJsonObject& plan, int planIndex) {
        currentInput_.clear();
        currentInput3d_.clear();
        currentInputGeo_.clear();
        currentInputXyz_.clear();
        currentInputVel_.clear();
        currentInputRgba_.clear();
        lastInlineWriteTarget_ = QJsonValue(QJsonValue::Null);
        currentParticlePipelineId_.clear();
        pipelineUniforms_ = QJsonObject();
        chainScopeId_ = QStringLiteral("chain_%1").arg(planIndex);

        const QJsonArray chain = plan.value(QStringLiteral("chain")).toArray();
        for (int stepPos = 0; stepPos < chain.size(); ++stepPos) {
            const QJsonObject step = chain.at(stepPos).toObject();
            const bool builtin = step.value(QStringLiteral("builtin")).toBool(false);
            const QString op = step.value(QStringLiteral("op")).toString();
            const QJsonObject stepArgs = step.value(QStringLiteral("args")).toObject();
            const int temp = step.value(QStringLiteral("temp")).toInt();
            const QString nodeId = nodeIdOf(temp);

            if (builtin && op == QStringLiteral("_read")) {
                const QJsonValue tex = stepArgs.value(QStringLiteral("tex"));
                if (tex.isObject() && tex.toObject().value(QStringLiteral("kind")).toString() == QStringLiteral("output")) {
                    currentInput_ = QStringLiteral("global_") + tex.toObject().value(QStringLiteral("name")).toString();
                }
                textureMap_.insert(nodeId + QStringLiteral("_out"), currentInput_);
                continue;
            }
            if (builtin && op == QStringLiteral("_read3d")) {
                const QJsonValue tex3d = stepArgs.value(QStringLiteral("tex3d"));
                const QJsonValue geo = stepArgs.value(QStringLiteral("geo"));
                if (tex3d.isObject()) {
                    const QJsonObject t = tex3d.toObject();
                    const QString kind = t.value(QStringLiteral("kind")).toString();
                    const QString type = t.value(QStringLiteral("type")).toString();
                    if (kind == QStringLiteral("vol") || type == QStringLiteral("VolRef")) {
                        currentInput3d_ = QStringLiteral("global_") + t.value(QStringLiteral("name")).toString();
                    } else {
                        const QJsonValue nameVal = t.value(QStringLiteral("name"));
                        currentInput3d_ = nameVal.isString() ? nameVal.toString() : QString();
                    }
                }
                if (geo.isObject()) {
                    const QJsonObject g = geo.toObject();
                    const QString kind = g.value(QStringLiteral("kind")).toString();
                    const QString type = g.value(QStringLiteral("type")).toString();
                    if (kind == QStringLiteral("geo") || type == QStringLiteral("GeoRef")) {
                        currentInputGeo_ = QStringLiteral("global_") + g.value(QStringLiteral("name")).toString();
                    } else {
                        const QJsonValue nameVal = g.value(QStringLiteral("name"));
                        currentInputGeo_ = nameVal.isString() ? nameVal.toString() : QString();
                    }
                }
                if (!currentInput3d_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_out3d"), currentInput3d_);
                if (!currentInputGeo_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_outGeo"), currentInputGeo_);
                continue;
            }
            if (builtin && op == QStringLiteral("_write")) {
                const QJsonValue tex = stepArgs.value(QStringLiteral("tex"));
                if (tex.isObject() && !currentInput_.isEmpty()) {
                    const QJsonObject t = tex.toObject();
                    const QString name = t.value(QStringLiteral("name")).toString();
                    if (name != QStringLiteral("none")) {
                        const QString target = QStringLiteral("global_") + name;
                        if (currentInput_ != target) {
                            passes_.append(makeBlit(nodeId + QStringLiteral("_write_blit"), currentInput_, target, nodeId, temp, true));
                            ensureBlitProgram();
                            lastWrittenSurface_ = name;
                            QJsonObject twt;
                            twt.insert(QStringLiteral("kind"), t.value(QStringLiteral("kind")));
                            twt.insert(QStringLiteral("name"), name);
                            lastInlineWriteTarget_ = twt;
                        }
                    }
                    textureMap_.insert(nodeId + QStringLiteral("_out"), currentInput_);
                }
                continue;
            }
            if (builtin && op == QStringLiteral("_write3d")) {
                const QJsonValue tex3d = stepArgs.value(QStringLiteral("tex3d"));
                const QJsonValue geo = stepArgs.value(QStringLiteral("geo"));
                if (tex3d.isObject()) {
                    const QString name = tex3d.toObject().value(QStringLiteral("name")).toString();
                    if (name != QStringLiteral("none") && !currentInput3d_.isEmpty()) {
                        const QString targetVol = QStringLiteral("global_") + name;
                        if (currentInput3d_ != targetVol) {
                            passes_.append(makeBlit(nodeId + QStringLiteral("_write3d_vol_blit"), currentInput3d_, targetVol, nodeId, temp, true));
                            ensureBlitProgram();
                        }
                    }
                }
                if (geo.isObject()) {
                    const QString name = geo.toObject().value(QStringLiteral("name")).toString();
                    if (name != QStringLiteral("none") && !currentInputGeo_.isEmpty()) {
                        const QString targetGeo = QStringLiteral("global_") + name;
                        if (currentInputGeo_ != targetGeo) {
                            passes_.append(makeBlit(nodeId + QStringLiteral("_write3d_geo_blit"), currentInputGeo_, targetGeo, nodeId, temp, true));
                        }
                    }
                }
                textureMap_.insert(nodeId + QStringLiteral("_out"), currentInput_);
                textureMap_.insert(nodeId + QStringLiteral("_out3d"), currentInput3d_);
                textureMap_.insert(nodeId + QStringLiteral("_outGeo"), currentInputGeo_);
                continue;
            }
            if (builtin && (op == QStringLiteral("_subchain_begin") || op == QStringLiteral("_subchain_end"))) {
                registerPassthrough(nodeId);
                continue;
            }

            lastInlineWriteTarget_ = QJsonValue(QJsonValue::Null);

            if (stepArgs.value(QStringLiteral("_skip")).toBool(false)) {
                registerPassthrough(nodeId);
                continue;
            }

            const QString effectName = op;
            const QJsonObject effectDef = registry_.getEffect(effectName);
            if (!registry_.hasEffect(effectName)) {
                QJsonObject err;
                err.insert(QStringLiteral("message"), QStringLiteral("Effect '%1' not found").arg(effectName));
                err.insert(QStringLiteral("step"), step);
                errors_.append(err);
                continue;
            }

            expandEffectStep(effectDef, step, stepArgs, effectName, nodeId, temp, stepPos, plan);
        }

        // final chain output (reference/03 §4.11)
        const QJsonValue writeVal = plan.value(QStringLiteral("write"));
        if (writeVal.isObject() && !currentInput_.isEmpty()) {
            const QJsonObject w = writeVal.toObject();
            const QString outName = w.value(QStringLiteral("name")).toString();
            lastWrittenSurface_ = outName;
            bool alreadyWritten = false;
            if (lastInlineWriteTarget_.isObject()) {
                const QJsonObject t = lastInlineWriteTarget_.toObject();
                alreadyWritten = t.value(QStringLiteral("kind")).toString() == QStringLiteral("output")
                                  && t.value(QStringLiteral("name")).toString() == outName;
            }
            if (alreadyWritten) return;
            const QString targetSurface = QStringLiteral("global_") + outName;
            if (currentInput_ != targetSurface) {
                passes_.append(makeBlit(QStringLiteral("final_blit_") + outName, currentInput_, targetSurface, QString(), 0, false));
                ensureBlitProgram();
            }
        }
    }

    // reference/03 §4.4-§4.10 — one real effect step.
    void expandEffectStep(const QJsonObject& effectDef, const QJsonObject& step, const QJsonObject& stepArgs,
                           const QString& effectName, const QString& nodeId, int temp, int stepPos, const QJsonObject& plan) {
        QMap<QString, QString> scopedParamMap;

        // 1. Particle-pipeline scope detection (§4.4 step 1).
        const QJsonObject textures = effectDef.value(QStringLiteral("textures")).toObject();
        if (textures.contains(QStringLiteral("global_xyz"))) {
            currentParticlePipelineId_ = nodeId;
            currentInputXyz_.clear();
            currentInputVel_.clear();
            currentInputRgba_.clear();
        }

        // 2. Compile-time defines (§4.5) — see file header for the
        // verified GLOBAL-name sort (not define-name).
        QJsonObject compileTimeDefines;
        QString programDefineSuffix;
        collectDefines(effectDef, stepArgs, effectName, compileTimeDefines, programDefineSuffix);

        // 3. Program collection (§4.6).
        const QJsonObject shadersSource = effectDef.value(QStringLiteral("shaders")).toObject();
        for (auto it = shadersSource.constBegin(); it != shadersSource.constEnd(); ++it) {
            const QString progName = it.key();
            const QString uniqueProgName = nodeId + QLatin1Char('_') + progName + programDefineSuffix;
            if (programs_.contains(uniqueProgName)) continue;
            QJsonValue layout = effectDef.value(QStringLiteral("uniformLayouts")).toObject().value(progName);
            if (!layout.isObject()) layout = effectDef.value(QStringLiteral("uniformLayout"));
            QJsonObject entry = it.value().toObject();
            entry.insert(QStringLiteral("uniformLayout"), layout.isObject() ? layout : QJsonValue(QJsonObject()));
            entry.insert(QStringLiteral("defines"), compileTimeDefines);
            programs_.insert(uniqueProgName, entry);
        }

        // 4. Texture-spec collection, 2D then 3D (§6).
        collectTextures2d(effectDef, nodeId, scopedParamMap);
        collectTextures3d(effectDef, nodeId);

        // 5. Resolve input cursor from step.from.
        const QJsonValue fromVal = step.value(QStringLiteral("from"));
        if (!fromVal.isNull() && !fromVal.isUndefined()) {
            currentInput_ = textureMap_.value(nodeIdOf(fromVal.toInt()) + QStringLiteral("_out"));
        }

        // 6. Globals -> pipelineUniforms defaults + colorMode (§4.7).
        applyGlobalDefaults(effectDef, stepArgs, effectName);

        // 7/8. Args two passes (§4.8).
        QSet<QString> colorModeControlled;
        argsFirstPass(effectDef, stepArgs, colorModeControlled);
        argsSecondPass(effectDef, stepArgs, colorModeControlled);

        // 9. Per-pass expansion (§4.9).
        expandPasses(effectDef, step, stepArgs, effectName, nodeId, temp, stepPos, plan,
                     compileTimeDefines, programDefineSuffix, scopedParamMap);

        // 10. Cursor updates (§4.10).
        updateCursorsAfterPasses(effectDef, nodeId, step.value(QStringLiteral("from")));
    }

    // reference/03 §4.5. Sorted by GLOBAL name (verified against the live
    // JS source — see file header "PARITY-CRITICAL DEVIATION").
    void collectDefines(const QJsonObject& effectDef, const QJsonObject& stepArgs, const QString& effectName,
                         QJsonObject& outDefines, QString& outSuffix) {
        const QJsonObject globals = effectDef.value(QStringLiteral("globals")).toObject();
        if (globals.isEmpty()) return;
        QStringList sortedGlobalNames = globals.keys();
        std::sort(sortedGlobalNames.begin(), sortedGlobalNames.end());

        QVector<QPair<QString, QJsonValue>> entries; // (defineName, value), in insertion order
        for (const QString& globalName : sortedGlobalNames) {
            const QJsonObject def = globals.value(globalName).toObject();
            const QJsonValue defineNameVal = def.value(QStringLiteral("define"));
            if (!defineNameVal.isString()) continue;
            const QString defineName = defineNameVal.toString();

            QJsonValue value = def.value(QStringLiteral("default"));
            if (stepArgs.contains(globalName)) {
                value = resolveArgValue(stepArgs.value(globalName));
            }
            value = resolveMemberIfNeeded(std_(), def, value);
            if (!isMissing(value)) {
                entries.append({defineName, value});
            }
        }
        for (const auto& e : entries) {
            outDefines.insert(e.first, e.second);
            outSuffix += QStringLiteral("__") + e.first + QLatin1Char('_') + jsStringOf(e.second);
        }
    }

    const QJsonObject& std_() const { return registry_.enums().std(); }

    void collectTextures2d(const QJsonObject& effectDef, const QString& nodeId, QMap<QString, QString>& scopedParamMap) {
        const QJsonObject textures = effectDef.value(QStringLiteral("textures")).toObject();
        static const QRegularExpression particleRe(QStringLiteral("^global_(xyz|vel|rgba|points_trail|life_data)$"));
        for (auto it = textures.constBegin(); it != textures.constEnd(); ++it) {
            const QString texName = it.key();
            const QJsonObject spec = it.value().toObject();
            const bool isParticleTex = particleRe.match(texName).hasMatch();
            const bool shouldScopeAsParticle = isParticleTex && !currentParticlePipelineId_.isEmpty();
            const bool shouldScopeAsChain = texName.startsWith(QStringLiteral("global_")) && !isParticleTex;

            QString virtualTexId;
            if (texName.startsWith(QStringLiteral("global_"))) {
                virtualTexId = shouldScopeAsParticle ? (texName + QLatin1Char('_') + currentParticlePipelineId_)
                                                      : (texName + QLatin1Char('_') + chainScopeId_);
            } else {
                virtualTexId = nodeId + QLatin1Char('_') + texName;
            }

            const bool hasParamRef = dim::referencesParam(spec.value(QStringLiteral("width")))
                                      || dim::referencesParam(spec.value(QStringLiteral("height")));
            QJsonObject resolvedSpec = spec;
            const bool shouldScopeParams = shouldScopeAsParticle || shouldScopeAsChain
                                            || (!currentParticlePipelineId_.isEmpty() && !texName.startsWith(QStringLiteral("global_")))
                                            || hasParamRef;
            if (shouldScopeParams) {
                const QString scopeSuffix = shouldScopeAsParticle ? currentParticlePipelineId_ : chainScopeId_;
                resolvedSpec.insert(QStringLiteral("width"), dim::scope(spec.value(QStringLiteral("width")), scopeSuffix, scopedParamMap));
                resolvedSpec.insert(QStringLiteral("height"), dim::scope(spec.value(QStringLiteral("height")), scopeSuffix, scopedParamMap));
            }
            textureSpecs_.insert(virtualTexId, resolvedSpec);
        }
    }

    void collectTextures3d(const QJsonObject& effectDef, const QString& nodeId) {
        const QJsonObject textures3d = effectDef.value(QStringLiteral("textures3d")).toObject();
        for (auto it = textures3d.constBegin(); it != textures3d.constEnd(); ++it) {
            const QString texName = it.key();
            const QString virtualTexId = texName.startsWith(QStringLiteral("global_")) ? scopeChainTex(texName)
                                                                                        : (nodeId + QLatin1Char('_') + texName);
            QJsonObject spec = it.value().toObject();
            spec.insert(QStringLiteral("is3D"), true);
            textureSpecs_.insert(virtualTexId, spec);
        }
    }

    // reference/03 §4.7.
    void applyGlobalDefaults(const QJsonObject& effectDef, const QJsonObject& stepArgs, const QString& effectKey) {
        const QJsonObject globals = effectDef.value(QStringLiteral("globals")).toObject();
        const QStringList order = orderedGlobalKeys(registry_, effectKey, globals);
        for (const QString& globalName : order) {
            const QJsonObject def = globals.value(globalName).toObject();
            const QJsonValue uniformVal = def.value(QStringLiteral("uniform"));
            const QJsonValue defaultVal = def.value(QStringLiteral("default"));
            if (uniformVal.isString() && !isMissing(defaultVal)) {
                const QString uniformName = uniformVal.toString();
                if (!pipelineUniforms_.contains(uniformName)) {
                    QJsonValue val = resolveMemberIfNeeded(std_(), def, defaultVal);
                    pipelineUniforms_.insert(uniformName, val);
                }
            }
            if (def.value(QStringLiteral("type")).toString() == QStringLiteral("surface")) {
                const QJsonValue cmu = def.value(QStringLiteral("colorModeUniform"));
                if (cmu.isString()) {
                    if (!stepArgs.contains(globalName)) {
                        const bool isNone = defaultVal.isString() && defaultVal.toString() == QStringLiteral("none");
                        pipelineUniforms_.insert(cmu.toString(), isNone ? 0 : 1);
                    }
                }
            }
        }
    }

    // reference/03 §4.8 first pass.
    void argsFirstPass(const QJsonObject& effectDef, const QJsonObject& stepArgs, QSet<QString>& colorModeControlled) {
        const QJsonObject globals = effectDef.value(QStringLiteral("globals")).toObject();
        for (auto it = stepArgs.constBegin(); it != stepArgs.constEnd(); ++it) {
            const QJsonValue arg = it.value();
            if (!isTextureArg(arg)) continue;
            const QJsonObject globalDef = globals.value(it.key()).toObject();
            const QJsonValue cmu = globalDef.value(QStringLiteral("colorModeUniform"));
            if (cmu.isString()) {
                const bool isNone = arg.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("none");
                pipelineUniforms_.insert(cmu.toString(), isNone ? 0 : 1);
                colorModeControlled.insert(cmu.toString());
            }
        }
    }

    // reference/03 §4.8 second pass.
    void argsSecondPass(const QJsonObject& effectDef, const QJsonObject& stepArgs, const QSet<QString>& colorModeControlled) {
        const QJsonObject globals = effectDef.value(QStringLiteral("globals")).toObject();
        for (auto it = stepArgs.constBegin(); it != stepArgs.constEnd(); ++it) {
            const QString argName = it.key();
            const QJsonValue arg = it.value();
            if (isTextureArg(arg)) continue;

            QString uniformName = argName;
            const QJsonObject globalDef = globals.value(argName).toObject();
            const QJsonValue uniformOverride = globalDef.value(QStringLiteral("uniform"));
            if (uniformOverride.isString()) uniformName = uniformOverride.toString();

            if (colorModeControlled.contains(uniformName)) continue;
            if (uniformName == QStringLiteral("volumeSize") && !currentInput3d_.isEmpty()
                && pipelineUniforms_.contains(QStringLiteral("volumeSize"))) {
                continue;
            }
            pipelineUniforms_.insert(uniformName, resolveArgValue(arg));
        }
    }

    void updateCursorsAfterPasses(const QJsonObject& effectDef, const QString& nodeId, const QJsonValue& stepFrom) {
        currentInput_ = textureMap_.value(nodeId + QStringLiteral("_out"));

        const QJsonValue outputTexVal = effectDef.value(QStringLiteral("outputTex"));
        if (outputTexVal.isString() && currentInput_.isEmpty()) {
            const QString internalTexName = outputTexVal.toString();
            if (internalTexName == QStringLiteral("inputTex")) {
                // reference/03 §4.10: restore from the previous node's
                // output when this effect's outputTex declaration is
                // itself just "inputTex" (a 2D passthrough effect).
                if (!stepFrom.isNull() && !stepFrom.isUndefined()) {
                    const QString prevOutKey = nodeIdOf(stepFrom.toInt()) + QStringLiteral("_out");
                    const QString prevOutput = textureMap_.value(prevOutKey);
                    if (!prevOutput.isEmpty()) {
                        textureMap_.insert(nodeId + QStringLiteral("_out"), prevOutput);
                        currentInput_ = prevOutput;
                    }
                }
            } else {
                const QString virtualTexId = internalTexName.startsWith(QStringLiteral("global_"))
                                                  ? scopeChainTex(internalTexName)
                                                  : (nodeId + QLatin1Char('_') + internalTexName);
                textureMap_.insert(nodeId + QStringLiteral("_out"), virtualTexId);
                currentInput_ = virtualTexId;
            }
        }

        const QString out3d = textureMap_.value(nodeId + QStringLiteral("_out3d"));
        if (!out3d.isEmpty()) currentInput3d_ = out3d;
        const QString outXyz = textureMap_.value(nodeId + QStringLiteral("_outXyz"));
        if (!outXyz.isEmpty()) currentInputXyz_ = outXyz;
        const QString outVel = textureMap_.value(nodeId + QStringLiteral("_outVel"));
        if (!outVel.isEmpty()) currentInputVel_ = outVel;
        const QString outRgba = textureMap_.value(nodeId + QStringLiteral("_outRgba"));
        if (!outRgba.isEmpty()) currentInputRgba_ = outRgba;

        const QJsonValue outputTex3dVal = effectDef.value(QStringLiteral("outputTex3d"));
        if (outputTex3dVal.isString() && out3d.isEmpty()) {
            const QString internalTexName = outputTex3dVal.toString();
            if (internalTexName == QStringLiteral("inputTex3d")) {
                if (!currentInput3d_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_out3d"), currentInput3d_);
            } else {
                const QString virtualTexId = internalTexName.startsWith(QStringLiteral("global_"))
                                                  ? scopeChainTex(internalTexName)
                                                  : (nodeId + QLatin1Char('_') + internalTexName);
                textureMap_.insert(nodeId + QStringLiteral("_out3d"), virtualTexId);
                currentInput3d_ = virtualTexId;
            }
        }

        const QJsonValue outputGeoVal = effectDef.value(QStringLiteral("outputGeo"));
        if (outputGeoVal.isString()) {
            const QString geoTexName = outputGeoVal.toString();
            if (geoTexName == QStringLiteral("inputGeo")) {
                if (!currentInputGeo_.isEmpty()) textureMap_.insert(nodeId + QStringLiteral("_outGeo"), currentInputGeo_);
            } else {
                const QString virtualGeoId = nodeId + QLatin1Char('_') + geoTexName;
                textureMap_.insert(nodeId + QStringLiteral("_outGeo"), virtualGeoId);
                currentInputGeo_ = virtualGeoId;
            }
        }

        applyAgentPassthrough(effectDef, QStringLiteral("outputXyz"), nodeId, QStringLiteral("_outXyz"), QStringLiteral("inputXyz"),
                               outXyz, currentInputXyz_);
        applyAgentPassthrough(effectDef, QStringLiteral("outputVel"), nodeId, QStringLiteral("_outVel"), QStringLiteral("inputVel"),
                               outVel, currentInputVel_);
        applyAgentPassthrough(effectDef, QStringLiteral("outputRgba"), nodeId, QStringLiteral("_outRgba"), QStringLiteral("inputRgba"),
                               outRgba, currentInputRgba_);
    }

    void applyAgentPassthrough(const QJsonObject& effectDef, const QString& declKey, const QString& nodeId,
                                const QString& outSuffix, const QString& reuseKeyword, const QString& already,
                                QString& cursor) {
        if (!already.isEmpty()) return; // out* already set this step (§4.10 `!outXyz` guard)
        const QJsonValue declVal = effectDef.value(declKey);
        if (!declVal.isString()) return;
        const QString texName = declVal.toString();
        if (texName == reuseKeyword) {
            if (!cursor.isEmpty()) textureMap_.insert(nodeId + outSuffix, cursor);
        } else {
            const QString virtualId = texName.startsWith(QStringLiteral("global_")) ? scopeChainTex(texName)
                                                                                     : (nodeId + QLatin1Char('_') + texName);
            textureMap_.insert(nodeId + outSuffix, virtualId);
            cursor = virtualId;
        }
    }

    // reference/03 §4.9 — builds each Pass for this effect step.
    void expandPasses(const QJsonObject& effectDef, const QJsonObject& step, const QJsonObject& stepArgs,
                       const QString& effectName, const QString& nodeId, int temp, int stepPos, const QJsonObject& plan,
                       const QJsonObject& compileTimeDefines, const QString& programDefineSuffix,
                       QMap<QString, QString>& scopedParamMap) {
        const QJsonArray effectPasses = effectDef.value(QStringLiteral("passes")).toArray();
        const QJsonObject globals = effectDef.value(QStringLiteral("globals")).toObject();
        const QStringList orderedGlobals = orderedGlobalKeys(registry_, effectName, globals);

        const QString rawJson = QString::fromUtf8(registry_.rawJson(effectName));
        const QVector<PassKeyOrder> keyOrders = rawJson.isEmpty() ? QVector<PassKeyOrder>() : passKeyOrders(rawJson);

        for (int i = 0; i < effectPasses.size(); ++i) {
            const QJsonObject passDef = effectPasses.at(i).toObject();

            if (passDef.value(QStringLiteral("entryPoint")).isString() || passDef.contains(QStringLiteral("workgroups"))
                || passDef.contains(QStringLiteral("storageBuffers")) || passDef.contains(QStringLiteral("storageTextures"))) {
                throw nm::UnsupportedDsl(QStringLiteral(
                    "compute/MRT pass fields (entryPoint/workgroups/storage*) are not implemented "
                    "in the first-cut Expander (reference/03 §2.1)."));
            }

            ExpandedPass pass;
            pass.id = QStringLiteral("%1_pass_%2").arg(nodeId).arg(i);
            pass.program = QStringLiteral("%1_%2%3").arg(nodeId, passDef.value(QStringLiteral("program")).toString(), programDefineSuffix);
            pass.entryPoint = passDef.value(QStringLiteral("entryPoint"));
            pass.drawMode = passDef.value(QStringLiteral("drawMode"));
            pass.drawBuffers = passDef.value(QStringLiteral("drawBuffers"));
            pass.count = passDef.value(QStringLiteral("count"));
            pass.countUniform = passDef.value(QStringLiteral("countUniform"));
            pass.repeat = passDef.value(QStringLiteral("repeat"));
            pass.blend = passDef.value(QStringLiteral("blend"));
            pass.workgroups = passDef.value(QStringLiteral("workgroups"));
            pass.storageBuffers = passDef.value(QStringLiteral("storageBuffers"));
            pass.storageTextures = passDef.value(QStringLiteral("storageTextures"));

            pass.effectKey = effectName;
            const QJsonValue funcVal = effectDef.value(QStringLiteral("func"));
            pass.effectFunc = funcVal.isString() && !funcVal.toString().isEmpty() ? funcVal : QJsonValue(effectName);
            const QJsonValue nsVal = effectDef.value(QStringLiteral("namespace"));
            pass.effectNamespace = (nsVal.isString() && !nsVal.toString().isEmpty()) ? nsVal : QJsonValue(QJsonValue::Null);
            pass.nodeId = nodeId;
            pass.stepIndex = temp;

            if (!currentInput3d_.isEmpty() && pipelineUniforms_.contains(QStringLiteral("volumeSize"))) {
                pass.inheritsVolumeSize = true;
            }

            pass.uniforms = pipelineUniforms_;

            // step 5: defaults fill
            for (const QString& gk : orderedGlobals) {
                const QJsonObject def = globals.value(gk).toObject();
                const QJsonValue uniformVal = def.value(QStringLiteral("uniform"));
                const QJsonValue defaultVal = def.value(QStringLiteral("default"));
                if (!uniformVal.isString() || isMissing(defaultVal)) continue;
                const QString uName = uniformVal.toString();
                if (pass.uniforms.contains(uName)) continue;
                QJsonValue val = resolveMemberIfNeeded(std_(), def, defaultVal);
                pass.uniforms.insert(uName, val);
                pipelineUniforms_.insert(uName, val);
            }

            // step 6: uniformSpecs
            pass.hasUniformSpecs = true;
            for (const QString& gk : orderedGlobals) {
                const QJsonObject def = globals.value(gk).toObject();
                const QJsonValue uniformVal = def.value(QStringLiteral("uniform"));
                const QString uName = uniformVal.isString() ? uniformVal.toString() : gk;
                const QString type = def.value(QStringLiteral("type")).toString();
                const bool hasChoices = def.value(QStringLiteral("choices")).isObject();
                if ((type == QStringLiteral("float") || type == QStringLiteral("int")) && !hasChoices) {
                    QJsonObject range;
                    const QJsonValue minVal = def.value(QStringLiteral("min"));
                    const QJsonValue maxVal = def.value(QStringLiteral("max"));
                    range.insert(QStringLiteral("min"), minVal.isDouble() ? minVal : QJsonValue(0));
                    range.insert(QStringLiteral("max"), maxVal.isDouble() ? maxVal : QJsonValue(100));
                    pass.uniformSpecs.insert(uName, range);
                }
            }

            // step 7: args -> uniforms
            for (auto it = stepArgs.constBegin(); it != stepArgs.constEnd(); ++it) {
                const QString argName = it.key();
                const QJsonValue arg = it.value();
                if (isTextureArg(arg)) continue;

                QString uniformName = argName;
                const QJsonObject globalDef = globals.value(argName).toObject();
                const QJsonValue uniformOverride = globalDef.value(QStringLiteral("uniform"));
                if (uniformOverride.isString()) uniformName = uniformOverride.toString();

                bool isControlled = false;
                for (auto git = globals.constBegin(); git != globals.constEnd(); ++git) {
                    if (git.value().toObject().value(QStringLiteral("colorModeUniform")).toString() == uniformName
                        && git.value().toObject().contains(QStringLiteral("colorModeUniform"))) {
                        isControlled = true;
                        break;
                    }
                }
                if (isControlled) continue;
                if (uniformName == QStringLiteral("volumeSize") && !currentInput3d_.isEmpty()
                    && pipelineUniforms_.contains(QStringLiteral("volumeSize"))) {
                    continue;
                }
                const QJsonValue resolved = resolveArgValue(arg);
                pass.uniforms.insert(uniformName, resolved);
                pipelineUniforms_.insert(uniformName, resolved);
            }

            // step 8: pass-level uniform wiring
            const QJsonObject passDefUniforms = passDef.value(QStringLiteral("uniforms")).toObject();
            for (auto it = passDefUniforms.constBegin(); it != passDefUniforms.constEnd(); ++it) {
                const QString uniformName = it.key();
                const QString globalRef = it.value().toString();
                if (pipelineUniforms_.contains(uniformName)) {
                    pass.uniforms.insert(uniformName, pipelineUniforms_.value(uniformName));
                } else if (!globalRef.isEmpty() && pipelineUniforms_.contains(globalRef)) {
                    pass.uniforms.insert(uniformName, pipelineUniforms_.value(globalRef));
                } else if (!globalRef.isEmpty() && globals.contains(globalRef)) {
                    const QJsonObject gdef = globals.value(globalRef).toObject();
                    const QJsonValue gdefault = gdef.value(QStringLiteral("default"));
                    if (!isMissing(gdefault)) {
                        pass.uniforms.insert(uniformName, resolveMemberIfNeeded(std_(), gdef, gdefault));
                    }
                }
            }

            // step 9: palette expansion
            expandPalettes(globals, orderedGlobals, pass);

            // steps 10/11: inputs / outputs
            mapInputs(effectDef, passDef, step, stepArgs, nodeId, plan, i, keyOrders, pass);
            mapOutputs(passDef, nodeId, plan, i, effectPasses.size(), stepPos, keyOrders, pass);

            // step 12: scoped-param propagation
            for (auto spIt = scopedParamMap.constBegin(); spIt != scopedParamMap.constEnd(); ++spIt) {
                const QString orig = spIt.key();
                const QString scoped = spIt.value();
                if (pass.uniforms.contains(orig)) {
                    pass.uniforms.insert(scoped, pass.uniforms.value(orig));
                    pipelineUniforms_.insert(scoped, pass.uniforms.value(orig));
                }
            }
            if (!scopedParamMap.isEmpty()) {
                QJsonObject sp;
                for (auto spIt = scopedParamMap.constBegin(); spIt != scopedParamMap.constEnd(); ++spIt) sp.insert(spIt.key(), spIt.value());
                pass.scopedParams = sp;
            }

            passes_.append(pass);
        }
    }

    void expandPalettes(const QJsonObject& globals, const QStringList& orderedGlobals, ExpandedPass& pass) {
        for (const QString& gk : orderedGlobals) {
            const QJsonObject def = globals.value(gk).toObject();
            if (def.value(QStringLiteral("type")).toString() != QStringLiteral("palette")) continue;
            const QJsonValue uniformVal = def.value(QStringLiteral("uniform"));
            const QString uName = uniformVal.isString() ? uniformVal.toString() : gk;
            const QJsonValue indexVal = pass.uniforms.value(uName);
            if (!indexVal.isDouble()) continue;
            const QJsonObject expanded = expandPaletteVectors(static_cast<int>(indexVal.toDouble()));
            if (expanded.isEmpty()) continue;
            for (auto it = expanded.constBegin(); it != expanded.constEnd(); ++it) {
                if (pass.uniforms.contains(it.key())) {
                    pass.uniforms.insert(it.key(), it.value());
                    pipelineUniforms_.insert(it.key(), it.value());
                }
            }
        }
    }

    // reference/07 palette expansion — implemented in expander.cpp
    // directly (small, single caller); table lives inline below.
    static QJsonObject expandPaletteVectors(int index);

    // reference/03 §5.1
    void mapInputs(const QJsonObject& effectDef, const QJsonObject& passDef, const QJsonObject& step,
                    const QJsonObject& stepArgs, const QString& nodeId, const QJsonObject& plan, int passIndex,
                    const QVector<PassKeyOrder>& keyOrders, ExpandedPass& pass) {
        const QJsonObject inputs = passDef.value(QStringLiteral("inputs")).toObject();
        if (inputs.isEmpty()) return;
        QStringList order = (passIndex < keyOrders.size() && keyOrders.at(passIndex).inputs.size() == inputs.size())
                                 ? keyOrders.at(passIndex).inputs
                                 : inputs.keys();

        for (const QString& uniformName : order) {
            const QJsonValue texRefVal = inputs.value(uniformName);
            if (!texRefVal.isString()) continue;
            const QString texRef = texRefVal.toString();

            bool intPrefixOk = false;
            if (texRef.size() >= 2 && texRef.at(0) == QLatin1Char('o')) {
                const QChar c1 = texRef.at(1);
                intPrefixOk = c1 >= QLatin1Char('0') && c1 <= QLatin1Char('9');
            }
            const bool isPipelineInput = texRef == QStringLiteral("inputTex") || intPrefixOk;

            QString resolved;
            if (isPipelineInput) {
                resolved = !currentInput_.isEmpty() ? currentInput_ : texRef;
            } else if (texRef == QStringLiteral("inputTex3d")) {
                resolved = !currentInput3d_.isEmpty() ? currentInput3d_ : texRef;
            } else if (texRef == QStringLiteral("inputGeo")) {
                resolved = !currentInputGeo_.isEmpty() ? currentInputGeo_ : texRef;
            } else if (texRef == QStringLiteral("inputXyz")) {
                resolved = !currentInputXyz_.isEmpty() ? currentInputXyz_ : texRef;
            } else if (texRef == QStringLiteral("inputVel")) {
                resolved = !currentInputVel_.isEmpty() ? currentInputVel_ : texRef;
            } else if (texRef == QStringLiteral("inputRgba")) {
                resolved = !currentInputRgba_.isEmpty() ? currentInputRgba_ : texRef;
            } else if (texRef == QStringLiteral("noise")) {
                resolved = QStringLiteral("global_noise");
            } else if (texRef == QStringLiteral("midiNoteGrid")) {
                resolved = QStringLiteral("midiNoteGrid");
            } else if (texRef == QStringLiteral("feedback") || texRef == QStringLiteral("selfTex")) {
                const QJsonValue writeVal = plan.value(QStringLiteral("write"));
                if (writeVal.isObject()) {
                    const QJsonObject w = writeVal.toObject();
                    const QString outName = w.value(QStringLiteral("name")).toString();
                    const QString outKind = w.contains(QStringLiteral("kind")) && w.value(QStringLiteral("kind")).isString()
                                                 ? w.value(QStringLiteral("kind")).toString()
                                                 : QStringLiteral("output");
                    const QString prefix = outKind == QStringLiteral("feedback") ? QStringLiteral("feedback") : QStringLiteral("global");
                    resolved = prefix + QLatin1Char('_') + outName;
                } else {
                    resolved = !currentInput_.isEmpty() ? currentInput_ : QStringLiteral("global_inputTex");
                }
            } else if (effectDef.value(QStringLiteral("externalTexture")).isString()
                       && texRef == effectDef.value(QStringLiteral("externalTexture")).toString()) {
                resolved = texRef + QStringLiteral("_step_") + QString::number(step.value(QStringLiteral("temp")).toInt());
            } else if (stepArgs.contains(texRef)) {
                const QJsonValue arg = stepArgs.value(texRef);
                if (arg.isNull() || arg.isUndefined()) {
                    continue; // intentionally unbound
                }
                if (arg.isObject()) {
                    const QJsonObject a = arg.toObject();
                    const QString kind = a.value(QStringLiteral("kind")).toString();
                    if (kind == QStringLiteral("temp")) {
                        resolved = textureMap_.value(nodeIdOf(a.value(QStringLiteral("index")).toInt()) + QStringLiteral("_out"));
                    } else if (kind == QStringLiteral("pipeline")
                               && (a.value(QStringLiteral("name")).toString() == QStringLiteral("inputTex")
                                   || a.value(QStringLiteral("name")).toString() == QStringLiteral("inputColor"))) {
                        resolved = !currentInput_.isEmpty() ? currentInput_ : a.value(QStringLiteral("name")).toString();
                    } else if (kind == QStringLiteral("output") || kind == QStringLiteral("source") || kind == QStringLiteral("vol")
                               || kind == QStringLiteral("geo") || kind == QStringLiteral("xyz") || kind == QStringLiteral("vel")
                               || kind == QStringLiteral("rgba")) {
                        const QString name = a.value(QStringLiteral("name")).toString();
                        resolved = name == QStringLiteral("none") ? QStringLiteral("none") : (QStringLiteral("global_") + name);
                    }
                } else if (arg.isString()) {
                    resolved = resolveGlobalSurfaceRef(arg.toString());
                }
            } else if (effectDef.value(QStringLiteral("globals")).toObject().value(texRef).toObject().contains(QStringLiteral("default"))
                       && !isMissing(effectDef.value(QStringLiteral("globals")).toObject().value(texRef).toObject().value(QStringLiteral("default")))) {
                const QJsonValue defaultValJson = effectDef.value(QStringLiteral("globals")).toObject().value(texRef).toObject().value(QStringLiteral("default"));
                if (defaultValJson.isString()) {
                    const QString defaultVal = defaultValJson.toString();
                    if (defaultVal == QStringLiteral("none")) {
                        resolved = QStringLiteral("none");
                    } else if (defaultVal == QStringLiteral("inputTex") || defaultVal == QStringLiteral("inputColor")) {
                        resolved = !currentInput_.isEmpty() ? currentInput_ : defaultVal;
                    } else if (isSurfaceRef(defaultVal)) {
                        resolved = QStringLiteral("global_") + defaultVal;
                    } else if (defaultVal.startsWith(QStringLiteral("global_"))) {
                        resolved = scopeChainTex(defaultVal);
                    } else {
                        resolved = defaultVal;
                    }
                } else {
                    // non-string default for a texRef key with no other match: reference
                    // falls through to the generic branches below since GlobalHasDefault
                    // (JS) only matches STRING defaults implicitly via the branches that
                    // follow (`defaultVal === 'none'` etc. all assume a string). A numeric
                    // default here is not a valid surface ref; fall through to node-local.
                    resolved = nodeId + QLatin1Char('_') + texRef;
                }
            } else if (texRef.startsWith(QStringLiteral("global_"))) {
                resolved = scopeChainTex(texRef);
            } else if (texRef == QStringLiteral("outputTex")) {
                resolved = nodeId + QStringLiteral("_out");
            } else {
                resolved = nodeId + QLatin1Char('_') + texRef;
            }

            pass.inputs.append({uniformName, resolved});
        }
    }

    static bool isSurfaceRef(const QString& s) {
        static const QRegularExpression re(QStringLiteral("^(?:o|vol|geo|xyz|vel|rgba)[0-7]$"));
        return re.match(s).hasMatch();
    }

    static QString resolveGlobalSurfaceRef(const QString& name) {
        if (name == QStringLiteral("none")) return name;
        if (name.startsWith(QStringLiteral("global_"))) return name;
        if (isSurfaceRef(name)) return QStringLiteral("global_") + name;
        return name;
    }

    // reference/03 §5.2 + §5.3 last-pass-to-surface optimization.
    void mapOutputs(const QJsonObject& passDef, const QString& nodeId, const QJsonObject& plan, int passIndex,
                     int passCount, int stepPos, const QVector<PassKeyOrder>& keyOrders, ExpandedPass& pass) {
        const QJsonObject outputs = passDef.value(QStringLiteral("outputs")).toObject();
        if (outputs.isEmpty()) return;
        QStringList order = (passIndex < keyOrders.size() && keyOrders.at(passIndex).outputs.size() == outputs.size())
                                 ? keyOrders.at(passIndex).outputs
                                 : outputs.keys();

        const QJsonArray chain = plan.value(QStringLiteral("chain")).toArray();
        const bool isLastStep = stepPos == chain.size() - 1;
        const bool isLastPass = passIndex == passCount - 1;
        const QJsonValue writeVal = plan.value(QStringLiteral("write"));

        for (const QString& attachment : order) {
            const QJsonValue texRefVal = outputs.value(attachment);
            if (!texRefVal.isString()) continue;
            const QString texRef = texRefVal.toString();
            QString virtualTex;

            if (texRef == QStringLiteral("outputTex")) {
                if (isLastStep && isLastPass && writeVal.isObject()) {
                    const QJsonObject w = writeVal.toObject();
                    const QString outName = w.value(QStringLiteral("name")).toString();
                    const QString outKind = w.contains(QStringLiteral("kind")) && w.value(QStringLiteral("kind")).isString()
                                                 ? w.value(QStringLiteral("kind")).toString()
                                                 : QStringLiteral("output");
                    const QString prefix = outKind == QStringLiteral("feedback") ? QStringLiteral("feedback") : QStringLiteral("global");
                    virtualTex = prefix + QLatin1Char('_') + outName;
                    lastWrittenSurface_ = outName;
                } else {
                    virtualTex = nodeId + QStringLiteral("_out");
                }
                textureMap_.insert(virtualTex, virtualTex);
                textureMap_.insert(nodeId + QStringLiteral("_out"), virtualTex);
            } else if (texRef == QStringLiteral("outputTex3d")) {
                virtualTex = nodeId + QStringLiteral("_out3d");
                textureMap_.insert(nodeId + QStringLiteral("_out3d"), virtualTex);
            } else if (texRef == QStringLiteral("outputXyz")) {
                virtualTex = nodeId + QStringLiteral("_outXyz");
                textureMap_.insert(nodeId + QStringLiteral("_outXyz"), virtualTex);
            } else if (texRef == QStringLiteral("outputVel")) {
                virtualTex = nodeId + QStringLiteral("_outVel");
                textureMap_.insert(nodeId + QStringLiteral("_outVel"), virtualTex);
            } else if (texRef == QStringLiteral("outputRgba")) {
                virtualTex = nodeId + QStringLiteral("_outRgba");
                textureMap_.insert(nodeId + QStringLiteral("_outRgba"), virtualTex);
            } else if (texRef == QStringLiteral("inputTex3d")) {
                virtualTex = !currentInput3d_.isEmpty() ? currentInput3d_ : (nodeId + QStringLiteral("_inputTex3d"));
            } else if (texRef == QStringLiteral("inputGeo")) {
                virtualTex = !currentInputGeo_.isEmpty() ? currentInputGeo_ : (nodeId + QStringLiteral("_inputGeo"));
            } else if (texRef == QStringLiteral("inputXyz")) {
                virtualTex = !currentInputXyz_.isEmpty() ? currentInputXyz_ : (nodeId + QStringLiteral("_inputXyz"));
            } else if (texRef == QStringLiteral("inputVel")) {
                virtualTex = !currentInputVel_.isEmpty() ? currentInputVel_ : (nodeId + QStringLiteral("_inputVel"));
            } else if (texRef == QStringLiteral("inputRgba")) {
                virtualTex = !currentInputRgba_.isEmpty() ? currentInputRgba_ : (nodeId + QStringLiteral("_inputRgba"));
            } else if (texRef.startsWith(QStringLiteral("global_"))) {
                virtualTex = scopeChainTex(texRef);
            } else if (texRef.startsWith(QStringLiteral("feedback_"))) {
                virtualTex = texRef;
            } else {
                virtualTex = nodeId + QLatin1Char('_') + texRef;
            }

            pass.outputs.append({attachment, virtualTex});
        }
    }
};

// ---------------------------------------------------------------------
// Palette expansion (reference/03 §7, shaders/src/runtime/
// palette-expansion.js — read in full, table transcribed verbatim
// including the non-round floats per that file's own §7 hazard warning).
// Legacy classicNoisedeck support only; a `type:'palette'` global holds a
// 1-based index. mode: 0=none,1=hsv,2=oklab,3=rgb (already in
// classicNoisedeck convention, NOT the same numbering as the modern
// filter/palette 0=rgb/1=hsv/2=oklab — see palette-expansion.js header).
// ---------------------------------------------------------------------

struct PaletteEntry {
    double amp[3];
    double freq[3];
    double offset[3];
    double phase[3];
    int mode;
};

const PaletteEntry kPalettes[55] = {
    /* 1  seventiesShirt */ {{0.76, 0.88, 0.37}, {1, 1, 1}, {0.93, 0.97, 0.52}, {0.21, 0.41, 0.56}, 3},
    /* 2  fiveG          */ {{0.56851584, 0.7740668, 0.23485267}, {1, 1, 1}, {0.5, 0.5, 0.5}, {0.727029, 0.08039695, 0.10427457}, 3},
    /* 3  afterimage     */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.5, 0.5, 0.5}, {0.3, 0.2, 0.2}, 3},
    /* 4  barstow        */ {{0.45, 0.2, 0.1}, {1, 1, 1}, {0.7, 0.2, 0.2}, {0.5, 0.4, 0.0}, 3},
    /* 5  bloob          */ {{0.09, 0.59, 0.48}, {1, 1, 1}, {0.2, 0.31, 0.98}, {0.88, 0.4, 0.33}, 3},
    /* 6  blueSkies      */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.1, 0.4, 0.7}, {0.1, 0.1, 0.1}, 3},
    /* 7  brushedMetal   */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.5, 0.5, 0.5}, {0.0, 0.1, 0.2}, 3},
    /* 8  burningSky     */ {{0.7259015, 0.7004237, 0.9494409}, {1, 1, 1}, {0.63290054, 0.37883538, 0.29405284}, {0.0, 0.1, 0.2}, 3},
    /* 9  california     */ {{0.94, 0.33, 0.27}, {1, 1, 1}, {0.74, 0.37, 0.73}, {0.44, 0.17, 0.88}, 3},
    /* 10 columbia       */ {{1.0, 0.7, 1.0}, {1, 1, 1}, {1.0, 0.4, 0.9}, {0.4, 0.5, 0.6}, 3},
    /* 11 cottonCandy    */ {{0.51, 0.39, 0.41}, {1, 1, 1}, {0.59, 0.53, 0.94}, {0.15, 0.41, 0.46}, 3},
    /* 12 darkSatin      */ {{0.0, 0.0, 0.51}, {1, 1, 1}, {0.0, 0.0, 0.43}, {0.0, 0.0, 0.36}, 1},
    /* 13 dealerHat      */ {{0.83, 0.45, 0.19}, {1, 1, 1}, {0.79, 0.45, 0.35}, {0.28, 0.91, 0.61}, 3},
    /* 14 dreamy         */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.5, 0.5, 0.5}, {0.0, 0.2, 0.25}, 3},
    /* 15 eventHorizon   */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.22, 0.48, 0.62}, {0.1, 0.3, 0.2}, 3},
    /* 16 ghostly        */ {{0.02, 0.92, 0.76}, {1, 1, 1}, {0.51, 0.49, 0.51}, {0.71, 0.23, 0.66}, 1},
    /* 17 grayscale      */ {{0.5, 0.5, 0.5}, {2, 2, 2}, {0.5, 0.5, 0.5}, {1.0, 1.0, 1.0}, 3},
    /* 18 hazySunset     */ {{0.79, 0.56, 0.22}, {1, 1, 1}, {0.96, 0.5, 0.49}, {0.15, 0.98, 0.87}, 3},
    /* 19 heatmap        */ {{0.75804377, 0.62868536, 0.2227562}, {1, 1, 1}, {0.35536355, 0.12935615, 0.17060602}, {0.0, 0.25, 0.5}, 3},
    /* 20 hypercolor     */ {{0.79, 0.5, 0.23}, {1, 1, 1}, {0.75, 0.47, 0.45}, {0.08, 0.84, 0.16}, 3},
    /* 21 jester         */ {{0.7, 0.81, 0.73}, {1, 1, 1}, {0.1, 0.22, 0.27}, {0.99, 0.12, 0.94}, 3},
    /* 22 justBlue       */ {{0.5, 0.5, 0.5}, {0.0, 0.0, 1.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    /* 23 justCyan       */ {{0.5, 0.5, 0.5}, {0.0, 1.0, 1.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    /* 24 justGreen      */ {{0.5, 0.5, 0.5}, {0.0, 1.0, 0.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    /* 25 justPurple     */ {{0.5, 0.5, 0.5}, {1.0, 0.0, 1.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    /* 26 justRed        */ {{0.5, 0.5, 0.5}, {1.0, 0.0, 0.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    /* 27 justYellow     */ {{0.5, 0.5, 0.5}, {1.0, 1.0, 0.0}, {0.5, 0.5, 0.5}, {0.5, 0.5, 0.5}, 3},
    /* 28 mars           */ {{0.74, 0.33, 0.09}, {1, 1, 1}, {0.62, 0.2, 0.2}, {0.2, 0.1, 0.0}, 3},
    /* 29 modesto        */ {{0.56, 0.68, 0.39}, {1, 1, 1}, {0.72, 0.07, 0.62}, {0.25, 0.4, 0.41}, 3},
    /* 30 moss           */ {{0.78, 0.39, 0.07}, {1, 1, 1}, {0.0, 0.53, 0.33}, {0.94, 0.92, 0.9}, 3},
    /* 31 neptune        */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.2, 0.64, 0.62}, {0.15, 0.2, 0.3}, 3},
    /* 32 netOfGems      */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.64, 0.12, 0.84}, {0.1, 0.25, 0.15}, 3},
    /* 33 organic        */ {{0.42, 0.42, 0.04}, {1, 1, 1}, {0.47, 0.27, 0.27}, {0.41, 0.14, 0.11}, 3},
    /* 34 papaya         */ {{0.65, 0.4, 0.11}, {1, 1, 1}, {0.72, 0.45, 0.08}, {0.71, 0.8, 0.84}, 3},
    /* 35 radioactive    */ {{0.62, 0.79, 0.11}, {1, 1, 1}, {0.22, 0.56, 0.17}, {0.15, 0.1, 0.25}, 3},
    /* 36 royal          */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.41, 0.22, 0.67}, {0.2, 0.25, 0.2}, 3},
    /* 37 santaCruz      */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.5, 0.5, 0.5}, {0.25, 0.5, 0.75}, 3},
    /* 38 sherbet        */ {{0.6059281, 0.17591387, 0.17166573}, {1, 1, 1}, {0.5224456, 0.3864609, 0.36020845}, {0.0, 0.25, 0.5}, 3},
    /* 39 sherbetDouble  */ {{0.6059281, 0.17591387, 0.17166573}, {2, 2, 2}, {0.5224456, 0.3864609, 0.36020845}, {0.0, 0.25, 0.5}, 3},
    /* 40 silvermane     */ {{0.42, 0.0, 0.0}, {2, 2, 2}, {0.45, 0.5, 0.42}, {0.63, 1.0, 1.0}, 2},
    /* 41 skykissed      */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.83, 0.6, 0.63}, {0.3, 0.1, 0.0}, 3},
    /* 42 solaris        */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.6, 0.4, 0.1}, {0.3, 0.2, 0.1}, 3},
    /* 43 spooky         */ {{0.46, 0.73, 0.19}, {1, 1, 1}, {0.27, 0.79, 0.78}, {0.27, 0.16, 0.04}, 2},
    /* 44 springtime     */ {{0.67, 0.25, 0.27}, {1, 1, 1}, {0.74, 0.48, 0.46}, {0.07, 0.79, 0.39}, 3},
    /* 45 sproingtime    */ {{0.9, 0.43, 0.34}, {1, 1, 1}, {0.56, 0.69, 0.32}, {0.03, 0.8, 0.4}, 3},
    /* 46 sulphur        */ {{0.73, 0.36, 0.52}, {1, 1, 1}, {0.78, 0.68, 0.15}, {0.74, 0.93, 0.28}, 3},
    /* 47 summoning      */ {{1.0, 0.0, 0.8}, {1, 1, 1}, {0.0, 0.0, 0.0}, {0.0, 0.5, 0.1}, 3},
    /* 48 superhero      */ {{1.0, 0.25, 0.5}, {0.5, 0.5, 0.5}, {0.0, 0.0, 0.25}, {0.5, 0.0, 0.0}, 3},
    /* 49 toxic          */ {{0.5, 0.5, 0.5}, {1, 1, 1}, {0.26, 0.57, 0.03}, {0.0, 0.1, 0.3}, 3},
    /* 50 tropicalia     */ {{0.28, 0.08, 0.65}, {1, 1, 1}, {0.48, 0.6, 0.03}, {0.1, 0.15, 0.3}, 2},
    /* 51 tungsten       */ {{0.65, 0.93, 0.73}, {1, 1, 1}, {0.31, 0.21, 0.27}, {0.43, 0.45, 0.48}, 3},
    /* 52 vaporwave      */ {{0.9, 0.76, 0.63}, {1, 1, 1}, {0.0, 0.19, 0.68}, {0.43, 0.23, 0.32}, 3},
    /* 53 vibrant        */ {{0.78, 0.63, 0.68}, {1, 1, 1}, {0.41, 0.03, 0.16}, {0.81, 0.61, 0.06}, 3},
    /* 54 vintage        */ {{0.97, 0.74, 0.23}, {1, 1, 1}, {0.97, 0.38, 0.35}, {0.34, 0.41, 0.44}, 3},
    /* 55 vintagePhoto   */ {{0.68, 0.79, 0.57}, {1, 1, 1}, {0.56, 0.35, 0.14}, {0.73, 0.9, 0.99}, 3},
};

QJsonObject Expander::expandPaletteVectors(int index) {
    if (index <= 0 || index > 55) return QJsonObject();
    const PaletteEntry& e = kPalettes[index - 1];
    auto vec3 = [](const double v[3]) {
        QJsonArray a;
        a.append(v[0]);
        a.append(v[1]);
        a.append(v[2]);
        return a;
    };
    QJsonObject out;
    out.insert(QStringLiteral("paletteOffset"), vec3(e.offset));
    out.insert(QStringLiteral("paletteAmp"), vec3(e.amp));
    out.insert(QStringLiteral("paletteFreq"), vec3(e.freq));
    out.insert(QStringLiteral("palettePhase"), vec3(e.phase));
    out.insert(QStringLiteral("paletteMode"), e.mode);
    return out;
}

} // namespace

ExpandResult expand(const QJsonObject& validated, EffectRegistry& registry) {
    Expander expander(registry);
    const QJsonArray plans = validated.value(QStringLiteral("plans")).toArray();
    const QJsonValue render = validated.value(QStringLiteral("render"));
    return expander.run(plans, render);
}

QJsonObject toRawPassJson(const ExpandedPass& pass) {
    QJsonObject out;
    out.insert(QStringLiteral("id"), pass.id);
    out.insert(QStringLiteral("program"), pass.program);

    if (pass.isBlit) {
        out.insert(QStringLiteral("type"), QStringLiteral("render"));
    } else {
        if (!pass.entryPoint.isUndefined()) out.insert(QStringLiteral("entryPoint"), pass.entryPoint);
        if (!pass.drawMode.isUndefined()) out.insert(QStringLiteral("drawMode"), pass.drawMode);
        if (!pass.drawBuffers.isUndefined()) out.insert(QStringLiteral("drawBuffers"), pass.drawBuffers);
        if (!pass.count.isUndefined()) out.insert(QStringLiteral("count"), pass.count);
        if (!pass.countUniform.isUndefined()) out.insert(QStringLiteral("countUniform"), pass.countUniform);
        if (!pass.repeat.isUndefined()) out.insert(QStringLiteral("repeat"), pass.repeat);
        if (!pass.blend.isUndefined()) out.insert(QStringLiteral("blend"), pass.blend);
        if (!pass.workgroups.isUndefined()) out.insert(QStringLiteral("workgroups"), pass.workgroups);
        if (!pass.storageBuffers.isUndefined()) out.insert(QStringLiteral("storageBuffers"), pass.storageBuffers);
        if (!pass.storageTextures.isUndefined()) out.insert(QStringLiteral("storageTextures"), pass.storageTextures);
    }

    QJsonObject inputs;
    for (const auto& kv : pass.inputs) inputs.insert(kv.first, kv.second);
    out.insert(QStringLiteral("inputs"), inputs);
    QJsonObject outputs;
    for (const auto& kv : pass.outputs) outputs.insert(kv.first, kv.second);
    out.insert(QStringLiteral("outputs"), outputs);
    out.insert(QStringLiteral("uniforms"), pass.uniforms);

    if (pass.isBlit) {
        if (!pass.nodeId.isUndefined()) out.insert(QStringLiteral("nodeId"), pass.nodeId);
        if (!pass.stepIndex.isUndefined()) out.insert(QStringLiteral("stepIndex"), pass.stepIndex);
    } else {
        out.insert(QStringLiteral("effectKey"), pass.effectKey);
        out.insert(QStringLiteral("effectFunc"), pass.effectFunc);
        out.insert(QStringLiteral("effectNamespace"), pass.effectNamespace);
        out.insert(QStringLiteral("nodeId"), pass.nodeId);
        out.insert(QStringLiteral("stepIndex"), pass.stepIndex);
        if (pass.inheritsVolumeSize) out.insert(QStringLiteral("inheritsVolumeSize"), true);
        if (pass.hasUniformSpecs) out.insert(QStringLiteral("uniformSpecs"), pass.uniformSpecs);
        if (!pass.scopedParams.isEmpty()) out.insert(QStringLiteral("scopedParams"), pass.scopedParams);
    }
    return out;
}

} // namespace nm
