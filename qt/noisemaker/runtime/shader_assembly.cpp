#include "shader_assembly.h"

#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace nm {

namespace {

bool containsWholeWord(const QString& source, const QString& word) {
    const QRegularExpression re(QStringLiteral("\\b%1\\b").arg(QRegularExpression::escape(word)));
    return re.match(source).hasMatch();
}

// Boolean-CONTEXT define detection (T6 cross-track bug: curl/curlSeeded;
// broadened in a later re-review pass -- see below). `curl.frag` declares
// `#ifndef RIDGES / #define RIDGES true / #endif` as its in-shader default,
// then uses `if (RIDGES)` as a genuine bool condition -- but the compiled
// graph's `defines` value for RIDGES arrives as the JSON NUMBER 1/0
// (QJsonValue::Double), not a JSON boolean. Desktop GLSL 330 core is strict
// about bool-vs-int in a condition ("Condition must be of type bool" on
// `if (1)`) where GLSL ES 3.00 / WebGL2-ANGLE tolerates it. Mirrors the
// TouchDesigner port's proven fix for the exact same class
// (`td/noisemaker/runtime/td_backend.py` `_bool_define_keys`/`_truthy`,
// noisemaker-for-touchdesigner).
//
// UPDATE (re-review, final fix wave): the first pass here deliberately
// ported only TD's narrower detector (the `#define K true|false`
// source-declared fallback), declining TD's additional bare-`if (K)`-
// without-a-fallback heuristic to keep the blast radius minimal. Re-review
// validated corpus-wide that the broader heuristic is needed and safe:
// `render3d.frag`/`renderCubemap3d.frag`'s `INVERT` and
// `synth3d/noise3d/precompute.frag`'s `RIDGES` are each used as a bare
// `if (K)` condition with NO in-shader fallback declared at all (the
// runtime is always expected to inject a value for them) -- the narrow
// detector alone misses these, and they hit the identical GLSL-330-vs-
// GLSL-ES bool/int strictness bug curl did. Grep-confirmed against the real
// corpus before broadening this: these three are the ONLY new keys the
// broader detector flags; nothing else in the shipped shader tree matches.
// Below now also detects K used as a bare (optionally negated) condition in
// `if (K)`/`if (!K)`, a ternary `K ? a : b`/`!K ? a : b`, or a logical-op
// `K && ...`/`K || ...` (and negated forms) -- this last pair is broader
// than TD's own precedent, added defensively since "boolean-context" isn't
// only ever spelled as an `if`.
//
// Strips `//` and `/* */` GLSL comments for SCANNING purposes only -- never
// applied to the text that actually gets assembled into shader output,
// which keeps comments verbatim (only `boolDefineKeys()` below consumes
// this). Each comment span is replaced by a single space (matching the
// GLSL preprocessor's own "a comment is equivalent to whitespace" rule),
// with embedded newlines inside a block comment preserved so any future
// line-oriented scan over this text isn't thrown off. A minimal, correct
// state machine -- not a regex -- because a regex-based comment stripper is
// exactly the kind of thing that's easy to get subtly wrong around nested
// `/`/`*` sequences and unterminated comments; this walks the text once,
// character by character, with no backtracking.
//
// Hardening beyond the TouchDesigner precedent this function's caller
// otherwise mirrors: TD's own `_BOOL_DEFINE_RE`/`_BOOL_IF_RE` scan the raw
// (comment-including) shader text, so a `#define K true` sitting inside a
// `/* */` or `//` comment in a TD-side shader would ALSO misclassify K as
// boolean-context there. Re-review here found and fixed that gap rather
// than inheriting it (dormant in this corpus today -- curl.frag's is the
// only live, uncommented hit anywhere) -- worth flagging back to the
// family as a possible backport, not assumed to already be fixed upstream.
QString stripCommentsForScan(const QString& source) {
    QString out;
    out.reserve(source.size());
    const int n = source.size();
    int i = 0;
    while (i < n) {
        const QChar c = source.at(i);
        if (c == QLatin1Char('/') && i + 1 < n && source.at(i + 1) == QLatin1Char('/')) {
            i += 2;
            while (i < n && source.at(i) != QLatin1Char('\n')) {
                ++i;
            }
            out += QLatin1Char(' ');
        } else if (c == QLatin1Char('/') && i + 1 < n && source.at(i + 1) == QLatin1Char('*')) {
            i += 2;
            while (i + 1 < n && !(source.at(i) == QLatin1Char('*') && source.at(i + 1) == QLatin1Char('/'))) {
                if (source.at(i) == QLatin1Char('\n')) {
                    out += QLatin1Char('\n');
                }
                ++i;
            }
            i = std::min(i + 2, n); // skip the closing `*/` (or clamp at EOF if unterminated)
            out += QLatin1Char(' ');
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// Collects every capture-group-1 match of `re` in `scanText` into `keys`.
void collectMatches(const QRegularExpression& re, const QString& scanText, QSet<QString>* keys) {
    QRegularExpressionMatchIterator it = re.globalMatch(scanText);
    while (it.hasNext()) {
        keys->insert(it.next().captured(1));
    }
}

// Matches a bare `#define K true` / `#define K false` line (the source-
// declared-fallback detector -- curl.frag's shape). `[A-Za-z_]\w*` (not
// `\w+`): a preprocessor identifier can't start with a digit, matching
// TD's own regex exactly -- also keeps this consistent with
// `boolConditionKeys()` below, where the distinction matters (a bare `\w+`
// there would capture numeric-literal operands of `?`/`&&`/`||` as fake
// "keys", verified empirically against the real corpus: `if (K == 3)` never
// matches either way, but `K ? a : 0` style expressions elsewhere in the
// corpus have a literal digit on the OTHER side of the operator that `\w+`
// would wrongly treat as a candidate key of its own).
QSet<QString> boolDefineFallbackKeys(const QString& scanText) {
    static const QRegularExpression re(QStringLiteral("#define\\s+([A-Za-z_]\\w*)\\s+(?:true|false)\\b"));
    QSet<QString> keys;
    collectMatches(re, scanText, &keys);
    return keys;
}

// Matches K used as a bare (optionally negated) boolean CONDITION with no
// fallback declared: `if (K)`/`if (!K)`, a ternary `K ? a : b`/`!K ? a : b`,
// or a logical-op `K && ...`/`K || ...` (and negated forms). Anchored to a
// standalone identifier immediately followed (only whitespace between) by
// the operator that makes it a condition -- `K == 1`, `K * 2.0`, `K + 1`,
// `for (int i = 0; i < K; i++)` etc. never match any of these, so a
// genuinely numeric define used in arithmetic/comparison/loop-bound
// contexts is unaffected.
//
// This is a deliberately loose TEXTUAL scan -- it also matches ordinary
// local GLSL variables used as conditions (`bool wrap = ...; if (wrap)
// {...}`), which is harmless by construction: `boolDefineKeys()`'s result
// is only ever consulted via `boolKeys.contains(key)` for a key that is
// ALSO a real entry in the compiled graph's own `defines` object for that
// pass, and the corpus's naming convention keeps every actual compile-time
// define UPPERCASE (verified against every `definition.js` "define" field
// in the reference: exactly `BEHAVIOR`, `COLOR_MODE`, `DIMENSIONS`,
// `FILTERING`, `INVERT`, `OCTAVES`, `RIDGES`) while local variables follow
// ordinary camelCase/lowercase GLSL naming -- so a same-file local variable
// never collides with a real define name in this corpus today. Verified
// exhaustively: cross-referencing this detector's corpus-wide raw matches
// against that authoritative 7-name list confirms the ONLY names that
// actually intersect are `INVERT` (render3d.frag, renderCubemap3d.frag) and
// `RIDGES` (curl.frag, synth3d/noise3d/precompute.frag) -- exactly what
// re-review validated, "and nothing else new" meaning behaviorally, not
// "the regex never matches anything else" (it matches plenty of local
// variables; none of them are ever a real define key). render3d/
// renderCubemap3d/noise3d-precompute's real shape is the first (`if`)
// form; the ternary/logical-op forms are not currently hit by any corpus
// define but are part of this detector's contract regardless (added
// defensively, confirmed to change no current behavior).
QSet<QString> boolConditionKeys(const QString& scanText) {
    static const QRegularExpression ifRe(QStringLiteral("\\bif\\s*\\(\\s*!?\\s*([A-Za-z_]\\w*)\\s*\\)"));
    static const QRegularExpression ternaryRe(QStringLiteral("\\b!?\\s*([A-Za-z_]\\w*)\\s*\\?"));
    static const QRegularExpression logicalRe(QStringLiteral("\\b!?\\s*([A-Za-z_]\\w*)\\s*(?:&&|\\|\\|)"));
    QSet<QString> keys;
    collectMatches(ifRe, scanText, &keys);
    collectMatches(ternaryRe, scanText, &keys);
    collectMatches(logicalRe, scanText, &keys);
    return keys;
}

// Union of both detectors, over the COMMENT-STRIPPED source (the
// `#ifndef K`/`#endif` wrapper around a fallback declaration, if present,
// isn't itself part of either match -- TD's own regex doesn't require it
// either, and it isn't needed: the injected `#define K <value>` line above
// always wins over the in-shader fallback via the `#ifndef` guard
// regardless of whether a detector's regex also matched that guard syntax).
QSet<QString> boolDefineKeys(const QString& source) {
    const QString scanText = stripCommentsForScan(source);
    return boolDefineFallbackKeys(scanText) | boolConditionKeys(scanText);
}

// JS/Python-style truthiness for a QJsonValue, used only for keys
// `boolDefineKeys()` has identified as boolean-context: a nonzero number,
// `true`, or a non-empty/non-"0"/non-"false"/non-"none" string is truthy.
// Matches TD's `_truthy()` exactly (case-insensitive string comparison
// against the same four literals).
bool isTruthy(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool();
    case QJsonValue::Double:
        return value.toDouble() != 0.0;
    case QJsonValue::String: {
        const QString s = value.toString().trimmed().toLower();
        return s != QStringLiteral("0") && s != QStringLiteral("false") && !s.isEmpty()
            && s != QStringLiteral("none");
    }
    default:
        return false;
    }
}

// IEEE-754 binary32 -> binary16, round-to-nearest-even, and the exact
// (lossless) inverse. PORTING-GUIDE.md "Shader assembly contract": desktop
// GLSL caps at 4.10 on macOS, but packHalf2x16/unpackHalf2x16 are 4.20
// builtins, so filter/median (the only user in this corpus) needs them
// shimmed. Function names match the reference builtins exactly so no
// call-site rewriting is needed at the shader source level.
//
// Verified against numpy's IEEE round-to-even float16 cast: 200k+ random
// pack() samples across the full magnitude range plus explicit edge cases
// (zero, +-max normal, overflow boundary, subnormal boundary, exact-tie
// mantissas, +-Inf, NaN), all 65536 possible half bit patterns for
// unpack() exactness, and full round-trip consistency for every non-NaN
// half value -- zero mismatches in all three. See task report for the
// verification script and its output.
const char* const kPackHalfPolyfill =
    "uint nm_f32_to_f16(float f) {\n"
    "    uint x = floatBitsToUint(f);\n"
    "    uint sign = (x >> 16u) & 0x8000u;\n"
    "    uint ax = x & 0x7fffffffu;\n"
    "    uint exp32 = ax >> 23u;\n"
    "    uint mant32 = ax & 0x7fffffu;\n"
    "    if (exp32 == 255u) {\n"
    "        return sign | 0x7c00u | (mant32 != 0u ? 0x0200u : 0u);\n"
    "    }\n"
    "    if (exp32 == 0u) {\n"
    "        return sign;\n"
    "    }\n"
    "    int e = int(exp32) - 127;\n"
    "    if (e > 15) {\n"
    "        return sign | 0x7c00u;\n"
    "    }\n"
    "    if (e >= -14) {\n"
    "        uint q = mant32 >> 13u;\n"
    "        uint rem = mant32 & 0x1fffu;\n"
    "        if (rem > 0x1000u || (rem == 0x1000u && (q & 1u) == 1u)) {\n"
    "            q += 1u;\n"
    "        }\n"
    "        uint expHalf = uint(e + 15);\n"
    "        return sign + (expHalf << 10u) + q;\n"
    "    }\n"
    "    if (e >= -25) {\n"
    "        uint m = mant32 | 0x800000u;\n"
    "        uint shift = uint(-e - 1);\n"
    "        uint q = m >> shift;\n"
    "        uint halfway = 1u << (shift - 1u);\n"
    "        uint rem = m & ((halfway << 1u) - 1u);\n"
    "        if (rem > halfway || (rem == halfway && (q & 1u) == 1u)) {\n"
    "            q += 1u;\n"
    "        }\n"
    "        return sign + q;\n"
    "    }\n"
    "    return sign;\n"
    "}\n"
    "float nm_f16_to_f32(uint h) {\n"
    "    uint sign = (h & 0x8000u) << 16u;\n"
    "    uint exp5 = (h >> 10u) & 0x1fu;\n"
    "    uint mant10 = h & 0x3ffu;\n"
    "    if (exp5 == 0u) {\n"
    "        if (mant10 == 0u) {\n"
    "            return uintBitsToFloat(sign);\n"
    "        }\n"
    "        uint m = mant10;\n"
    "        int e = -14;\n"
    "        while ((m & 0x400u) == 0u) {\n"
    "            m <<= 1u;\n"
    "            e -= 1;\n"
    "        }\n"
    "        m &= 0x3ffu;\n"
    "        uint exp32 = uint(e + 127);\n"
    "        return uintBitsToFloat(sign | (exp32 << 23u) | (m << 13u));\n"
    "    }\n"
    "    if (exp5 == 31u) {\n"
    "        uint mant32 = mant10 == 0u ? 0u : ((mant10 << 13u) | 0x400000u);\n"
    "        return uintBitsToFloat(sign | 0x7f800000u | mant32);\n"
    "    }\n"
    "    uint exp32 = exp5 - 15u + 127u;\n"
    "    return uintBitsToFloat(sign | (exp32 << 23u) | (mant10 << 13u));\n"
    "}\n"
    "uint packHalf2x16(vec2 v) {\n"
    "    return (nm_f32_to_f16(v.y) << 16u) | nm_f32_to_f16(v.x);\n"
    "}\n"
    "vec2 unpackHalf2x16(uint u) {\n"
    "    return vec2(nm_f16_to_f32(u & 0xffffu), nm_f16_to_f32(u >> 16u));\n"
    "}\n";

QString formatDefineValue(const QJsonValue& value) {
    switch (value.type()) {
    case QJsonValue::Bool:
        return value.toBool() ? QStringLiteral("true") : QStringLiteral("false");
    case QJsonValue::Double: {
        const double d = value.toDouble();
        if (std::floor(d) == d && std::abs(d) < 1.0e15) {
            return QString::number(static_cast<qint64>(d));
        }
        return QString::number(d, 'g', 17);
    }
    case QJsonValue::String:
        return value.toString();
    default:
        return QString();
    }
}

} // namespace

bool shaderNeedsPackHalfPolyfill(const QString& source) {
    return containsWholeWord(source, QStringLiteral("packHalf2x16"))
        || containsWholeWord(source, QStringLiteral("unpackHalf2x16"));
}

QByteArray assembleShader(const QString& source, const QJsonObject& defines, bool needsPackPolyfill) {
    QString injected = QStringLiteral(
        "#version 330 core\nprecision highp float;\nprecision highp int;\n");

    const QSet<QString> boolKeys = boolDefineKeys(source);
    for (auto it = defines.begin(); it != defines.end(); ++it) {
        const QString formatted = boolKeys.contains(it.key())
            ? (isTruthy(it.value()) ? QStringLiteral("true") : QStringLiteral("false"))
            : formatDefineValue(it.value());
        injected += QStringLiteral("#define %1 %2\n").arg(it.key(), formatted);
    }

    if (needsPackPolyfill) {
        injected += QString::fromLatin1(kPackHalfPolyfill);
    }

    // Strip exactly the FIRST #version directive line's text, leaving its
    // trailing newline intact -- mirrors the reference's
    // source.replace(/^\s*#version.*$/m, '') byte-for-byte (no 'g' flag
    // upstream, so only the first match is replaced; PORTING-GUIDE.md
    // "Shader assembly contract" / webgl2.js injectDefines ~L889-903).
    static const QRegularExpression versionLine(
        QStringLiteral("^\\s*#version.*$"), QRegularExpression::MultilineOption);
    QString cleaned = source;
    const QRegularExpressionMatch match = versionLine.match(cleaned);
    if (match.hasMatch()) {
        cleaned.remove(match.capturedStart(), match.capturedLength());
    }

    return (injected + cleaned).toUtf8();
}

} // namespace nm
