#pragma once

// effect_registry.h -- loads effect definitions and exposes the op/spec/
// starter/namespace tables the Validator (and, later, the Expander) need.
// Port of the REFERENCE load-time registration in renderer/canvas.js
// registerEffectWithRuntime() (with runtime/registry.js, lang/ops.js,
// lang/paramAliases.js, lang/effectAliases.js, lang/enums.js), cross-
// checked against td/noisemaker/compiler/lang/effect_registry.py and
// godot/addons/noisemaker/compiler/lang/effect_registry.gd. See
// effect_registry.cpp for the PARITY-CRITICAL details (starter derivation
// by the pass-input rule, namespaced-only op/starter keys, vec4->color
// rewrite, verbatim vs. filtered `choices`).
//
// Effect definitions ship as JSON under qt/noisemaker/effects/<ns>/
// <func>.json (generated from the reference by tools/convert-
// definitions.mjs; READ-ONLY input to this file). For every definition
// this builds the same structures the reference builds when it registers
// effects at load:
//   - effects  : lookup map. Each def registered under the reference's
//                four keys -- func | <ns>.<func> | <ns>/<func> | <ns>.
//                <func> (key 4 duplicates key 2; "<ns>/<func>" uses the
//                effect dir name, which == func for the whole catalog).
//   - ops      : op-spec map "<ns>.<func>" -> {name, args}. args derived
//                from `globals` exactly as canvas.js does (vec4->color,
//                choices -> derived enumPath, enum passthrough, absent
//                fields omitted). NAMESPACED KEYS ONLY -- the reference
//                (canvas.js registerEffectWithRuntime, dump-registry.mjs,
//                dump-validate.mjs, export-graph.mjs) never registers a
//                bare "<func>" op key.
//   - starterOps : set of "<ns>.<func>" op names that are STARTERS (valid
//                as a chain root, need no pipeline input). Derived from
//                the pass-input rule (mirrors tools/export-graph.mjs / the
//                REAL graph-producing path), NOT the per-effect `starter`
//                JSON field -- see effect_registry.cpp. NAMESPACED KEYS
//                ONLY, matching registerStarterOps()'s actual call sites
//                throughout the reference (canvas.js registerEffectWithRuntime
//                lazy-load path additionally registers BARE names too, but
//                that is a different bootstrap this port does not mirror --
//                our oracle is tools/dump-validate.mjs, which never does).
//   - paramAliases / effectAliases : "<ns>.<func>" -> alias maps.
//   - defineMap : "<ns>.<func>" -> {globalKey: DEFINE_NAME} for globals
//                that are compile-time defines (Expander concern; exposed
//                here since EffectRegistry is the single load point).
//   - enums    : an owned Enums instance; effect `choices` register as
//                nested leaves <ns>.<func>.<key>.<choice> (+ any sanitized
//                alias), mirroring canvas's choicesToRegister +
//                mergeIntoEnums.
//
// Instance-owned, no static/global state (the reference uses module
// globals; both prior ports use an owned instance threaded explicitly to
// the Validator, matching T8's own no-global-state precedent). loadAll()
// is idempotent-by-construction (call it once per instance).

#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "enums.h"

namespace nm {

// The op param spec the Validator iterates (reference/02 SS5.5 -- one per
// `globals` entry, in declaration order). Optional reference fields that
// the JSON may omit are QJsonValue::Undefined when absent, so presence
// tracking for the check_registry.mjs dump falls out of QJsonValue itself
// rather than a parallel set of `has*` bools.
// NOTE ON QJsonValue "absent" TRACKING (verified empirically -- see task
// report "hazards"): QJsonValue's OWN default constructor (`QJsonValue v;`
// / `QJsonValue()` / a struct member with no initializer) yields
// Type::Null, NOT Type::Undefined. Only QJsonObject::value() on a MISSING
// key, or the explicit `QJsonValue(QJsonValue::Undefined)` tag
// constructor, actually produce Undefined. Every field below is therefore
// EXPLICITLY initialized to the Undefined tag (never relying on the bare
// default constructor), and "presence" is additionally exposed via TYPE-
// checking accessors (hasMin/hasMax/hasUniform/...) rather than a raw
// isUndefined() call, so a future field that forgets the explicit
// initializer still degrades safely (Null fails every isDouble()/
// isString()/isObject() check exactly like Undefined does).
struct ParamDef {
    QString name;             // globals key; always present
    QString type;             // globals[key].type, post vec4->color rewrite
    QJsonValue defaultValue = QJsonValue(QJsonValue::Undefined);  // globals[key].default
    QJsonValue enumPath = QJsonValue(QJsonValue::Undefined);      // spec.enum || spec.enumPath, OR the
                               // synthesized "<ns>.<func>.<key>" path when
                               // choices had no explicit enum; feeds BOTH
                               // the dumped "enum" and "enumPath" keys.
    QJsonValue minValue = QJsonValue(QJsonValue::Undefined);      // globals[key].min
    QJsonValue maxValue = QJsonValue(QJsonValue::Undefined);      // globals[key].max
    QJsonValue uniformValue = QJsonValue(QJsonValue::Undefined);  // globals[key].uniform
    QJsonValue choicesValue = QJsonValue(QJsonValue::Undefined);  // globals[key].choices VERBATIM
                               // (incl. ':' group-header entries) -- the
                               // ops-dump consumer; NOT the same filtering
                               // as enum registration (see .cpp).
    QJsonValue defaultFromValue = QJsonValue(QJsonValue::Undefined); // globals[key].defaultFrom

