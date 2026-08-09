#include "validator.h"

#include "ast.h"
#include "diagnostics.h"
#include "effect_registry.h"
#include "enums.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>

#include <charconv>
#include <cmath>

namespace nm {

namespace {

// PARITY-CRITICAL behaviors replicated exactly from the reference
// shaders/src/lang/validator.js (source of truth), cross-checked against
// td/noisemaker/compiler/lang/validator.py (registry-passed-in class
// shape; UnsupportedDsl fail-loud sites) and godot/addons/noisemaker/
// compiler/lang/validator.gd (full non-throwing argument-resolution
// fidelity -- used to verify the SHARED logic paths, since godot never
// diverges from the reference there), and spot-verified against the
// reference oracle (NM_REFERENCE_ROOT tools/dump-validate.mjs) for every
// hazard called out below -- see task report "hazards encountered":
//   - `nodeId` in a diagnostic is `node?.id` -- NOT a validator-assigned
//     id. It is present (possibly `null`) IFF the triggering AST node
//     literally has an "id" FIELD, which only Subchain nodes ever do; for
//     every other node type the key is entirely absent after JSON
//     serialization (undefined-valued keys are dropped).
//   - `location` in a diagnostic is `{line}` ONLY, never `{line,column}`:
//     the reference reads `node.loc.column`, but parser.js's loc objects
//     only ever have `.line`/`.col` (not `.column`) -- a reference typo
//     that survives because JSON.stringify silently drops the resulting
//     `column: undefined`. TD's port sets a real `column` value here
//     (reading `.col` into `d['column']`) -- VERIFIED WRONG against the
//     live oracle; do not copy that.
//   - ALLOWED_STRING_PARAMS has FOUR entries (adds 'text.style'), not the
//     three both prior ports transcribe -- verified directly against the
//     live shaders/src/lang/validator.js source and filter/text.json's
//     real `style` global (type:"string", default "").
//   - "member"-typed params (`def.type === 'member'`, e.g. filter.channel,
//     filter.palette, filter3d.palette3d, synth.osc2d -- FOUR effects
//     total) are the ONLY ones dispatched through the dedicated member
//     resolver; an unrecognized value SILENTLY falls back to the default,
//     NO diagnostic. Every other choices/enum-bearing global (e.g.
//     synth.noise's `type`/`loopOffset`/`colorMode`, any `palette`-typed
//     global) is declared plain "int"/"float"/"palette" and falls through
//     to the NUMERIC resolver's `def.enum`/`def.choices` Ident branches
//     instead, which DO push S003 on failure. Verified against the oracle
//     with both `filter.channel(channel: bogus)` (silent, default 0, no
//     diagnostic) and `noise(type: bogus)` (S003, default 10).
//   - vec2/mat3/palette (and anything else not one of the eight explicitly
//     dispatched types) fall through to the numeric resolver too -- a
//     bare Number argument clamps as a SCALAR even though the default may
//     be an array (verified: synth.media's vec2 imageSize with no
//     override keeps its [1024,1024] array default verbatim; with
//     `imageSize: 5` becomes the scalar 5).
//   - `clamp()` requires `typeof min/max === 'number'`; an array-valued
//     min/max (vec3 params' per-component bounds, e.g.
//     render.renderLit3d's cameraPosition min:[-1,-1,-1]) is never used
//     for clamping -- only ever relevant to numeric-typed params, whose
//     min/max are always plain numbers in the catalog, but ported as an
//     explicit type check (not assumed) for fidelity.
//   - Subchain begin/end steps carry NO `iterations` field -- TD's port
//     adds one (an intra-frame ping-pong loop count) that does not exist
//     anywhere in the JS reference; the parser (T8) does not produce an
//     `iterations` key on Subchain nodes either. Not ported.
//   - `evalExpr`/`evalCondition` are UNREACHABLE in this port and are not
//     implemented at all (matches TD's own omission, for the same
//     reason): their only call sites are IfStmt's condition and Return's
//     value, both of which raise UnsupportedDsl before ever reaching
//     them.
//   - `isStarterOp`'s bare-canonical-name fallback branch is
//     STRUCTURALLY DEAD on this catalog: STARTER_OPS (registerStarterOps)
//     is only ever populated with NAMESPACED "<ns>.<func>" keys -- by the
//     real reference (canvas.js's non-lazy path, export-graph.mjs) AND by
//     this port's EffectRegistry -- so a bare query name (the common case
//     for an un-namespaced call) NEVER matches STARTER_OPS directly, and
//     the `parts.length > 1` guard requires the QUERY name to already be
//     dotted. Net effect (verified against the oracle): a starter chain
//     with no explicit write() only produces S006 when reached via an
//     explicit from()-resolved namespace; `search synth\nsolid(...)` with
//     no write() produces ONLY S001, never S006. Ported bug-for-bug.

QString diagSeverity(const QString& code) {
    if (code == QStringLiteral("S002") || code == QStringLiteral("S007") || code == QStringLiteral("S008")) {
        return QStringLiteral("warning");
    }
    return QStringLiteral("error");
}

QString diagDefaultMessage(const QString& code) {
    static const QMap<QString, QString> table = {
        {QStringLiteral("S001"), QStringLiteral("Unknown identifier")},
        {QStringLiteral("S002"), QStringLiteral("Argument out of range")},
        {QStringLiteral("S003"), QStringLiteral("Variable used before assignment")},
        {QStringLiteral("S004"), QStringLiteral("Cannot assign null or undefined")},
        {QStringLiteral("S005"), QStringLiteral("Illegal chain structure")},
        {QStringLiteral("S006"), QStringLiteral("Starter chain missing write() call")},
        {QStringLiteral("S007"), QStringLiteral("Deprecated parameter alias")},
        {QStringLiteral("S008"), QStringLiteral("Deprecated effect")},
    };
    return table.value(code);
}

const QSet<QString>& stateSurfaces() {
    static const QSet<QString> s = {
        QStringLiteral("time"), QStringLiteral("frame"), QStringLiteral("mouse"),
        QStringLiteral("resolution"), QStringLiteral("seed"), QStringLiteral("a"),
    };
    return s;
}

const QSet<QString>& stateValues() {
    static const QSet<QString> s = {
        QStringLiteral("time"),  QStringLiteral("frame"),     QStringLiteral("mouse"), QStringLiteral("resolution"),
        QStringLiteral("seed"),  QStringLiteral("a"),         QStringLiteral("u1"),    QStringLiteral("u2"),
        QStringLiteral("u3"),    QStringLiteral("u4"),        QStringLiteral("s1"),    QStringLiteral("s2"),
        QStringLiteral("b1"),    QStringLiteral("b2"),        QStringLiteral("a1"),    QStringLiteral("a2"),
        QStringLiteral("deltaTime"),
    };
    return s;
}

// !! Do not expand -- strict allowlist for string params (reference
// ALLOWED_STRING_PARAMS; SEE FILE HEADER re: the 4th entry).
const QSet<QString>& allowedStringParams() {
    static const QSet<QString> s = {
        QStringLiteral("text.text"),
        QStringLiteral("text.font"),
        QStringLiteral("text.justify"),
        QStringLiteral("text.style"),
    };
    return s;
}

const QSet<QString>& surfacePassthroughCalls() {
    static const QSet<QString> s = {QStringLiteral("read")};
    return s;
}

bool isTruthy(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Null:
        case QJsonValue::Undefined:
            return false;
        case QJsonValue::Bool:
            return v.toBool();
        case QJsonValue::Double:
            return v.toDouble() != 0.0;
        case QJsonValue::String:
            return !v.toString().isEmpty();
        case QJsonValue::Array:
        case QJsonValue::Object:
            return true;
    }
    return false;
}

// Formats a double exactly like JS's String(number)/template-literal
// interpolation (shortest round-trippable decimal; no trailing ".0" for
// integral values) -- diagnostic MESSAGE TEXT is machine-gated by
// check_validate.mjs (full deep-equality on `out`, unlike the lex/parse
// gates' ok-only comparison), so this must match byte-for-byte for every
// value the corpus's min/max-clamp diagnostics interpolate.
QString jsNumberToString(double value) {
    if (value == 0.0) return QStringLiteral("0"); // JS: String(-0) === "0"
    char buf[32];
    const auto res = std::to_chars(buf, buf + sizeof(buf), value);
    return QString::fromLatin1(buf, static_cast<int>(res.ptr - buf));
}

// value -> [non-empty string segments], or an EMPTY list for "null" (no
// path) -- mirrors normalizeMemberPath's null-collapsing exactly (an
// array with only empty/falsy segments, an empty string, or a value of
// neither array/string/number type, all become "no path"; a plain number
// becomes its single-segment string form).
QStringList normalizeMemberPath(const QJsonValue& value) {
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue& seg : value.toArray()) {
            if (seg.isString() && !seg.toString().isEmpty()) parts.append(seg.toString());
        }
        return parts;
    }
    if (value.isString()) {
        QStringList parts;
        const QStringList segs = value.toString().split(QLatin1Char('.'));
        for (const QString& seg : segs) {
            const QString t = seg.trimmed();
            if (!t.isEmpty()) parts.append(t);
        }
        return parts;
    }
    if (value.isDouble()) {
        return {jsNumberToString(value.toDouble())};
    }
    return {};
}
QStringList normalizeMemberPath(const QStringList& value) { return value; } // already normalized

bool pathStartsWith(const QStringList& path, const QStringList& prefix) {
    if (prefix.isEmpty()) return true;
    if (path.size() < prefix.size()) return false;
    for (int i = 0; i < prefix.size(); ++i) {
        if (path.at(i) != prefix.at(i)) return false;
    }
    return true;
}

QStringList applyEnumPrefix(const QStringList& path, const QStringList& prefix) {
    if (path.isEmpty()) return path;
    if (prefix.isEmpty()) return path;
    if (pathStartsWith(path, prefix)) return path;
    for (int i = 1; i < prefix.size(); ++i) {
        const QStringList suffix = prefix.mid(i);
        if (pathStartsWith(path, suffix)) {
            return prefix.mid(0, i) + path;
        }
    }
    return prefix + path;
}

// clamp(value, min, max): min/max only apply when they are PLAIN NUMBERS
// (reference `typeof min === 'number'`) -- an array-valued min/max (vec3
// per-component bounds) never clamps a scalar.
double clampValue(double value, const QJsonValue& min, const QJsonValue& max) {
    if (min.isDouble() && value < min.toDouble()) return min.toDouble();
    if (max.isDouble() && value > max.toDouble()) return max.toDouble();
    return value;
}

bool toBoolean(const QJsonValue& v) {
    if (v.isDouble()) return v.toDouble() != 0.0;
    return isTruthy(v);
}

