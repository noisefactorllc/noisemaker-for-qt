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

// Boolean-CONTEXT define detection (T6 cross-track bug: curl/curlSeeded).
// `curl.frag` declares `#ifndef RIDGES / #define RIDGES true / #endif` as
// its in-shader default, then uses `if (RIDGES)` as a genuine bool
// condition -- but the compiled graph's `defines` value for RIDGES arrives
// as the JSON NUMBER 1/0 (QJsonValue::Double), not a JSON boolean. Desktop
// GLSL 330 core is strict about bool-vs-int in a condition ("Condition must
// be of type bool" on `if (1)`) where GLSL ES 3.00 / WebGL2-ANGLE tolerates
// it. Mirrors the TouchDesigner port's proven fix for the exact same class
// (`td/noisemaker/runtime/td_backend.py` `_bool_define_keys`/`_truthy`,
// noisemaker-for-touchdesigner) -- deliberately only the narrower of TD's
// two detectors (the `#define K true|false` source-declared fallback), not
// TD's additional bare-`if (K)`-without-a-fallback heuristic: scoping this
// fix to keys the SHADER ITSELF already declares as boolean keeps the
// blast radius to exactly the curl-class bug, not a blanket "any define
// used in an `if` is a bool" inference that could reinterpret a genuinely
// numeric define elsewhere in the corpus.
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

// Matches a bare `#define K true` / `#define K false` line anywhere in the
// COMMENT-STRIPPED source (the `#ifndef K`/`#endif` wrapper around it, if
// present, isn't part of the match -- TD's own regex doesn't require it
// either, and it isn't needed: the injected `#define K <value>` line above
// always wins over the in-shader fallback via the `#ifndef` guard
// regardless of whether this detector's regex also matched that guard
// syntax).
QSet<QString> boolDefineKeys(const QString& source) {
    static const QRegularExpression re(QStringLiteral("#define\\s+(\\w+)\\s+(?:true|false)\\b"));
    const QString scanText = stripCommentsForScan(source);
    QSet<QString> keys;
    QRegularExpressionMatchIterator it = re.globalMatch(scanText);
    while (it.hasNext()) {
        keys.insert(it.next().captured(1));
    }
    return keys;
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