    bool hasDefault() const { return !defaultValue.isUndefined(); }
    bool hasEnumPath() const { return enumPath.isString(); }
    QString enumPathString() const { return enumPath.toString(); }
    // PRESENCE checks (matches the reference's blind `min: spec.min` /
    // `max: spec.max` passthrough -- min/max are usually plain numbers for
    // scalar params, but CAN be a 3-element array for a vec3 param's
    // per-component bounds, e.g. render.renderLit3d's cameraPosition
    // min:[-1,-1,-1] -- verified against the oracle). minAsDouble()/
    // maxAsDouble() are for the validator's SCALAR numeric-clamp path only,
    // which never runs for a vec3-typed param in the first place.
    bool hasMin() const { return !minValue.isUndefined(); }
    double minAsDouble() const { return minValue.toDouble(); }
    bool hasMax() const { return !maxValue.isUndefined(); }
    double maxAsDouble() const { return maxValue.toDouble(); }
    bool hasUniform() const { return !uniformValue.isUndefined(); }
    bool hasChoices() const { return choicesValue.isObject(); }
    QJsonObject choicesObject() const { return choicesValue.toObject(); }
    bool hasDefaultFrom() const { return defaultFromValue.isString(); }
    QString defaultFromString() const { return defaultFromValue.toString(); }
};

struct OpSpec {
    QString name;   // bare func
    QString opName; // "<ns>.<func>"
    QVector<ParamDef> args;
};

class EffectRegistry {
public:
    // Loads every qt/noisemaker/effects/<ns>/<func>.json under
    // `<dataRoot>/effects` (dataRoot is the SAME concept
    // nm::Backend::setup()/main.cpp's resolveDataRoot() use -- the
    // "qt/noisemaker" directory, not the effects dir itself). Silently
    // registers nothing if the directory is absent (matches the reference
    // ports' graceful-degradation behavior; callers that need a hard
    // failure check emptiness themselves).
    void loadAll(const QString& dataRoot);

    // CWD-relative resolution of the default dataRoot ("qt/noisemaker",
    // when run from the repo root) -- sufficient for the parity-gate
    // helper tools (registry_dump, dump_validate.cpp), which are always
    // invoked from the repo root by their driving parity/*.mjs scripts.
    // Does NOT replicate main.cpp's fuller executable-relative resolution
    // (that helper is anonymous-namespace-local to main.cpp, a frozen
    // shared file -- see PORTING-GUIDE.md file ownership rules).
    static QString defaultDataRoot();

    // Op-spec lookup (the validator's ops[name]). Namespaced keys only
    // ("<ns>.<func>") -- returns nullptr for a bare func name or an
    // unregistered op, matching the reference exactly (see file header).
    const OpSpec* getOp(const QString& opName) const;

    // Effect definition lookup (the future Expander's getEffect). Returns
    // the raw effect JSON object, or an empty object if `key` was never
    // registered.
    QJsonObject getEffect(const QString& key) const;
    bool hasEffect(const QString& key) const;

    // reference/02 SS9 isStarterOp. Namespaced-registration semantics baked
    // in structurally (see effect_registry.cpp) -- do not "fix" a bare-name
    // query to consult search order here; that is the validator's job via
    // canResolveOpName / the real candidateNames loop.
    bool isStarterOp(const QString& name) const;

    // reference/01 SS8.4. Returns the deprecation warning string, or an
    // empty QString if `opName` carries no alias.
    QString checkEffectAlias(const QString& opName) const;

    // reference/01 SS8.5 -- mutates `kwargs` in place (old key renamed to
    // new, new key wins if both present), returns one warning string per
    // alias hit.
    QStringList resolveParamAliases(const QString& opName, QJsonObject& kwargs) const;

    Enums& enums() { return enums_; }
    const Enums& enums() const { return enums_; }

    // "<ns>.<func>" -> {globalKey: DEFINE_NAME} for globals carrying a
    // compile-time `define` (Expander/orchestrator concern; T9 only
    // populates and exposes this table).
    const QJsonObject& defineMap() const { return defineMap_; }

    // {ops, enums, paramAliases, effectAliases, effectKeys} -- the exact
    // surface parity/check_registry.mjs diffs against tools/dump-
    // registry.mjs's oracle output. Optional/absent fields are omitted
    // from the dump (never emitted as JSON null), matching the oracle's
    // own JSON.parse(JSON.stringify(...)) undefined-dropping round trip.
    QJsonObject dumpSummary() const;

private:
    // Takes the RAW file bytes (not a pre-parsed QJsonObject) because
    // `globals` key ORDER must be recovered from the source text itself --
    // see effect_registry.cpp's objectKeyOrder() comment.
    void registerEffect(const QByteArray& rawJson);
    static bool isStarterDef(const QJsonObject& def);
    static QJsonObject opSpecToJson(const OpSpec& spec);

    QMap<QString, QJsonObject> effects_;      // lookup key -> raw effect def
    QMap<QString, OpSpec> ops_;               // "<ns>.<func>" -> spec
    QSet<QString> starterOps_;                // "<ns>.<func>" set
    QMap<QString, QJsonObject> paramAliases_; // "<ns>.<func>" -> {old:new}, non-empty only
    QMap<QString, QString> effectAliases_;    // "<ns>.<func>" -> replacement name
    QJsonObject defineMap_;                   // "<ns>.<func>" -> {globalKey: DEFINE_NAME}
    Enums enums_;
};

} // namespace nm