QString nodeType(const QJsonValue& node) { return node.isObject() ? node.toObject().value(QStringLiteral("type")).toString() : QString(); }

// ---------------------------------------------------------------- Validator

class Validator {
public:
    explicit Validator(EffectRegistry& reg) : reg_(reg) {}

    QJsonObject run(const QJsonObject& ast);

private:
    EffectRegistry& reg_;
    QJsonArray diagnostics_;
    QMap<QString, QJsonValue> symbols_;
    QStringList programSearchOrder_;
    int tempIndex_ = 0;

    // -------------------------------------------------- diagnostics
    void pushDiag(const QString& code, const QJsonValue& node, const QString& message = QString());
    static QString extractIdentifierName(const QJsonValue& node);

    // -------------------------------------------------- enum / symbol resolution
    QJsonValue resolveEnum(const QStringList& path);
    bool canResolveOpName(const QString& name);
    QJsonValue resolveCall(QJsonObject call);
    static QJsonValue firstChainCall(const QJsonValue& node);
    QJsonValue getStarterInfo(const QJsonValue& node);
    bool isStarterChain(const QJsonValue& node);
    QJsonValue substitute(const QJsonValue& node);
    void bindVar(const QJsonObject& v);
    static QJsonValue buildNamespaceSnapshot(const QJsonValue& callNamespace);

    // -------------------------------------------------- statement / chain compilation
    QJsonValue compileStmt(const QJsonObject& stmt); // Undefined => produced nothing
    QJsonObject compileChainStatement(const QJsonObject& stmt);
    int processChain(QJsonArray& chain, const QJsonArray& calls, int input, bool allowStarterless,
                      const QString& writeName);

    // -------------------------------------------------- surfaces
    static QJsonValue toSurface(const QJsonValue& arg);
    QJsonValue callToSurface(const QJsonValue& node);
    static QJsonValue make3dRef(const QJsonValue& node, const QString& defaultKind);
    static QString surfaceName(const QJsonValue& node);

    // -------------------------------------------------- argument resolution
    QJsonValue resolveArgs(QJsonArray& chain, const OpSpec& spec, const QJsonObject& call, const QString& opName,
                            const QJsonObject& original, QJsonObject& args, const QString& writeName);
    void resolveSurfaceArg(QJsonArray& chain, const ParamDef& def, const QJsonValue& node, const QJsonObject& call,
                            QJsonObject& args, const QString& argKey, const QString& writeName);
    void resolveColorArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call, QJsonObject& args,
                          const QString& argKey);
    void resolveVecArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call, QJsonObject& args,
                        const QString& argKey, int n);
    void resolveBooleanArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call, QJsonObject& args,
                            const QString& argKey);
    void resolveMemberArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call, QJsonObject& args,
                           const QString& argKey);
    void resolveVolumeOrGeometryArg(const ParamDef& def, const QJsonValue& node, QJsonObject& args,
                                     const QString& argKey, const QString& kind);
    void resolveStringArg(const ParamDef& def, const QJsonValue& node, const QString& opName,
                           const QJsonObject& original, QJsonObject& args, const QString& argKey);
    void resolveNumericArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call, const OpSpec& spec,
                            QJsonObject& args, const QString& argKey);
    QJsonObject resolveOscillator(const QJsonObject& node);
    QJsonValue resolveOscParam(const QJsonValue& param);
};

// ---------------------------------------------------------------- diagnostics

QString Validator::extractIdentifierName(const QJsonValue& nodeVal) {
    if (!nodeVal.isObject()) return QString();
    const QJsonObject node = nodeVal.toObject();
    const QString t = node.value(QStringLiteral("type")).toString();
    if (t == NodeKind::Ident) return node.value(QStringLiteral("name")).toString();
    if (t == NodeKind::Member && node.value(QStringLiteral("path")).isArray()) {
        QStringList segs;
        for (const QJsonValue& s : node.value(QStringLiteral("path")).toArray()) segs.append(s.toString());
        return segs.join(QLatin1Char('.'));
    }
    if (t == NodeKind::Call) return node.value(QStringLiteral("name")).toString();
    if (t == NodeKind::Func && isTruthy(node.value(QStringLiteral("src")))) {
        const QString src = node.value(QStringLiteral("src")).toString();
        return QStringLiteral("{%1%2}").arg(src.left(30), src.size() > 30 ? QStringLiteral("...") : QString());
    }
    if (isTruthy(node.value(QStringLiteral("name")))) return node.value(QStringLiteral("name")).toString();
    if (isTruthy(node.value(QStringLiteral("value")))) {
        const QJsonValue v = node.value(QStringLiteral("value"));
        return v.isDouble() ? jsNumberToString(v.toDouble()) : v.toVariant().toString();
    }
    return QStringLiteral("[%1]").arg(t.isEmpty() ? QStringLiteral("unknown") : t);
}

void Validator::pushDiag(const QString& code, const QJsonValue& nodeVal, const QString& message) {
    const QString msg = message.isEmpty() ? diagDefaultMessage(code) : message;
    const QString identName = extractIdentifierName(nodeVal);
    QString enriched = msg;
    if (!identName.isEmpty() && !msg.contains(identName) && !msg.contains(QLatin1Char('\''))) {
        enriched = QStringLiteral("%1: '%2'").arg(msg, identName);
    }

    QJsonObject diag;
    diag.insert(QStringLiteral("code"), code);
    diag.insert(QStringLiteral("message"), enriched);
    diag.insert(QStringLiteral("severity"), diagSeverity(code));
    // nodeId: node?.id -- present (possibly null) IFF the node literally
    // has an "id" key (only true of Subchain nodes; see file header).
    const QJsonObject node = nodeVal.toObject();
    if (node.contains(QStringLiteral("id"))) {
        diag.insert(QStringLiteral("nodeId"), node.value(QStringLiteral("id")));
    }
    // location: {line} ONLY -- never "column" (see file header).
    const QJsonValue locVal = node.value(QStringLiteral("loc"));
    if (locVal.isObject()) {
        QJsonObject location;
        location.insert(QStringLiteral("line"), locVal.toObject().value(QStringLiteral("line")));
        diag.insert(QStringLiteral("location"), location);
    }
    if (!identName.isEmpty()) diag.insert(QStringLiteral("identifier"), identName);
    diagnostics_.append(diag);
}

// ---------------------------------------------------------------- enum resolution

QJsonValue Validator::resolveEnum(const QStringList& path) {
    if (path.isEmpty()) return QJsonValue(QJsonValue::Undefined);
    const QString head = path.first();
    QJsonValue cur;
    if (symbols_.contains(head)) {
        cur = symbols_.value(head);
        const QString t = nodeType(cur);
        if (t == NodeKind::Number || t == NodeKind::Boolean) {
            cur = cur.toObject().value(QStringLiteral("value"));
        }
    } else {
        cur = reg_.enums().tryGetHead(head);
        if (cur.isUndefined()) return QJsonValue(QJsonValue::Undefined);
    }
    for (int i = 1; i < path.size(); ++i) {
        if (cur.isObject() && cur.toObject().contains(path.at(i))) {
            cur = cur.toObject().value(path.at(i));
        } else {
            return QJsonValue(QJsonValue::Undefined);
        }
    }
    if (cur.isObject()) {
        const QString t = cur.toObject().value(QStringLiteral("type")).toString();
        if (t == QStringLiteral("Number") || t == QStringLiteral("Boolean")) {
            return cur.toObject().value(QStringLiteral("value"));
        }
    }
    return cur;
}

bool Validator::canResolveOpName(const QString& name) {
    for (const QString& ns : programSearchOrder_) {
        if (reg_.getOp(ns + QLatin1Char('.') + name) != nullptr) return true;
    }
    return false;
}

QJsonValue Validator::resolveCall(QJsonObject call) {
    const QString name = call.value(QStringLiteral("name")).toString();
    if (!symbols_.contains(name)) return call;
    const QJsonValue val = symbols_.value(name);
    const QString vt = nodeType(val);
    if (vt == NodeKind::Ident) {
        QJsonObject out = call;
        out.insert(QStringLiteral("name"), val.toObject().value(QStringLiteral("name")));
        return out;
    }
    if (vt == NodeKind::Call) {
        const QJsonObject valObj = val.toObject();
        QJsonArray mergedArgs = valObj.value(QStringLiteral("args")).toArray();
        for (const QJsonValue& a : call.value(QStringLiteral("args")).toArray()) mergedArgs.append(a); // APPEND
        QJsonObject mergedKw;
        bool haveKw = false;
        if (valObj.contains(QStringLiteral("kwargs"))) {
            mergedKw = valObj.value(QStringLiteral("kwargs")).toObject();
            haveKw = true;
        }
        if (call.contains(QStringLiteral("kwargs"))) {
            haveKw = true;
            const QJsonObject ck = call.value(QStringLiteral("kwargs")).toObject();
            for (auto it = ck.constBegin(); it != ck.constEnd(); ++it) mergedKw.insert(it.key(), it.value());
        }
        QJsonObject merged;
        merged.insert(QStringLiteral("type"), NodeKind::Call);
        merged.insert(QStringLiteral("name"), valObj.value(QStringLiteral("name")));
        merged.insert(QStringLiteral("args"), mergedArgs);
        if (haveKw) merged.insert(QStringLiteral("kwargs"), mergedKw);
        if (call.contains(QStringLiteral("namespace"))) {
            merged.insert(QStringLiteral("namespace"), call.value(QStringLiteral("namespace")));
        } else if (valObj.contains(QStringLiteral("namespace"))) {
            merged.insert(QStringLiteral("namespace"), valObj.value(QStringLiteral("namespace")));
        }
        return merged;
    }
    return call;
}

QJsonValue Validator::firstChainCall(const QJsonValue& node) {
    if (!node.isObject()) return QJsonValue(QJsonValue::Undefined);
    const QJsonObject o = node.toObject();
    const QString t = o.value(QStringLiteral("type")).toString();
    if (t == NodeKind::Call) return node;
    if (t == NodeKind::Chain) {
        const QJsonArray chain = o.value(QStringLiteral("chain")).toArray();
        if (!chain.isEmpty() && nodeType(chain.first()) == NodeKind::Call) return chain.first();
    }
    return QJsonValue(QJsonValue::Undefined);
}

