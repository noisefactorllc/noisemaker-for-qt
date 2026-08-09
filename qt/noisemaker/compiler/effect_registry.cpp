#include "effect_registry.h"

#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLatin1Char>

namespace nm {

namespace {

// An effect is a STARTER (valid as a chain root, needs no pipeline input)
// iff none of its passes consumes one of these upstream-surface inputs.
// This mirrors tools/export-graph.mjs / tools/dump-validate.mjs -- the
// rule the REAL graph-producing path uses -- NOT the per-effect `starter`
// JSON field (e.g. mixer.channelCombine has starter:false in its JSON but
// takes its inputs via surface kwargs, so it IS a starter and renders;
// verified empirically: derived-vs-baked starter status disagrees on
// exactly this one effect across the whole 210-effect catalog).
const QSet<QString>& starterInputSentinels() {
    static const QSet<QString> s = {
        QStringLiteral("inputTex"), QStringLiteral("inputTex3d"), QStringLiteral("src"),
        QStringLiteral("o0"), QStringLiteral("o1"),
    };
    return s;
}

bool isAlpha(QChar c) {
    return (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'));
}
bool isDigitChar(QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); }
bool isWs(QChar c) {
    return c == QLatin1Char(' ') || c == QLatin1Char('\t') || c == QLatin1Char('\n') || c == QLatin1Char('\r');
}

bool isValidIdentifier(const QString& name) {
    if (name.isEmpty()) return false;
    if (!(isAlpha(name.at(0)) || name.at(0) == QLatin1Char('_'))) return false;
    for (int i = 1; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (!(isAlpha(c) || isDigitChar(c) || c == QLatin1Char('_'))) return false;
    }
    return true;
}

// Verbatim port of renderer/canvas.js sanitizeEnumName: "Cell Scale" ->
// "CellScale" (uppercase the char after each whitespace run, drop the
// spaces), then strip remaining non-identifier chars; empty string if the
// result is not a valid identifier (reference returns null). Cross-checked
// against godot/addons/noisemaker/compiler/lang/effect_registry.gd's
// char-by-char transcription of the same reference regexes. Confirmed
// (dump-registry.mjs header comment) that this never actually changes a
// name across the current 210-effect catalog -- ported for completeness.
QString sanitizeEnumName(const QString& name) {
    QString result;
    int i = 0;
    const int n = name.size();
    while (i < n) {
        if (isWs(name.at(i))) {
            while (i < n && isWs(name.at(i))) ++i;
            if (i < n) {
                result += name.at(i).toUpper();
                ++i;
            }
        } else {
            result += name.at(i);
            ++i;
        }
    }
    QString stripped;
    for (const QChar c : result) {
        if (isAlpha(c) || isDigitChar(c) || c == QLatin1Char('_')) stripped += c;
    }
    return isValidIdentifier(stripped) ? stripped : QString();
}

// Recovers the DECLARATION ORDER of a top-level JSON object's direct keys
// from raw file text, given the object's own key name (e.g. "globals").
//
// QJsonObject itself does NOT preserve parse order: Qt 6's QJsonObject is
// backed by a QCborMap, which keeps entries key-SORTED (verified directly
// -- parsing {"zebra":1,"apple":2,"mango":3,"banana":4} and iterating the
// resulting QJsonObject yields apple/banana/mango/zebra, alphabetical, not
// the source order). The reference's `Object.entries(instance.globals)`
// preserves JS declaration order, which is PARITY-CRITICAL here: `args`
// entries are matched positionally against a call's positional arguments
// (reference/02 SS6: `call.args[i]` <-> `spec.args[i]`), and some effects'
// enum values are literally the POSITIONAL INDEX of their `globals` key
// (palette-style choices; T2's convert-definitions.mjs flags this same
// hazard verbatim). This walker recovers the true order directly from the
// source bytes instead. Quote/escape-aware (handles `\"` inside string
// values) and correctly skips NESTED object/array values without
// mistaking their own keys for direct children of the target object.
// Returns an empty list if `key` is not found as a top-level object value.
QStringList objectKeyOrder(const QString& text, const QString& key) {
    QStringList keys;
    const QString marker = QStringLiteral("\"%1\"").arg(key);
    int markerIdx = text.indexOf(marker);
    if (markerIdx < 0) return keys;
    int openBrace = text.indexOf(QLatin1Char('{'), markerIdx + marker.size());
    // Reject if something other than optional whitespace/':' sits between
    // the key and the '{' (defensive; the generated files are always
    // "key": { ... } with no intervening content).
    for (int i = markerIdx + marker.size(); i < openBrace; ++i) {
        const QChar c = text.at(i);
        if (c != QLatin1Char(':') && !c.isSpace()) return keys;
    }
    if (openBrace < 0) return keys;

    int i = openBrace + 1;
    int depth = 1;
    const int n = text.size();
    while (i < n && depth > 0) {
        const QChar c = text.at(i);
        if (c == QLatin1Char('"')) {
            const int stringStart = i;
            ++i;
            while (i < n) {
                if (text.at(i) == QLatin1Char('\\')) {
                    i += 2;
                    continue;
                }
                if (text.at(i) == QLatin1Char('"')) {
                    ++i;
                    break;
                }
                ++i;
            }
            if (depth == 1) {
                int j = i;
                while (j < n && text.at(j).isSpace()) ++j;
                if (j < n && text.at(j) == QLatin1Char(':')) {
                    keys.append(text.mid(stringStart + 1, i - stringStart - 2));
                }
            }
            continue;
        }
        if (c == QLatin1Char('{') || c == QLatin1Char('[')) {
            ++depth;
            ++i;
            continue;
        }
        if (c == QLatin1Char('}') || c == QLatin1Char(']')) {
            --depth;
            ++i;
            continue;
        }
        ++i;
    }
    return keys;
}

} // namespace

QString EffectRegistry::defaultDataRoot() {
    QDir d(QStringLiteral("qt/noisemaker"));
    if (d.exists(QStringLiteral("effects"))) return d.absolutePath();
    return QStringLiteral("qt/noisemaker");
}

void EffectRegistry::loadAll(const QString& dataRoot) {
    QDir effectsDir(dataRoot + QStringLiteral("/effects"));
    if (!effectsDir.exists()) return;

    // gather "<ns>/<file>.json", then sort -- matches dump-registry.mjs's
    // `rels.sort()` (a single alphabetical sort over the full relative
    // path, which is also what breaks the "noise" bare-func collision
    // between classicNoisedeck/noise.json and synth/noise.json in the
    // SAME order the oracle resolves it: synth sorts after
    // classicNoisedeck, so it registers second and wins the bare key).
    QStringList relFiles;
    const QStringList namespaces = effectsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString& ns : namespaces) {
        QDir nsDir(effectsDir.filePath(ns));
        const QStringList jsonFiles = nsDir.entryList(QStringList{QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& f : jsonFiles) {
            relFiles.append(ns + QLatin1Char('/') + f);
        }
    }
    relFiles.sort();

    for (const QString& rel : relFiles) {
        QFile file(effectsDir.filePath(rel));
        if (!file.open(QIODevice::ReadOnly)) continue;
        registerEffect(file.readAll());
    }
}

// Mirror of renderer/canvas.js registerEffectWithRuntime() (and
// dump-registry.mjs/dump-validate.mjs's inlined re-implementation of it).
void EffectRegistry::registerEffect(const QByteArray& rawJson) {
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(rawJson, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return;
    const QJsonObject def = doc.object();

    const QString ns = def.value(QStringLiteral("namespace")).toString();
    const QString func = def.value(QStringLiteral("func")).toString();
    if (func.isEmpty()) return;

    // Multi-key effect registration. The reference's four keys are
    // func | ns.func | ns/func | ns.func (the 4th literally duplicates the
    // 2nd, since effectName===func for the whole catalog) -- three
    // DISTINCT keys land here.
    effects_.insert(func, def);
    if (!ns.isEmpty()) {
        effects_.insert(ns + QLatin1Char('.') + func, def);
        effects_.insert(ns + QLatin1Char('/') + func, def);
    }

    const QString opName = ns.isEmpty() ? func : (ns + QLatin1Char('.') + func);

    const QJsonObject globals = def.value(QStringLiteral("globals")).toObject();
    QStringList orderedKeys = objectKeyOrder(QString::fromUtf8(rawJson), QStringLiteral("globals"));
    if (orderedKeys.size() != globals.size()) {
        // Defensive fallback (should not happen for a well-formed generated
        // definition -- PORTING-GUIDE.md rule 2). An alphabetical order is
        // still a VALID args list, just not guaranteed positionally
        // faithful -- better than silently dropping/duplicating args.
        orderedKeys = globals.keys();
    }

    OpSpec spec;
    spec.name = func;
    spec.opName = opName;

    for (const QString& key : orderedKeys) {
        const QJsonValue specVal = globals.value(key);
        if (!specVal.isObject()) continue;
        const QJsonObject gspec = specVal.toObject();

        const QString gtype = gspec.value(QStringLiteral("type")).toString();

        // enumPath = spec.enum || spec.enumPath (falsy-OR: an explicitly
        // empty-string `enum` ALSO falls through to `enumPath`).
        QString enumPath;
        const QJsonValue enumVal = gspec.value(QStringLiteral("enum"));
        if (enumVal.isString() && !enumVal.toString().isEmpty()) {
            enumPath = enumVal.toString();
        } else {
            const QJsonValue enumPathVal = gspec.value(QStringLiteral("enumPath"));
            if (enumPathVal.isString() && !enumPathVal.toString().isEmpty()) enumPath = enumPathVal.toString();
        }

        const QJsonValue choicesVal = gspec.value(QStringLiteral("choices"));
        const bool hasChoicesField = choicesVal.isObject();

        // choices with no explicit enum: synthesize an enum path and
        // register the choices as project enums (canvas.js parity).
        if (enumPath.isEmpty() && hasChoicesField) {
            enumPath = opName + QLatin1Char('.') + key;
            const QJsonObject choices = choicesVal.toObject();
            for (auto cit = choices.constBegin(); cit != choices.constEnd(); ++cit) {
                const QString cname = cit.key();
                if (cname.endsWith(QLatin1Char(':'))) continue; // UI group header, not a value
                const QJsonValue cval = cit.value();
                // NOTE: the reference blindly wraps WHATEVER a choices entry's
                // value is into {type:'Number', value: val} with no type
                // check -- filter.text's `font`/`justify` globals are
                // type:"string" with STRING-valued choices
                // ({"nunito":"Nunito", ...}) and still register as
                // {"type":"Number","value":"Nunito"} (verified against the
                // live oracle). Ported bug-for-bug: register whatever the
                // value is, not just numbers.
                enums_.registerChoice({ns, func, key, cname}, cval);
                const QString sanitized = sanitizeEnumName(cname);
                if (!sanitized.isEmpty() && sanitized != cname) {
                    enums_.registerChoice({ns, func, key, sanitized}, cval);
                }
            }
        }

        // default/min/max/uniform are BLIND passthroughs in the reference
        // (dump-registry.mjs: `default: spec.default, min: spec.min,
        // max: spec.max, uniform: spec.uniform` -- no type filtering at
        // all). min/max are usually numbers but can be a 3-element array
        // for a vec3 param's per-component bounds (render.renderLit3d
        // cameraPosition min:[-1,-1,-1], verified against the oracle) --
        // ParamDef stores whatever is there; QJsonObject::value() already
        // yields Undefined for a genuinely absent key, so no extra "is
        // this the right type" gating belongs here.
        ParamDef pd;
        pd.name = key;
        pd.type = (gtype == QStringLiteral("vec4")) ? QStringLiteral("color") : gtype;
        pd.defaultValue = gspec.value(QStringLiteral("default"));
        if (!enumPath.isEmpty()) pd.enumPath = enumPath;
        pd.minValue = gspec.value(QStringLiteral("min"));
        pd.maxValue = gspec.value(QStringLiteral("max"));
        pd.uniformValue = gspec.value(QStringLiteral("uniform"));
        // Verbatim (incl. ':' group headers) -- the ops-dump consumer; NOT
        // the same filtering as the enum-registration loop above.
        if (hasChoicesField) pd.choicesValue = choicesVal;
        const QJsonValue defaultFromVal = gspec.value(QStringLiteral("defaultFrom"));
        if (defaultFromVal.isString()) pd.defaultFromValue = defaultFromVal;

        spec.args.append(pd);
    }

    ops_.insert(opName, spec);

    if (isStarterDef(def)) {
        starterOps_.insert(opName);
    }

    const QJsonValue paramAliasesVal = def.value(QStringLiteral("paramAliases"));
    if (paramAliasesVal.isObject() && !paramAliasesVal.toObject().isEmpty()) {
        paramAliases_.insert(opName, paramAliasesVal.toObject());
    }

    if (def.value(QStringLiteral("hidden")).toBool(false)) {
        const QJsonValue deprecatedBy = def.value(QStringLiteral("deprecatedBy"));
        if (deprecatedBy.isString()) {
            effectAliases_.insert(opName, deprecatedBy.toString());
        }
    }

    // define-map: globals carrying `define` -> {globalKey: DEFINE_NAME}
    // (Expander/orchestrator concern; not part of any T9 gate -- order is
    // irrelevant, this is a lookup map, not a compared array).
    QJsonObject defs;
    for (auto git = globals.constBegin(); git != globals.constEnd(); ++git) {
        if (!git.value().isObject()) continue;
        const QJsonValue defineVal = git.value().toObject().value(QStringLiteral("define"));
        if (defineVal.isString()) defs.insert(git.key(), defineVal);
    }
    if (!defs.isEmpty()) defineMap_.insert(opName, defs);
}

QJsonObject EffectRegistry::getEffect(const QString& key) const { return effects_.value(key); }

bool EffectRegistry::hasEffect(const QString& key) const { return effects_.contains(key); }

const OpSpec* EffectRegistry::getOp(const QString& opName) const {
    const auto it = ops_.find(opName);
    return it == ops_.end() ? nullptr : &it.value();
}

bool EffectRegistry::isStarterOp(const QString& name) const {
    // reference/02 SS9 isStarterOp.
    if (name.isEmpty()) return false;
    // Force particles to be non-starter (workaround for stale
    // manifest/cache -- reference comment, verbatim). Dead code on this
    // catalog (no `particles` effect exists), ported for fidelity.
    if (name == QStringLiteral("particles") || name == QStringLiteral("render.particles")) return false;
    if (starterOps_.contains(name)) return true;
    // For namespaced QUERY names like "nm.voronoi": if the bare canonical
    // ("voronoi") is ALSO a registered starter key, a namespaced starter
    // must not "leak" as a false positive for a DIFFERENT namespace.
    // NOTE: starterOps_ only ever holds NAMESPACED keys (see file header),
    // so `starterOps_.contains(canonical)` can never be true in practice
    // for this catalog -- this whole branch is structurally dead here,
    // ported verbatim for fidelity with the reference (and because a bare
    // canonical fallback is exactly the kind of thing a future reference
    // change could make reachable again).
    const QStringList parts = name.split(QLatin1Char('.'));
    if (parts.size() > 1) {
        const QString canonical = parts.last();
        if (starterOps_.contains(canonical)) {
            for (const QString& op : starterOps_) {
                if (op.endsWith(QStringLiteral(".") + canonical)) return false;
            }
            return true;
        }
    }
    return false;
}

QString EffectRegistry::checkEffectAlias(const QString& opName) const {
    // reference/01 SS8.4.
    const auto it = effectAliases_.find(opName);
    if (it == effectAliases_.end()) return QString();
    const QString newName = it.value();
    const int dot = opName.lastIndexOf(QLatin1Char('.'));
    const QString oldName = dot >= 0 ? opName.mid(dot + 1) : opName;
    return QStringLiteral("effect '%1' is deprecated, use '%2' instead. Aliases will be removed on 2026-09-01.")
        .arg(oldName, newName);
}

QStringList EffectRegistry::resolveParamAliases(const QString& opName, QJsonObject& kwargs) const {
    // reference/01 SS8.5 -- mutates kwargs in place; returns warnings.
    QStringList warnings;
    const auto it = paramAliases_.find(opName);
    if (it == paramAliases_.end()) return warnings;
    const QJsonObject aliases = it.value();
    for (auto ait = aliases.constBegin(); ait != aliases.constEnd(); ++ait) {
        const QString oldName = ait.key();
        if (!kwargs.contains(oldName)) continue;
        const QString newName = ait.value().toString();
        if (!kwargs.contains(newName)) {
            kwargs.insert(newName, kwargs.value(oldName));
        }
        kwargs.remove(oldName);
        warnings.append(
            QStringLiteral("param '%1' is deprecated, use '%2' instead. Aliases will be removed on 2026-09-01.")
                .arg(oldName, newName));
    }
    return warnings;
}

bool EffectRegistry::isStarterDef(const QJsonObject& def) {
    const QJsonValue passesVal = def.value(QStringLiteral("passes"));
    if (!passesVal.isArray()) return true; // no passes => starter
    const QSet<QString>& sentinels = starterInputSentinels();
    for (const QJsonValue& passVal : passesVal.toArray()) {
        if (!passVal.isObject()) continue;
        const QJsonValue inputsVal = passVal.toObject().value(QStringLiteral("inputs"));
        if (!inputsVal.isObject()) continue;
        const QJsonObject inputs = inputsVal.toObject();
        for (auto iit = inputs.constBegin(); iit != inputs.constEnd(); ++iit) {
            if (iit.value().isString() && sentinels.contains(iit.value().toString())) {
                return false;
            }
        }
    }
    return true;
}

QJsonObject EffectRegistry::opSpecToJson(const OpSpec& spec) {
    QJsonArray args;
    for (const ParamDef& pd : spec.args) {
        QJsonObject arg;
        arg.insert(QStringLiteral("name"), pd.name);
        if (!pd.type.isEmpty()) arg.insert(QStringLiteral("type"), pd.type);
        if (pd.hasDefault()) arg.insert(QStringLiteral("default"), pd.defaultValue);
        if (pd.hasEnumPath()) {
            arg.insert(QStringLiteral("enum"), pd.enumPath);
            arg.insert(QStringLiteral("enumPath"), pd.enumPath);
        }
        if (pd.hasMin()) arg.insert(QStringLiteral("min"), pd.minValue);
        if (pd.hasMax()) arg.insert(QStringLiteral("max"), pd.maxValue);
        if (pd.hasUniform()) arg.insert(QStringLiteral("uniform"), pd.uniformValue);
        if (pd.hasChoices()) arg.insert(QStringLiteral("choices"), pd.choicesValue);
        // NOTE: defaultFrom is deliberately NOT dumped -- dump-registry.mjs's
        // own op-arg shape never includes it (validator-internal only).
        args.append(arg);
    }
    QJsonObject out;
    out.insert(QStringLiteral("name"), spec.name);
    out.insert(QStringLiteral("args"), args);
    return out;
}

QJsonObject EffectRegistry::dumpSummary() const {
    QJsonObject opsOut;
    for (auto it = ops_.constBegin(); it != ops_.constEnd(); ++it) {
        opsOut.insert(it.key(), opSpecToJson(it.value()));
    }
    QJsonObject paramAliasesOut;
    for (auto it = paramAliases_.constBegin(); it != paramAliases_.constEnd(); ++it) {
        paramAliasesOut.insert(it.key(), it.value());
    }
    QJsonObject effectAliasesOut;
    for (auto it = effectAliases_.constBegin(); it != effectAliases_.constEnd(); ++it) {
        effectAliasesOut.insert(it.key(), it.value());
    }
    QJsonObject effectKeysOut;
    for (auto it = effects_.constBegin(); it != effects_.constEnd(); ++it) {
        const QJsonObject def = it.value();
        const QString ns = def.value(QStringLiteral("namespace")).toString();
        const QString func = def.value(QStringLiteral("func")).toString();
        effectKeysOut.insert(it.key(), ns + QLatin1Char('.') + func);
    }

    QJsonObject out;
    out.insert(QStringLiteral("ops"), opsOut);
    out.insert(QStringLiteral("enums"), enums_.project());
    out.insert(QStringLiteral("paramAliases"), paramAliasesOut);
    out.insert(QStringLiteral("effectAliases"), effectAliasesOut);
    out.insert(QStringLiteral("effectKeys"), effectKeysOut);
    return out;
}

} // namespace nm