// {call,index} of the first STARTER op in the chain, or Undefined. NOTE:
// the QUERY name here is the call's BARE name UNLESS its namespace was
// explicitly resolved (from()) -- see file header re: isStarterOp's
// bare-name limitation, ported bug-for-bug.
QJsonValue Validator::getStarterInfo(const QJsonValue& node) {
    if (!node.isObject()) return QJsonValue(QJsonValue::Undefined);
    const QJsonObject o = node.toObject();
    const QString t = o.value(QStringLiteral("type")).toString();
    auto queryName = [](const QJsonObject& call) {
        QString name = call.value(QStringLiteral("name")).toString();
        const QJsonValue ns = call.value(QStringLiteral("namespace"));
        if (ns.isObject() && isTruthy(ns.toObject().value(QStringLiteral("resolved")))) {
            name = ns.toObject().value(QStringLiteral("resolved")).toString() + QLatin1Char('.') + name;
        }
        return name;
    };
    if (t == NodeKind::Call) {
        if (reg_.isStarterOp(queryName(o))) {
            QJsonObject info;
            info.insert(QStringLiteral("call"), o);
            info.insert(QStringLiteral("index"), 0);
            return info;
        }
        return QJsonValue(QJsonValue::Undefined);
    }
    if (t == NodeKind::Chain) {
        const QJsonArray chain = o.value(QStringLiteral("chain")).toArray();
        for (int i = 0; i < chain.size(); ++i) {
            if (nodeType(chain.at(i)) == NodeKind::Call && reg_.isStarterOp(queryName(chain.at(i).toObject()))) {
                QJsonObject info;
                info.insert(QStringLiteral("call"), chain.at(i));
                info.insert(QStringLiteral("index"), i);
                return info;
            }
        }
    }
    return QJsonValue(QJsonValue::Undefined);
}

bool Validator::isStarterChain(const QJsonValue& node) {
    if (nodeType(node) != NodeKind::Chain) return false;
    const QJsonValue info = getStarterInfo(node);
    return info.isObject() && info.toObject().value(QStringLiteral("index")).toInt(-1) == 0;
}

QJsonValue Validator::substitute(const QJsonValue& nodeVal) {
    if (!nodeVal.isObject()) return nodeVal;
    const QJsonObject node = nodeVal.toObject();
    const QString t = node.value(QStringLiteral("type")).toString();
    if (t == NodeKind::Ident && symbols_.contains(node.value(QStringLiteral("name")).toString())) {
        QJsonValue result = substitute(symbols_.value(node.value(QStringLiteral("name")).toString()));
        // Marks the substituted node as having come from a variable
        // reference -- resolveNumericArg's Number/Boolean case and
        // resolveOscillator both read this back (verified against the
        // oracle: `let o = osc(...); noise(scaleX: o)` embeds `_varRef`
        // BOTH at the Oscillator value's top level AND nested inside its
        // `_ast` copy, because the mutation happens on the shared node
        // object here, before resolveOscillator captures it as `_ast`).
        if (result.isObject()) {
            QJsonObject r = result.toObject();
            r.insert(QStringLiteral("_varRef"), node.value(QStringLiteral("name")));
            result = r;
        }
        return result;
    }
    if (t == NodeKind::Chain) {
        QJsonArray mapped;
        for (const QJsonValue& cVal : node.value(QStringLiteral("chain")).toArray()) {
            const QJsonObject c = cVal.toObject();
            QJsonArray mappedArgs;
            for (const QJsonValue& a : c.value(QStringLiteral("args")).toArray()) mappedArgs.append(substitute(a));
            QJsonObject mappedCall;
            mappedCall.insert(QStringLiteral("type"), NodeKind::Call);
            mappedCall.insert(QStringLiteral("name"), c.value(QStringLiteral("name")));
            mappedCall.insert(QStringLiteral("args"), mappedArgs);
            if (c.contains(QStringLiteral("kwargs"))) {
                QJsonObject kw;
                const QJsonObject srcKw = c.value(QStringLiteral("kwargs")).toObject();
                for (auto it = srcKw.constBegin(); it != srcKw.constEnd(); ++it) kw.insert(it.key(), substitute(it.value()));
                mappedCall.insert(QStringLiteral("kwargs"), kw);
            }
            mapped.append(resolveCall(mappedCall));
        }
        QJsonObject out;
        out.insert(QStringLiteral("type"), NodeKind::Chain);
        out.insert(QStringLiteral("chain"), mapped);
        return out;
    }
    if (t == NodeKind::Call) {
        QJsonArray mappedArgs;
        for (const QJsonValue& a : node.value(QStringLiteral("args")).toArray()) mappedArgs.append(substitute(a));
        QJsonObject mappedCall;
        mappedCall.insert(QStringLiteral("type"), NodeKind::Call);
        mappedCall.insert(QStringLiteral("name"), node.value(QStringLiteral("name")));
        mappedCall.insert(QStringLiteral("args"), mappedArgs);
        if (node.contains(QStringLiteral("kwargs"))) {
            QJsonObject kw;
            const QJsonObject srcKw = node.value(QStringLiteral("kwargs")).toObject();
            for (auto it = srcKw.constBegin(); it != srcKw.constEnd(); ++it) kw.insert(it.key(), substitute(it.value()));
            mappedCall.insert(QStringLiteral("kwargs"), kw);
        }
        return resolveCall(mappedCall);
    }
    return nodeVal;
}

void Validator::bindVar(const QJsonObject& v) {
    const QJsonValue expr = substitute(v.value(QStringLiteral("expr")));
    if (isStarterChain(expr)) {
        const QJsonValue head = firstChainCall(expr);
        if (!head.isUndefined()) pushDiag(QStringLiteral("S006"), head);
    }
    const QString exprType = nodeType(expr);
    if (expr.isUndefined() || expr.isNull()
        || (exprType == NodeKind::Ident
            && (expr.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("null")
                || expr.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("undefined")))) {
        pushDiag(QStringLiteral("S004"), v);
        return;
    }
    const QString name = v.value(QStringLiteral("name")).toString();
    if (exprType == NodeKind::Ident) {
        const QString identName = expr.toObject().value(QStringLiteral("name")).toString();
        if (!symbols_.contains(identName) && !stateValues().contains(identName) && reg_.getOp(identName) == nullptr
            && !canResolveOpName(identName)) {
            pushDiag(QStringLiteral("S003"), expr);
            return;
        }
    }
    if (exprType == NodeKind::Chain && expr.toObject().value(QStringLiteral("chain")).toArray().size() == 1) {
        symbols_.insert(name, expr.toObject().value(QStringLiteral("chain")).toArray().first());
    } else if (exprType == NodeKind::Member) {
        const QJsonValue resolved = resolveEnum(normalizeMemberPath(expr.toObject().value(QStringLiteral("path"))));
        if (resolved.isDouble()) {
            symbols_.insert(name, Enums::leaf(resolved.toDouble()));
        } else if (!resolved.isUndefined()) {
            symbols_.insert(name, resolved);
        } else {
            symbols_.insert(name, expr);
        }
    } else {
        symbols_.insert(name, expr);
    }
}

QJsonValue Validator::buildNamespaceSnapshot(const QJsonValue& callNamespaceVal) {
    if (!callNamespaceVal.isObject()) return QJsonValue(QJsonValue::Undefined);
    const QJsonObject cn = callNamespaceVal.toObject();
    QJsonObject callSnap;
    callSnap.insert(QStringLiteral("name"), cn.value(QStringLiteral("name")).isString()
                                                 ? cn.value(QStringLiteral("name"))
                                                 : QJsonValue(QJsonValue::Null));
    callSnap.insert(QStringLiteral("resolved"), cn.value(QStringLiteral("resolved")).isString()
                                                     ? cn.value(QStringLiteral("resolved"))
                                                     : QJsonValue(QJsonValue::Null));
    callSnap.insert(QStringLiteral("explicit"), isTruthy(cn.value(QStringLiteral("explicit"))));
    callSnap.insert(QStringLiteral("source"), cn.value(QStringLiteral("source")).isString()
                                                   ? cn.value(QStringLiteral("source"))
                                                   : QJsonValue(QJsonValue::Null));
    if (cn.value(QStringLiteral("searchOrder")).isArray()) {
        callSnap.insert(QStringLiteral("searchOrder"), cn.value(QStringLiteral("searchOrder")));
    }
    if (isTruthy(cn.value(QStringLiteral("fromOverride")))) {
        callSnap.insert(QStringLiteral("fromOverride"), true);
    }
    QJsonObject snapshot;
    snapshot.insert(QStringLiteral("call"), callSnap);
    if (isTruthy(cn.value(QStringLiteral("resolved")))) {
        snapshot.insert(QStringLiteral("resolved"), cn.value(QStringLiteral("resolved")));
    }
    return snapshot;
}

// ---------------------------------------------------------------- surfaces

QJsonValue Validator::toSurface(const QJsonValue& argVal) {
    if (!argVal.isObject()) return QJsonValue(QJsonValue::Undefined);
    const QJsonObject arg = argVal.toObject();
    const QString t = arg.value(QStringLiteral("type")).toString();
    auto ref = [&](const char* kind) {
        QJsonObject o;
        o.insert(QStringLiteral("kind"), QString::fromLatin1(kind));
        o.insert(QStringLiteral("name"), arg.value(QStringLiteral("name")));
        return QJsonValue(o);
    };
    if (t == NodeKind::OutputRef) return ref("output");
    if (t == NodeKind::SourceRef) return ref("source");
    if (t == NodeKind::XyzRef) return ref("xyz");
    if (t == NodeKind::VelRef) return ref("vel");
    if (t == NodeKind::RgbaRef) return ref("rgba");
    if (t == NodeKind::MeshRef) return ref("mesh");
    if (t == NodeKind::Ident && arg.value(QStringLiteral("name")).toString() == QStringLiteral("none")) {
        return ref("output");
    }
    if (t == NodeKind::Ident && stateSurfaces().contains(arg.value(QStringLiteral("name")).toString())) {
        return ref("state");
    }
    return QJsonValue(QJsonValue::Undefined);
}

QJsonValue Validator::callToSurface(const QJsonValue& nodeVal) {
    if (!nodeVal.isObject()) return QJsonValue(QJsonValue::Undefined);
    const QJsonObject node = nodeVal.toObject();
    if (node.value(QStringLiteral("type")).toString() == NodeKind::Chain
        && node.value(QStringLiteral("chain")).toArray().size() == 1) {
        return callToSurface(node.value(QStringLiteral("chain")).toArray().first());
    }
    if (node.value(QStringLiteral("type")).toString() != NodeKind::Call
        || !surfacePassthroughCalls().contains(node.value(QStringLiteral("name")).toString())) {
        return QJsonValue(QJsonValue::Undefined);
    }
    QJsonValue target;
    const QJsonArray args = node.value(QStringLiteral("args")).toArray();
    if (!args.isEmpty()) {
        target = args.first();
    } else if (node.value(QStringLiteral("kwargs")).isObject()) {
        target = node.value(QStringLiteral("kwargs")).toObject().value(QStringLiteral("tex"));
    }
    if (target.isUndefined() || target.isNull()) return QJsonValue(QJsonValue::Undefined);
    return toSurface(target);
}

QString Validator::surfaceName(const QJsonValue& nVal) {
    if (!nVal.isObject()) return QString();
    const QJsonObject n = nVal.toObject();
    return n.value(QStringLiteral("name")).toString();
}

QJsonValue Validator::make3dRef(const QJsonValue& nVal, const QString& defaultKind) {
    const QString name = surfaceName(nVal);
    if (name.isEmpty()) return QJsonValue(QJsonValue::Undefined);
    QString kind = defaultKind;
    if (defaultKind == QStringLiteral("vol")) {
        kind = (nodeType(nVal) == NodeKind::VolRef) ? QStringLiteral("vol") : QStringLiteral("tex3d");
    } else {
        kind = QStringLiteral("geo");
    }
    QJsonObject o;
    o.insert(QStringLiteral("kind"), kind);
    o.insert(QStringLiteral("name"), name);
    return o;
}

// ---------------------------------------------------------------- statement compilation

QJsonValue Validator::compileStmt(const QJsonObject& stmt) {
    const QString t = stmt.value(QStringLiteral("type")).toString();
    // UnsupportedDsl fail-loud points 1-2 of 9 (see validator.h / file
    // header): control flow is fully INTERPRETED at runtime by the
    // reference (a live `fn(state)` closure decides which branch runs
    // each frame); this AOT frontend has no interpreter to defer to.
    if (t == NodeKind::IfStmt) {
        throw UnsupportedDsl(QStringLiteral(
            "if/elif/else branches are not implemented in the first-cut DSL frontend (reference/02 SS4.1)."));
    }
    if (t == NodeKind::Break || t == NodeKind::Continue || t == NodeKind::Return) {
        throw UnsupportedDsl(
            QStringLiteral("break/continue/return are not implemented in the first-cut DSL frontend (reference/02 SS4)."));
    }
    // The chain-statement wrapper has NO "type" key (identified by the
    // presence of "chain" alone -- ast.h / parser.cpp T8 convention).
    if (!stmt.contains(QStringLiteral("type")) && stmt.contains(QStringLiteral("chain"))) {
        // compileChainStatement returns an EMPTY object as its "null plan"
        // sentinel (the missing-write() error path -- JS `return null`); a
        // real plan always has at least a "chain" key, so emptiness alone
        // distinguishes the two cases.
        const QJsonObject compiled = compileChainStatement(stmt);
        return compiled.isEmpty() ? QJsonValue(QJsonValue::Undefined) : QJsonValue(compiled);
    }
    return QJsonValue(QJsonValue::Undefined);
}

QJsonObject Validator::compileChainStatement(const QJsonObject& stmt) {
    QJsonArray chain;
    const QJsonArray stmtChain = stmt.value(QStringLiteral("chain")).toArray();

    QJsonObject chainNode;
    chainNode.insert(QStringLiteral("type"), NodeKind::Chain);
    chainNode.insert(QStringLiteral("chain"), stmtChain);
    const bool hasWrite = !stmt.value(QStringLiteral("write")).isNull() || !stmt.value(QStringLiteral("write3d")).isNull();

    if (!hasWrite && isStarterChain(chainNode)) {
        pushDiag(QStringLiteral("S006"), stmtChain.isEmpty() ? QJsonValue(QJsonValue::Undefined) : stmtChain.first());
    }
    if (!hasWrite) {
        pushDiag(QStringLiteral("S001"), stmtChain.isEmpty() ? QJsonValue(QJsonValue::Undefined) : stmtChain.first(),
                  QStringLiteral("Chain must have explicit write() or write3d() target"));
        return QJsonObject(); // null plan (JS `return null`)
    }

    const QJsonValue writeVal = stmt.value(QStringLiteral("write"));
    const QString writeName = writeVal.isObject() ? writeVal.toObject().value(QStringLiteral("name")).toString() : QString();

    QJsonValue write3dTarget = QJsonValue(QJsonValue::Null);
    if (!stmt.value(QStringLiteral("write3d")).isNull()) {
        const QJsonObject w3 = stmt.value(QStringLiteral("write3d")).toObject();
        QJsonObject tex3d;
        tex3d.insert(QStringLiteral("kind"), QStringLiteral("vol"));
        tex3d.insert(QStringLiteral("name"), surfaceName(w3.value(QStringLiteral("tex3d"))));
        QJsonObject geo;
        geo.insert(QStringLiteral("kind"), QStringLiteral("geo"));
        geo.insert(QStringLiteral("name"), surfaceName(w3.value(QStringLiteral("geo"))));
        QJsonObject w3out;
        w3out.insert(QStringLiteral("tex3d"), tex3d);
        w3out.insert(QStringLiteral("geo"), geo);
        write3dTarget = w3out;
    }

    const int finalIndex = processChain(chain, stmtChain, -1, false, writeName);

    QJsonValue writeSurf = QJsonValue(QJsonValue::Null);
    if (writeVal.isObject()) {
        QJsonObject ws;
        ws.insert(QStringLiteral("kind"), QStringLiteral("output"));
        ws.insert(QStringLiteral("name"), writeVal.toObject().value(QStringLiteral("name")));
        writeSurf = ws;
    }

    QJsonObject plan;
    plan.insert(QStringLiteral("chain"), chain);
    plan.insert(QStringLiteral("write"), writeSurf);
    plan.insert(QStringLiteral("write3d"), write3dTarget);
    plan.insert(QStringLiteral("final"), finalIndex < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(finalIndex));
    plan.insert(QStringLiteral("states"), QJsonArray()); // no validatorHooks are ever registered (dead feature)
    if (stmt.contains(QStringLiteral("leadingComments"))) {
        plan.insert(QStringLiteral("leadingComments"), stmt.value(QStringLiteral("leadingComments")));
    }
    return plan;
}

// ---------------------------------------------------------------- chain flattening (reference/02 SS5)

int Validator::processChain(QJsonArray& chain, const QJsonArray& calls, int input, bool allowStarterless,
                             const QString& writeName) {
    int current = input;
    for (const QJsonValue& originalVal : calls) {
        const QJsonObject original = originalVal.toObject();
        const QString ot = original.value(QStringLiteral("type")).toString();

        // read() builtin -- a STARTER node; illegal to chain inline.
        if (ot == NodeKind::Read) {
            if (current != -1) {
                pushDiag(QStringLiteral("S001"), originalVal,
                          QStringLiteral("read() is a starter node and cannot be chained inline. Use standalone "
                                         "read() to start a new chain."));
                continue;
            }
            const QJsonValue surface = toSurface(original.value(QStringLiteral("surface")));
            if (surface.isUndefined()) {
                pushDiag(QStringLiteral("S001"), originalVal, QStringLiteral("read() requires a valid surface reference"));
                continue;
            }
            const int idx = tempIndex_++;
            QJsonObject stepArgs;
            stepArgs.insert(QStringLiteral("tex"), surface);
            if (original.value(QStringLiteral("_skip")).toBool(false)) stepArgs.insert(QStringLiteral("_skip"), true);
            QJsonObject step;
            step.insert(QStringLiteral("op"), QStringLiteral("_read"));
            step.insert(QStringLiteral("args"), stepArgs);
            step.insert(QStringLiteral("from"), QJsonValue(QJsonValue::Null));
            step.insert(QStringLiteral("temp"), idx);
            step.insert(QStringLiteral("builtin"), true);
            if (original.contains(QStringLiteral("leadingComments"))) {
                step.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
            }
            chain.append(step);
            current = idx;
            continue;
        }

        // read3d() two-arg starter form (single-arg read3d() is handled
        // inside volume/geometry param resolution, not here).
        if (ot == NodeKind::Read3D && !original.value(QStringLiteral("geo")).isNull()) {
            if (current != -1) {
                pushDiag(QStringLiteral("S001"), originalVal,
                          QStringLiteral("read3d() is a starter node and cannot be chained inline. Use standalone "
                                         "read3d() to start a new chain."));
                continue;
            }
            const QJsonValue tex3d = make3dRef(original.value(QStringLiteral("tex3d")), QStringLiteral("vol"));
            const QJsonValue geo = make3dRef(original.value(QStringLiteral("geo")), QStringLiteral("geo"));
            if (tex3d.isUndefined() || geo.isUndefined()) {
                pushDiag(QStringLiteral("S001"), originalVal, QStringLiteral("read3d() as starter requires tex3d and geo references"));
                continue;
            }
            const int idx = tempIndex_++;
            QJsonObject stepArgs;
            stepArgs.insert(QStringLiteral("tex3d"), tex3d);
            stepArgs.insert(QStringLiteral("geo"), geo);
            if (original.value(QStringLiteral("_skip")).toBool(false)) stepArgs.insert(QStringLiteral("_skip"), true);
            QJsonObject step;
            step.insert(QStringLiteral("op"), QStringLiteral("_read3d"));
            step.insert(QStringLiteral("args"), stepArgs);
            step.insert(QStringLiteral("from"), QJsonValue(QJsonValue::Null));
            step.insert(QStringLiteral("temp"), idx);
            step.insert(QStringLiteral("builtin"), true);
            if (original.contains(QStringLiteral("leadingComments"))) {
                step.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
            }
            chain.append(step);
            current = idx;
            continue;
        }

        // write() builtin -- chainable, requires an input.
        if (ot == NodeKind::Write) {
            const QJsonValue surface = toSurface(original.value(QStringLiteral("surface")));
            if (surface.isUndefined()) {
                pushDiag(QStringLiteral("S001"), originalVal, QStringLiteral("write() requires a valid surface reference"));
                continue;
            }
            if (current == -1) {
                pushDiag(QStringLiteral("S005"), originalVal, QStringLiteral("write() requires an input - cannot be first in chain"));
                continue;
            }
            const int idx = tempIndex_++;
            QJsonObject stepArgs;
            stepArgs.insert(QStringLiteral("tex"), surface);
            QJsonObject step;
            step.insert(QStringLiteral("op"), QStringLiteral("_write"));
            step.insert(QStringLiteral("args"), stepArgs);
            step.insert(QStringLiteral("from"), current);
            step.insert(QStringLiteral("temp"), idx);
            step.insert(QStringLiteral("builtin"), true);
            if (original.contains(QStringLiteral("leadingComments"))) {
                step.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
            }
            chain.append(step);
            current = idx;
            continue;
        }

        // write3d() chain node.
        if (ot == NodeKind::Write3D) {
            const QJsonValue tex3d = make3dRef(original.value(QStringLiteral("tex3d")), QStringLiteral("vol"));
            const QJsonValue geo = make3dRef(original.value(QStringLiteral("geo")), QStringLiteral("geo"));
            if (tex3d.isUndefined() || geo.isUndefined()) {
                pushDiag(QStringLiteral("S001"), originalVal, QStringLiteral("write3d() requires tex3d and geo references"));
                continue;
            }
            if (current == -1) {
                pushDiag(QStringLiteral("S005"), originalVal, QStringLiteral("write3d() requires an input - cannot be first in chain"));
                continue;
            }
            const int idx = tempIndex_++;
            QJsonObject stepArgs;
            stepArgs.insert(QStringLiteral("tex3d"), tex3d);
            stepArgs.insert(QStringLiteral("geo"), geo);
            QJsonObject step;
            step.insert(QStringLiteral("op"), QStringLiteral("_write3d"));
            step.insert(QStringLiteral("args"), stepArgs);
            step.insert(QStringLiteral("from"), current);
            step.insert(QStringLiteral("temp"), idx);
            step.insert(QStringLiteral("builtin"), true);
            if (original.contains(QStringLiteral("leadingComments"))) {
                step.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
            }
            chain.append(step);
            current = idx;
            continue;
        }

        // subchain() -- first-class grouping bracket. NOTE: no
        // "iterations" field anywhere (see file header).
        if (ot == NodeKind::Subchain) {
            if (current == -1) {
                pushDiag(QStringLiteral("S005"), originalVal, QStringLiteral("subchain() requires an input - cannot be first in chain"));
                continue;
            }
            const int beginIdx = tempIndex_++;
            QJsonObject beginArgs;
            beginArgs.insert(QStringLiteral("name"), original.value(QStringLiteral("name")));
            beginArgs.insert(QStringLiteral("id"), original.value(QStringLiteral("id")));
            QJsonObject beginStep;
            beginStep.insert(QStringLiteral("op"), QStringLiteral("_subchain_begin"));
            beginStep.insert(QStringLiteral("args"), beginArgs);
            beginStep.insert(QStringLiteral("from"), current);
            beginStep.insert(QStringLiteral("temp"), beginIdx);
            beginStep.insert(QStringLiteral("builtin"), true);
            if (original.contains(QStringLiteral("leadingComments"))) {
                beginStep.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
            }
            chain.append(beginStep);
            current = beginIdx;

            current = processChain(chain, original.value(QStringLiteral("body")).toArray(), current, false, writeName);

            const int endIdx = tempIndex_++;
            QJsonObject endArgs;
            endArgs.insert(QStringLiteral("name"), original.value(QStringLiteral("name")));
            endArgs.insert(QStringLiteral("id"), original.value(QStringLiteral("id")));
            QJsonObject endStep;
            endStep.insert(QStringLiteral("op"), QStringLiteral("_subchain_end"));
            endStep.insert(QStringLiteral("args"), endArgs);
            endStep.insert(QStringLiteral("from"), current == -1 ? QJsonValue(QJsonValue::Null) : QJsonValue(current));
            endStep.insert(QStringLiteral("temp"), endIdx);
            endStep.insert(QStringLiteral("builtin"), true);
            chain.append(endStep);
            current = endIdx;
            continue;
        }

        // regular effect call (reference/02 SS5.2)
        QJsonObject call = resolveCall(original).toObject();
        const QJsonValue callNs = call.value(QStringLiteral("namespace"));
        QStringList searchOrder = programSearchOrder_;
        if (callNs.isObject() && callNs.toObject().value(QStringLiteral("searchOrder")).isArray()) {
            searchOrder.clear();
            for (const QJsonValue& v : callNs.toObject().value(QStringLiteral("searchOrder")).toArray()) searchOrder.append(v.toString());
        }
        QStringList candidateNames;
        if (callNs.isObject() && isTruthy(callNs.toObject().value(QStringLiteral("resolved")))) {
            candidateNames.append(callNs.toObject().value(QStringLiteral("resolved")).toString() + QLatin1Char('.')
                                   + call.value(QStringLiteral("name")).toString());
        }
        for (const QString& ns : searchOrder) candidateNames.append(ns + QLatin1Char('.') + call.value(QStringLiteral("name")).toString());
        QString opName;
        const OpSpec* spec = nullptr;
        for (const QString& candidate : candidateNames) {
            if (candidate.isEmpty()) continue;
            if (const OpSpec* s = reg_.getOp(candidate)) {
                opName = candidate;
                spec = s;
                break;
            }
        }
        if (!spec) {
            pushDiag(QStringLiteral("S001"), originalVal,
                      QStringLiteral("Unknown effect: '%1'").arg(call.value(QStringLiteral("name")).toString()));
            continue;
        }
        const QString aliasWarning = reg_.checkEffectAlias(opName);
        if (!aliasWarning.isEmpty()) pushDiag(QStringLiteral("S008"), originalVal, aliasWarning);

        if (opName == QStringLiteral("prev")) {
            const int idx = tempIndex_++;
            QJsonObject prevTex;
            prevTex.insert(QStringLiteral("kind"), QStringLiteral("output"));
            prevTex.insert(QStringLiteral("name"), writeName);
            QJsonObject prevArgs;
            prevArgs.insert(QStringLiteral("tex"), prevTex);
            QJsonObject step;
            step.insert(QStringLiteral("op"), opName);
            step.insert(QStringLiteral("args"), prevArgs);
            step.insert(QStringLiteral("from"), current == -1 ? QJsonValue(QJsonValue::Null) : QJsonValue(current));
            step.insert(QStringLiteral("temp"), idx);
            const QJsonValue nsSnap = buildNamespaceSnapshot(callNs);
            if (!nsSnap.isUndefined()) step.insert(QStringLiteral("namespace"), nsSnap);
            if (original.contains(QStringLiteral("leadingComments"))) {
                step.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
            }
            chain.append(step);
            current = idx;
            continue;
        }

        const bool starter = reg_.isStarterOp(opName);
        const bool starterlessRoot = (current == -1);
        const bool allowPassthroughRoot = allowStarterless && surfacePassthroughCalls().contains(opName);
        if (starterlessRoot && !starter && !allowPassthroughRoot) {
            pushDiag(QStringLiteral("S005"), originalVal);
            continue;
        }
        const bool starterHasInput = starter && current != -1;
        const int fromInput = starterHasInput ? -1 : current;
        if (starterHasInput) pushDiag(QStringLiteral("S005"), originalVal);

        QJsonObject args;
        const QJsonValue argSources = resolveArgs(chain, *spec, call, opName, original, args, writeName);

        const int idx = tempIndex_++;
        QJsonObject step;
        step.insert(QStringLiteral("op"), opName);
        step.insert(QStringLiteral("args"), args);
        step.insert(QStringLiteral("from"), fromInput == -1 ? QJsonValue(QJsonValue::Null) : QJsonValue(fromInput));
        step.insert(QStringLiteral("temp"), idx);
        const QJsonValue nsSnap = buildNamespaceSnapshot(callNs);
        if (!nsSnap.isUndefined()) step.insert(QStringLiteral("namespace"), nsSnap);
        if (original.contains(QStringLiteral("leadingComments"))) {
            step.insert(QStringLiteral("leadingComments"), original.value(QStringLiteral("leadingComments")));
        }
        if (original.value(QStringLiteral("kwargs")).isObject() && !original.value(QStringLiteral("kwargs")).toObject().isEmpty()) {
            step.insert(QStringLiteral("rawKwargs"), original.value(QStringLiteral("kwargs")));
        }
        if (!argSources.isUndefined()) step.insert(QStringLiteral("argSources"), argSources);
        chain.append(step);
        current = idx;
    }
    return current;
}

// ---------------------------------------------------------------- argument resolution (reference/02 SS6)

bool matchesRefPattern(const QString& name, const QString& prefix) {
    if (name.size() != prefix.size() + 1 || !name.startsWith(prefix)) return false;
    const QChar d = name.at(prefix.size());
    return d >= QLatin1Char('0') && d <= QLatin1Char('7');
}

QJsonValue Validator::resolveArgs(QJsonArray& chain, const OpSpec& spec, const QJsonObject& call,
                                   const QString& opName, const QJsonObject& original, QJsonObject& args,
                                   const QString& writeName) {
    const bool hasKw = call.contains(QStringLiteral("kwargs"));
    QJsonObject kw = hasKw ? call.value(QStringLiteral("kwargs")).toObject() : QJsonObject();
    if (hasKw) {
        const QStringList warnings = reg_.resolveParamAliases(opName, kw);
        for (const QString& w : warnings) pushDiag(QStringLiteral("S007"), call, w);
    }
    const QJsonArray callArgs = call.value(QStringLiteral("args")).toArray();
    QSet<QString> seen;
    QJsonObject argSources;
    bool haveArgSources = false;

    for (int i = 0; i < spec.args.size(); ++i) {
        const ParamDef& def = spec.args.at(i);
        QJsonValue node = (hasKw && kw.contains(def.name)) ? kw.value(def.name)
                                                             : (i < callArgs.size() ? callArgs.at(i) : QJsonValue(QJsonValue::Undefined));
        node = substitute(node);
        const QString argKey = def.name;

        // color-splat special case: a bare Color literal spread into THREE
        // consecutive r/g/b NUMERIC params (only fires positional-only,
        // never with kwargs). Structurally unreachable on this catalog (no
        // effect declares consecutive r/g/b globals) -- ported for
        // fidelity, never exercised.
        if (!hasKw && nodeType(node) == NodeKind::Color && def.type != QStringLiteral("color") && def.name == QStringLiteral("r")
            && i + 2 < spec.args.size() && spec.args.at(i + 1).name == QStringLiteral("g")
            && spec.args.at(i + 2).name == QStringLiteral("b")) {
            const QJsonArray cv = node.toObject().value(QStringLiteral("value")).toArray();
            args.insert(QStringLiteral("r"), cv.at(0));
            args.insert(spec.args.at(i + 1).name, cv.at(1));
            args.insert(spec.args.at(i + 2).name, cv.at(2));
            i += 2;
            continue;
        }

        if (hasKw && kw.contains(def.name)) seen.insert(def.name);

        // array literal -- additive numeric input form.
        if (nodeType(node) == NodeKind::ArrayLiteral) {
            QJsonArray value;
            for (const QJsonValue& el : node.toObject().value(QStringLiteral("elements")).toArray()) {
                if (nodeType(el) == NodeKind::Number) {
                    value.append(el.toObject().value(QStringLiteral("value")));
                } else {
                    pushDiag(QStringLiteral("S002"), el,
                              QStringLiteral("Array element must be a number for '%1' in %2()")
                                  .arg(def.name, call.value(QStringLiteral("name")).toString()));
                    value.append(0);
                }
            }
            args.insert(argKey, value);
            argSources.insert(argKey, QStringLiteral("array"));
            haveArgSources = true;
            continue;
        }

        const QString ty = def.type;
        if (ty == QStringLiteral("surface")) {
            resolveSurfaceArg(chain, def, node, call, args, argKey, writeName);
        } else if (ty == QStringLiteral("color")) {
            resolveColorArg(def, node, call, args, argKey);
        } else if (ty == QStringLiteral("vec3")) {
            resolveVecArg(def, node, call, args, argKey, 3);
        } else if (ty == QStringLiteral("vec4")) {
            resolveVecArg(def, node, call, args, argKey, 4);
        } else if (ty == QStringLiteral("boolean")) {
            resolveBooleanArg(def, node, call, args, argKey);
        } else if (ty == QStringLiteral("member")) {
            resolveMemberArg(def, node, call, args, argKey);
        } else if (ty == QStringLiteral("volume")) {
            resolveVolumeOrGeometryArg(def, node, args, argKey, QStringLiteral("vol"));
        } else if (ty == QStringLiteral("geometry")) {
            resolveVolumeOrGeometryArg(def, node, args, argKey, QStringLiteral("geo"));
        } else if (ty == QStringLiteral("string")) {
            resolveStringArg(def, node, opName, original, args, argKey);
        } else {
            // numeric catch-all: int/float/palette/vec2/mat3/anything else
            // not one of the eight explicitly-dispatched types (see file
            // header).
            resolveNumericArg(def, node, call, spec, args, argKey);
        }
    }

    // _skip meta-argument (reference/02 SS6.14).
    if (hasKw && kw.contains(QStringLiteral("_skip"))) {
        const QJsonValue skipNode = kw.value(QStringLiteral("_skip"));
        args.insert(QStringLiteral("_skip"),
                     nodeType(skipNode) == NodeKind::Boolean && skipNode.toObject().value(QStringLiteral("value")).toBool());
        seen.insert(QStringLiteral("_skip"));
    }
    // unknown-kwarg sweep.
    if (hasKw) {
        for (auto it = kw.constBegin(); it != kw.constEnd(); ++it) {
            if (!seen.contains(it.key())) {
                pushDiag(QStringLiteral("S001"), it.value(),
                          QStringLiteral("Unknown argument '%1' for %2()").arg(it.key(), call.value(QStringLiteral("name")).toString()));
            }
        }
    }
    return haveArgSources ? QJsonValue(argSources) : QJsonValue(QJsonValue::Undefined);
}

// 6.1 surface
void Validator::resolveSurfaceArg(QJsonArray& chain, const ParamDef& def, const QJsonValue& node,
                                   const QJsonObject& call, QJsonObject& args, const QString& argKey,
                                   const QString& writeName) {
    if (nodeType(node) == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node, QStringLiteral("String literal not allowed for surface parameter '%1'").arg(def.name));
        QJsonValue dflt = QJsonValue(QJsonValue::Null);
        if (def.defaultValue.isString()) {
            QJsonObject ident;
            ident.insert(QStringLiteral("type"), NodeKind::Ident);
            ident.insert(QStringLiteral("name"), def.defaultValue);
            const QJsonValue s = toSurface(ident);
            if (!s.isUndefined()) dflt = s;
        }
        args.insert(argKey, dflt);
        return;
    }
    QJsonValue surf = QJsonValue(QJsonValue::Undefined);
    bool invalidStarterChain = false;
    const QJsonValue starter = node.isUndefined() ? QJsonValue(QJsonValue::Undefined) : getStarterInfo(node);

    if (nodeType(node) == NodeKind::Read && node.toObject().value(QStringLiteral("surface")).isObject()) {
        surf = toSurface(node.toObject().value(QStringLiteral("surface")));
    }
    const QJsonValue inlineSurface = !surf.isUndefined() ? surf : callToSurface(node);
    if (!inlineSurface.isUndefined()) {
        surf = inlineSurface;
    } else if (nodeType(node) == NodeKind::Chain) {
        const int idx = processChain(chain, node.toObject().value(QStringLiteral("chain")).toArray(), -1, true, writeName);
        if (idx != -1) {
            QJsonObject t;
            t.insert(QStringLiteral("kind"), QStringLiteral("temp"));
            t.insert(QStringLiteral("index"), idx);
            surf = t;
        }
    } else if (nodeType(node) == NodeKind::Call) {
        QJsonArray single;
        single.append(node);
        const int idx = processChain(chain, single, -1, true, writeName);
        if (idx != -1) {
            QJsonObject t;
            t.insert(QStringLiteral("kind"), QStringLiteral("temp"));
            t.insert(QStringLiteral("index"), idx);
            surf = t;
        }
    } else if (!starter.isUndefined()) {
        pushDiag(QStringLiteral("S005"), starter.toObject().value(QStringLiteral("call")));
        invalidStarterChain = true;
    } else {
        surf = toSurface(node);
    }

    if (surf.isUndefined()) {
        if (invalidStarterChain) {
            args.insert(argKey, QJsonValue(QJsonValue::Null));
            return;
        }
        const bool hasDefault = def.defaultValue.isString();
        if (!hasDefault) {
            if (node.isUndefined()) {
                pushDiag(QStringLiteral("S001"), call,
                          QStringLiteral("Missing required surface argument '%1' for %2()")
                              .arg(def.name, call.value(QStringLiteral("name")).toString()));
            } else if (nodeType(node) == NodeKind::Ident
                       && !symbols_.contains(node.toObject().value(QStringLiteral("name")).toString())) {
                pushDiag(QStringLiteral("S003"), node,
                          QStringLiteral("Undefined variable '%1' for '%2' in %3()")
                              .arg(node.toObject().value(QStringLiteral("name")).toString(), def.name,
                                   call.value(QStringLiteral("name")).toString()));
            } else {
                const QJsonObject n = node.toObject();
                QString nodeName;
                if (isTruthy(n.value(QStringLiteral("name")))) {
                    nodeName = n.value(QStringLiteral("name")).toString();
                } else if (n.value(QStringLiteral("path")).isArray()) {
                    QStringList segs;
                    for (const QJsonValue& s : n.value(QStringLiteral("path")).toArray()) segs.append(s.toString());
                    nodeName = segs.join(QLatin1Char('.'));
                } else if (isTruthy(n.value(QStringLiteral("value")))) {
                    const QJsonValue v = n.value(QStringLiteral("value"));
                    nodeName = v.isDouble() ? jsNumberToString(v.toDouble()) : v.toVariant().toString();
                } else if (isTruthy(n.value(QStringLiteral("type")))) {
                    nodeName = n.value(QStringLiteral("type")).toString();
                } else {
                    nodeName = QStringLiteral("invalid");
                }
                pushDiag(QStringLiteral("S001"), node,
                          QStringLiteral("Invalid surface reference '%1' for '%2' in %3()")
                              .arg(nodeName, def.name, call.value(QStringLiteral("name")).toString()));
            }
        }
        if (hasDefault) {
            QJsonObject ident;
            ident.insert(QStringLiteral("type"), NodeKind::Ident);
            ident.insert(QStringLiteral("name"), def.defaultValue);
            const QJsonValue s = toSurface(ident);
            if (!s.isUndefined()) {
                surf = s;
            } else {
                QJsonObject pipe;
                pipe.insert(QStringLiteral("kind"), QStringLiteral("pipeline"));
                pipe.insert(QStringLiteral("name"), def.defaultValue);
                surf = pipe;
            }
        }
    }
    args.insert(argKey, surf.isUndefined() ? QJsonValue(QJsonValue::Null) : surf);
}

// 6.2 color
void Validator::resolveColorArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call,
                                 QJsonObject& args, const QString& argKey) {
    if (nodeType(node) == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node, QStringLiteral("String literal not allowed for color parameter '%1'").arg(def.name));
        args.insert(argKey, def.defaultValue);
        return;
    }
    if (nodeType(node) == NodeKind::Color) {
        // reference: node.hex || node.value -- T8's parser never emits a
        // `.hex` field on Color nodes (verified: 339/339 PARSE parity with
        // only `.value`), so this always resolves to `.value`.
        args.insert(argKey, node.toObject().value(QStringLiteral("value")));
        return;
    }
    if (!node.isUndefined() && nodeType(node) != NodeKind::Ident) {
        pushDiag(QStringLiteral("S002"), node,
                  QStringLiteral("Argument out of range for '%1' in %2()").arg(def.name, call.value(QStringLiteral("name")).toString()));
    }
    args.insert(argKey, def.defaultValue);
}

// 6.3 / 6.4 vec3 / vec4
void Validator::resolveVecArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call, QJsonObject& args,
                               const QString& argKey, int n) {
    const QString ctor = n == 3 ? QStringLiteral("vec3") : QStringLiteral("vec4");
    auto defaultOrZero = [&]() -> QJsonValue {
        if (def.defaultValue.isArray()) return def.defaultValue;
        QJsonArray z;
        for (int i = 0; i < n; ++i) z.append(0.0);
        if (n == 4) z[3] = 1.0;
        return z;
    };
    if (nodeType(node) == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node, QStringLiteral("String literal not allowed for %1 parameter '%2'").arg(ctor, def.name));
        args.insert(argKey, defaultOrZero());
        return;
    }
    if (nodeType(node) == NodeKind::Call && node.toObject().value(QStringLiteral("name")).toString() == ctor
        && node.toObject().value(QStringLiteral("args")).toArray().size() == n) {
        QJsonArray value;
        for (const QJsonValue& a : node.toObject().value(QStringLiteral("args")).toArray()) {
            if (nodeType(a) == NodeKind::Number) {
                value.append(a.toObject().value(QStringLiteral("value")));
            } else {
                pushDiag(QStringLiteral("S002"), a,
                          QStringLiteral("Argument out of range for '%1' in %2()").arg(def.name, call.value(QStringLiteral("name")).toString()));
                value.append(0);
            }
        }
        args.insert(argKey, value);
        return;
    }
    if (nodeType(node) == NodeKind::Color) {
        const QJsonArray cv = node.toObject().value(QStringLiteral("value")).toArray();
        QJsonArray value;
        for (int i = 0; i < n && i < cv.size(); ++i) value.append(cv.at(i));
        args.insert(argKey, value);
        return;
    }
    if (!node.isUndefined() && nodeType(node) != NodeKind::Ident) {
        pushDiag(QStringLiteral("S002"), node,
                  QStringLiteral("Argument out of range for '%1' in %2()").arg(def.name, call.value(QStringLiteral("name")).toString()));
    }
    args.insert(argKey, defaultOrZero());
}

// 6.5 boolean
void Validator::resolveBooleanArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call,
                                   QJsonObject& args, const QString& argKey) {
    auto defaultBool = [&]() { return isTruthy(def.defaultValue) ? true : false; };
    if (node.isUndefined()) {
        args.insert(argKey, defaultBool());
        return;
    }
    const QString t = nodeType(node);
    if (t == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node, QStringLiteral("String literal not allowed for boolean parameter '%1'").arg(def.name));
        args.insert(argKey, defaultBool());
        return;
    }
    if (t == NodeKind::Boolean) {
        args.insert(argKey, node.toObject().value(QStringLiteral("value")).toBool());
        return;
    }
    if (t == NodeKind::Number) {
        args.insert(argKey, node.toObject().value(QStringLiteral("value")).toDouble() != 0.0);
        return;
    }
    // UnsupportedDsl 3/9: `(state) => ...` boolean automation has no
    // interpreter to defer to in this AOT frontend.
    if (t == NodeKind::Func) {
        throw UnsupportedDsl(QStringLiteral(
            "Func boolean params ((state)=>...) are not implemented in the first-cut DSL frontend (reference/02 SS6.5)."));
    }
    const QString identName = node.toObject().value(QStringLiteral("name")).toString();
    // UnsupportedDsl 4/9: a bare state-value ident (time/frame/...) reads a
    // LIVE per-frame value; this AOT frontend resolves args once, ahead of
    // time.
    if (t == NodeKind::Ident && stateValues().contains(identName)) {
        throw UnsupportedDsl(QStringLiteral(
            "state-value boolean params are not implemented in the first-cut DSL frontend (reference/02 SS6.5)."));
    }
    if (t == NodeKind::Ident) {
        pushDiag(QStringLiteral("S003"), node);
    } else {
        pushDiag(QStringLiteral("S002"), node,
                  QStringLiteral("Argument out of range for '%1' in %2()").arg(def.name, call.value(QStringLiteral("name")).toString()));
    }
    args.insert(argKey, defaultBool());
}

// 6.6 member (dedicated enum-typed param -- e.g. filter.channel,
// filter.palette, filter3d.palette3d, synth.osc2d ONLY; see file header).
// Falls back to 0 on total failure; NEVER pushes a diagnostic for an
// unresolved value (verified against the oracle:
// `filter.channel(channel: bogus)` -> channel=0, diagnostics:[]).
void Validator::resolveMemberArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call,
                                  QJsonObject& args, const QString& argKey) {
    if (nodeType(node) == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node, QStringLiteral("String literal not allowed for member/enum parameter '%1'").arg(def.name));
        args.insert(argKey, def.defaultValue);
        return;
    }
    const QStringList prefix = normalizeMemberPath(def.hasEnumPath() ? def.enumPath : QJsonValue(QJsonValue::Undefined));
    QStringList path;
    const QString t = nodeType(node);
    if (t == NodeKind::Member) {
        path = normalizeMemberPath(node.toObject().value(QStringLiteral("path")));
    } else if (t == NodeKind::Number || t == NodeKind::Boolean) {
        const QJsonObject n = node.toObject();
        args.insert(argKey, t == NodeKind::Boolean ? QJsonValue(n.value(QStringLiteral("value")).toBool() ? 1 : 0)
                                                     : n.value(QStringLiteral("value")));
        return;
    } else if (t == NodeKind::Ident && stateValues().contains(node.toObject().value(QStringLiteral("name")).toString())) {
        // UnsupportedDsl 5/9.
        throw UnsupportedDsl(QStringLiteral(
            "state-value member params are not implemented in the first-cut DSL frontend (reference/02 SS6.6)."));
    } else if (t == NodeKind::Ident) {
        path = {node.toObject().value(QStringLiteral("name")).toString()};
    }
    if (path.isEmpty()) path = normalizeMemberPath(def.defaultValue);

    QJsonValue resolved = path.isEmpty() ? QJsonValue(QJsonValue::Undefined) : resolveEnum(path);
    if (!resolved.isDouble()) {
        path = applyEnumPrefix(path, prefix);
        if (!prefix.isEmpty() && !pathStartsWith(path, prefix)) {
            pushDiag(QStringLiteral("S001"), !node.isUndefined() ? node : QJsonValue(call),
                      QStringLiteral("Invalid enum value for '%1': expected path starting with '%2'").arg(def.name, prefix.join(QLatin1Char('.'))));
            path = prefix;
        }
        resolved = path.isEmpty() ? QJsonValue(QJsonValue::Undefined) : resolveEnum(path);
    }
    if (!resolved.isDouble()) {
        const QStringList fallback = normalizeMemberPath(def.defaultValue);
        const QJsonValue fallbackValue = fallback.isEmpty() ? QJsonValue(QJsonValue::Undefined) : resolveEnum(fallback);
        resolved = fallbackValue.isDouble() ? fallbackValue : QJsonValue(0.0);
    }
    args.insert(argKey, resolved);
}

// 6.7 / 6.8 volume / geometry
void Validator::resolveVolumeOrGeometryArg(const ParamDef& def, const QJsonValue& node, QJsonObject& args,
                                            const QString& argKey, const QString& kind) {
    const QString label = kind == QStringLiteral("vol") ? QStringLiteral("volume") : QStringLiteral("geometry");
    const QString refType = kind == QStringLiteral("vol") ? NodeKind::VolRef : NodeKind::GeoRef;
    // Plain concatenation (not QString::arg with a repeated placeholder
    // number) to avoid any ambiguity: "vol0-vol7" / "geo0-geo7".
    const QString rangeText = kind + QStringLiteral("0-") + kind + QStringLiteral("7");
    auto refObj = [&](const QString& name) { QJsonObject o; o.insert(QStringLiteral("kind"), kind); o.insert(QStringLiteral("name"), name); return QJsonValue(o); };
    auto defaultOrNull = [&]() { return def.defaultValue.isString() ? refObj(def.defaultValue.toString()) : QJsonValue(QJsonValue::Null); };

    if (nodeType(node) == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node, QStringLiteral("String literal not allowed for %1 parameter '%2'").arg(label, def.name));
        args.insert(argKey, defaultOrNull());
        return;
    }
    QJsonValue value = QJsonValue(QJsonValue::Null);

    if (nodeType(node) == NodeKind::Read3D && node.toObject().value(QStringLiteral("tex3d")).isObject()
        && node.toObject().value(QStringLiteral("geo")).isNull()) {
        const QString nm = surfaceName(node.toObject().value(QStringLiteral("tex3d")));
        if (matchesRefPattern(nm, kind)) {
            value = refObj(nm);
        } else {
            // NOTE: this message (unlike the Ident-branch one below) has NO
            // "or none" suffix -- verified against the exact reference text
            // (shaders/src/lang/validator.js). TD's port drops the
            // interpolated name and the whole "- expected ..." suffix here
            // entirely; that does not match the live reference -- do not
            // copy it.
            pushDiag(QStringLiteral("S001"), node,
                      QStringLiteral("Invalid %1 reference '%2' in read3d() for '%3' - expected %4").arg(label, nm, def.name, rangeText));
            value = defaultOrNull();
        }
    } else if (nodeType(node) == refType) {
        value = refObj(node.toObject().value(QStringLiteral("name")).toString());
    } else if (nodeType(node) == NodeKind::Ident) {
        const QString nm = node.toObject().value(QStringLiteral("name")).toString();
        if (nm == QStringLiteral("none")) {
            value = refObj(QStringLiteral("none"));
        } else if (matchesRefPattern(nm, kind)) {
            value = refObj(nm);
        } else {
            pushDiag(QStringLiteral("S001"), node,
                      QStringLiteral("Invalid %1 reference '%2' for '%3' - expected %4 or none").arg(label, nm, def.name, rangeText));
            value = defaultOrNull();
        }
    } else if (node.isUndefined() && def.defaultValue.isString()) {
        value = refObj(def.defaultValue.toString());
    }
    args.insert(argKey, value);
}

// 6.9 string (STRICT allowlist)
void Validator::resolveStringArg(const ParamDef& def, const QJsonValue& node, const QString& opName,
                                  const QJsonObject& original, QJsonObject& args, const QString& argKey) {
    const int dot = opName.lastIndexOf(QLatin1Char('.'));
    const QString funcName = dot >= 0 ? opName.mid(dot + 1) : opName;
    const QString allowlistKey = funcName + QLatin1Char('.') + def.name;
    if (!allowedStringParams().contains(allowlistKey)) {
        pushDiag(QStringLiteral("S001"), !node.isUndefined() ? node : QJsonValue(original),
                  QStringLiteral("String parameter '%1' on effect '%2' is NOT in the allowed string params list. "
                                 "String params are strictly controlled - use enums or choices instead.")
                      .arg(def.name, funcName));
        args.insert(argKey, def.defaultValue);
        return;
    }
    if (nodeType(node) == NodeKind::String) {
        args.insert(argKey, node.toObject().value(QStringLiteral("value")));
        return;
    }
    if (nodeType(node) == NodeKind::Ident && def.hasChoices()) {
        const QString name = node.toObject().value(QStringLiteral("name")).toString();
        const QJsonObject choices = def.choicesObject();
        if (choices.contains(name)) {
            args.insert(argKey, choices.value(name));
        } else {
            pushDiag(QStringLiteral("S001"), node, QStringLiteral("Invalid choice '%1' for string parameter '%2'").arg(name, def.name));
            args.insert(argKey, def.defaultValue);
        }
        return;
    }
    if (!node.isUndefined()) {
        pushDiag(QStringLiteral("S001"), node,
                  QStringLiteral("String parameter '%1' requires a quoted string literal, got %2").arg(def.name, nodeType(node)));
        args.insert(argKey, def.defaultValue);
        return;
    }
    args.insert(argKey, def.defaultValue);
}

// 6.10 numeric (the catch-all: int/float/palette/vec2/mat3/...)
void Validator::resolveNumericArg(const ParamDef& def, const QJsonValue& node, const QJsonObject& call,
                                   const OpSpec& spec, QJsonObject& args, const QString& argKey) {
    auto numericDefault = [&]() {
        if (def.hasDefaultFrom()) {
            QString refKey = def.defaultFromString();
            for (const ParamDef& d : spec.args) {
                if (d.name == def.defaultFromString()) {
                    refKey = d.name;
                    break;
                }
            }
            if (args.contains(refKey)) return args.value(refKey);
            return def.defaultValue;
        }
        return def.defaultValue;
    };

    if (node.isUndefined()) {
        args.insert(argKey, numericDefault());
        return;
    }
    const QString t = nodeType(node);
    if (t == NodeKind::String) {
        pushDiag(QStringLiteral("S001"), node,
                  QStringLiteral("String literal not allowed for numeric parameter '%1' - strings are only valid "
                                 "for type: \"string\" parameters")
                      .arg(def.name));
        args.insert(argKey, def.defaultValue);
        return;
    }
    if (t == NodeKind::Number || t == NodeKind::Boolean) {
        const QJsonObject n = node.toObject();
        const double value = (t == NodeKind::Boolean) ? (n.value(QStringLiteral("value")).toBool() ? 1.0 : 0.0)
                                                        : n.value(QStringLiteral("value")).toDouble();
        const double clamped = clampValue(value, def.minValue, def.maxValue);
        if (clamped != value) {
            pushDiag(QStringLiteral("S002"), node,
                      QStringLiteral("Argument out of range for '%1' in %2() (got %3, clamped to %4)")
                          .arg(def.name, call.value(QStringLiteral("name")).toString(), jsNumberToString(value),
                               jsNumberToString(clamped)));
        }
        if (n.contains(QStringLiteral("_varRef"))) {
            QJsonObject wrapped;
            wrapped.insert(QStringLiteral("_varRef"), n.value(QStringLiteral("_varRef")));
            wrapped.insert(QStringLiteral("value"), clamped);
            args.insert(argKey, wrapped);
        } else {
            args.insert(argKey, clamped);
        }
        return;
    }
    // UnsupportedDsl 6/9: Func numeric automation.
    if (t == NodeKind::Func) {
        throw UnsupportedDsl(QStringLiteral(
            "Func numeric params ((state)=>...) are not implemented in the first-cut DSL frontend (reference/02 SS6.10)."));
    }
    if (t == NodeKind::Oscillator) {
        args.insert(argKey, resolveOscillator(node.toObject()));
        return;
    }
    // UnsupportedDsl 7/9 and 8/9: midi()/audio() are LIVE external-input
    // automation; nothing to resolve ahead of time.
    if (t == NodeKind::Midi) {
        throw UnsupportedDsl(QStringLiteral(
            "midi() automation args are not implemented in the first-cut DSL frontend (reference/02 SS6.12)."));
    }
    if (t == NodeKind::Audio) {
        throw UnsupportedDsl(QStringLiteral(
            "audio() automation args are not implemented in the first-cut DSL frontend (reference/02 SS6.13)."));
    }
    if (t == NodeKind::Member) {
        const QJsonValue cur = resolveEnum(normalizeMemberPath(node.toObject().value(QStringLiteral("path"))));
        if (cur.isDouble()) {
            const double v = clampValue(cur.toDouble(), def.minValue, def.maxValue);
            if (v != cur.toDouble()) {
                pushDiag(QStringLiteral("S002"), node,
                          QStringLiteral("Argument out of range for '%1' in %2() (got %3, clamped to %4)")
                              .arg(def.name, call.value(QStringLiteral("name")).toString(), jsNumberToString(cur.toDouble()),
                                   jsNumberToString(v)));
            }
            args.insert(argKey, v);
        } else {
            QStringList segs;
            const QJsonValue pathVal = node.toObject().value(QStringLiteral("path"));
            QString pathText;
            if (pathVal.isArray()) {
                for (const QJsonValue& s : pathVal.toArray()) segs.append(s.toString());
                pathText = segs.join(QLatin1Char('.'));
            }
            if (pathText.isEmpty()) pathText = isTruthy(node.toObject().value(QStringLiteral("name")))
                                                    ? node.toObject().value(QStringLiteral("name")).toString()
                                                    : QStringLiteral("unknown");
            pushDiag(QStringLiteral("S001"), node, QStringLiteral("Cannot resolve enum value for '%1': '%2'").arg(def.name, pathText));
            args.insert(argKey, def.defaultValue);
        }
        return;
    }
    const QString identName = node.toObject().value(QStringLiteral("name")).toString();
    // UnsupportedDsl 9/9: a bare state-value ident in numeric position.
    if (t == NodeKind::Ident && stateValues().contains(identName)) {
        throw UnsupportedDsl(QStringLiteral(
            "state-value numeric params (time/frame/...) are not implemented in the first-cut DSL frontend "
            "(reference/02 SS6.10)."));
    }
    if (t == NodeKind::Ident && def.hasEnumPath()) {
        const QStringList prefix = normalizeMemberPath(def.enumPath);
        const QStringList path = prefix.isEmpty() ? QStringList{identName} : (prefix + QStringList{identName});
        const QJsonValue resolved = resolveEnum(path);
        if (resolved.isDouble()) {
            args.insert(argKey, clampValue(resolved.toDouble(), def.minValue, def.maxValue));
        } else {
            pushDiag(QStringLiteral("S003"), node);
            args.insert(argKey, def.defaultValue);
        }
        return;
    }
    if (t == NodeKind::Ident && def.hasChoices()) {
        const QJsonValue choiceVal = def.choicesObject().value(identName);
        if (choiceVal.isDouble()) {
            args.insert(argKey, clampValue(choiceVal.toDouble(), def.minValue, def.maxValue));
        } else {
            pushDiag(QStringLiteral("S003"), node);
            args.insert(argKey, def.defaultValue);
        }
        return;
    }
    if (t == NodeKind::Ident) {
        pushDiag(QStringLiteral("S003"), node);
    } else {
        pushDiag(QStringLiteral("S002"), node,
                  QStringLiteral("Argument out of range for '%1' in %2()").arg(def.name, call.value(QStringLiteral("name")).toString()));
    }
    args.insert(argKey, numericDefault());
}

// 6.11 osc() value oscillator -> resolved config object.
QJsonObject Validator::resolveOscillator(const QJsonObject& node) {
    double oscTypeValue = 0;
    const QJsonValue oscTypeNode = node.value(QStringLiteral("oscType"));
    if (nodeType(oscTypeNode) == NodeKind::Member) {
        const QJsonValue r = resolveEnum(normalizeMemberPath(oscTypeNode.toObject().value(QStringLiteral("path"))));
        if (r.isDouble()) oscTypeValue = r.toDouble();
    } else if (nodeType(oscTypeNode) == NodeKind::Ident) {
        const QJsonValue r = resolveEnum({QStringLiteral("oscKind"), oscTypeNode.toObject().value(QStringLiteral("name")).toString()});
        if (r.isDouble()) oscTypeValue = r.toDouble();
    }
    auto param = [&](const char* key, double fallback) {
        const QJsonValue r = resolveOscParam(node.value(QString::fromLatin1(key)));
        return r.isDouble() ? r.toDouble() : fallback;
    };
    QJsonObject v;
    v.insert(QStringLiteral("type"), QStringLiteral("Oscillator"));
    v.insert(QStringLiteral("oscType"), oscTypeValue);
    v.insert(QStringLiteral("min"), std::max(0.0, std::min(1.0, param("min", 0.0))));
    v.insert(QStringLiteral("max"), std::max(0.0, std::min(1.0, param("max", 1.0))));
    v.insert(QStringLiteral("speed"), param("speed", 1.0));
    v.insert(QStringLiteral("offset"), param("offset", 0.0));
    v.insert(QStringLiteral("seed"), param("seed", 1.0));
    v.insert(QStringLiteral("_ast"), node);
    if (node.contains(QStringLiteral("_varRef"))) v.insert(QStringLiteral("_varRef"), node.value(QStringLiteral("_varRef")));
    return v;
}

QJsonValue Validator::resolveOscParam(const QJsonValue& param) {
    if (!param.isObject()) return QJsonValue(QJsonValue::Undefined);
    const QJsonObject p = param.toObject();
    const QString t = p.value(QStringLiteral("type")).toString();
    if (t == NodeKind::Number) return p.value(QStringLiteral("value"));
    if (t == NodeKind::Boolean) return p.value(QStringLiteral("value")).toBool() ? 1.0 : 0.0;
    if (t == NodeKind::Member) return resolveEnum(normalizeMemberPath(p.value(QStringLiteral("path"))));
    return QJsonValue(QJsonValue::Undefined);
}

// ---------------------------------------------------------------- entry point

QJsonObject Validator::run(const QJsonObject& ast) {
    const QJsonValue renderVal = ast.value(QStringLiteral("render"));
    const QJsonValue render = renderVal.isObject() ? renderVal.toObject().value(QStringLiteral("name")) : QJsonValue(QJsonValue::Null);

    const QJsonValue nsMeta = ast.value(QStringLiteral("namespace"));
    if (nsMeta.isObject() && nsMeta.toObject().value(QStringLiteral("searchOrder")).isArray()) {
        for (const QJsonValue& v : nsMeta.toObject().value(QStringLiteral("searchOrder")).toArray()) {
            programSearchOrder_.append(v.toString());
        }
    }
    if (programSearchOrder_.isEmpty()) {
        // Dead in practice: nm::parse (T8) already guarantees a non-empty
        // search directive, mirroring the reference's OWN parser.js
        // enforcement (identical message text) -- this validator-level
        // check exists only for callers that hand-build an AST bypassing
        // the parser (e.g. unit tests exercising this function directly).
        throw std::runtime_error(
            "Missing required 'search' directive. Every program must start with 'search <namespace>, ...' "
            "to specify namespace search order.");
    }

    for (const QJsonValue& v : ast.value(QStringLiteral("vars")).toArray()) bindVar(v.toObject());

    QJsonArray plans;
    for (const QJsonValue& stmtVal : ast.value(QStringLiteral("plans")).toArray()) {
        const QJsonValue compiled = compileStmt(stmtVal.toObject());
        if (!compiled.isUndefined()) plans.append(compiled);
    }

    QJsonObject result;
    result.insert(QStringLiteral("plans"), plans);
    result.insert(QStringLiteral("diagnostics"), diagnostics_);
    result.insert(QStringLiteral("render"), render);
    result.insert(QStringLiteral("vars"), ast.value(QStringLiteral("vars")).isArray() ? ast.value(QStringLiteral("vars")) : QJsonValue(QJsonArray()));
    QJsonArray searchNamespaces;
    for (const QString& ns : programSearchOrder_) searchNamespaces.append(ns);
    result.insert(QStringLiteral("searchNamespaces"), searchNamespaces);
    if (ast.contains(QStringLiteral("trailingComments"))) {
        result.insert(QStringLiteral("trailingComments"), ast.value(QStringLiteral("trailingComments")));
    }
    return result;
}

} // namespace

QJsonObject validate(const QJsonObject& ast, EffectRegistry& registry) {
    Validator v(registry);
    return v.run(ast);
}

} // namespace nm
