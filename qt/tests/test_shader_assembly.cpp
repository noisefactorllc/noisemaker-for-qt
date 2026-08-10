// Unit tests for nm::assembleShader / nm::shaderNeedsPackHalfPolyfill
// (qt/noisemaker/runtime/shader_assembly.{h,cpp}). Plain assert-style
// checks, no test framework dependency (T3 brief Step 2: "QtTest or plain
// `assert` main -- keep it dependency-free").
//
// RED (before shader_assembly.cpp has real definitions): this binary fails
// to LINK ("undefined symbols: nm::assembleShader(...),
// nm::shaderNeedsPackHalfPolyfill(...)"). GREEN: all checks below print
// PASS and the process exits 0.

#include "../noisemaker/runtime/shader_assembly.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <cstdio>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("PASS: %s\n", description);
    } else {
        std::printf("FAIL: %s\n", description);
        ++g_failures;
    }
}

} // namespace

int main() {
    // 1) Header prepended exactly; defines present in the QJsonObject's own
    //    order; existing #version line stripped; body otherwise untouched.
    {
        const QString source =
            QStringLiteral("#version 300 es\n"
                            "precision highp float;\n"
                            "uniform float scaleX;\n"
                            "void main() {\n"
                            "  gl_FragColor = vec4(1.0);\n"
                            "}\n");

        QJsonObject defines;
        defines.insert(QStringLiteral("NOISE_TYPE"), 10);
        defines.insert(QStringLiteral("LOOP_OFFSET"), 300);

        // Qt's QJsonObject does not preserve insertion/parse order -- it
        // always iterates keys in sorted order regardless of how the object
        // was built (verified empirically against this Qt build; see
        // shader_assembly.h's note). So "graph order" here means
        // "assembleShader iterates the QJsonObject in its own natural
        // order" -- determine that order the same way assembleShader must,
        // so this assertion is meaningful rather than assuming JS-style
        // insertion order that Qt does not provide.
        QStringList expectedOrder;
        for (auto it = defines.begin(); it != defines.end(); ++it) {
            expectedOrder << it.key();
        }

        const QByteArray assembled = nm::assembleShader(source, defines, false);
        const QString text = QString::fromUtf8(assembled);

        check(text.startsWith(QStringLiteral(
                  "#version 330 core\nprecision highp float;\nprecision highp int;\n")),
              "header is prepended exactly (#version 330 core + precision lines)");

        int lastPos = -1;
        bool orderOk = true;
        for (const QString& key : expectedOrder) {
            const int pos = text.indexOf(QStringLiteral("#define %1 ").arg(key));
            if (pos < 0 || pos < lastPos) {
                orderOk = false;
                break;
            }
            lastPos = pos;
        }
        check(orderOk, "defines appear in the QJsonObject's own (graph) order");
        check(text.contains(QStringLiteral("#define NOISE_TYPE 10")),
              "define value formatted as a bare integer");
        check(text.contains(QStringLiteral("#define LOOP_OFFSET 300")),
              "second define present with correct value");

        check(!text.contains(QStringLiteral("#version 300 es")),
              "original #version line is stripped from the body");
        check(text.contains(QStringLiteral("uniform float scaleX;")),
              "body content otherwise passes through untouched (declaration)");
        check(text.contains(QStringLiteral("gl_FragColor = vec4(1.0);")),
              "body content otherwise passes through untouched (main)");
    }

    // 2) Boolean define formatting: bare `true`/`false` literals.
    {
        QJsonObject defines;
        defines.insert(QStringLiteral("WRAP"), true);
        defines.insert(QStringLiteral("RIDGES"), false);
        const QByteArray assembled = nm::assembleShader(
            QStringLiteral("#version 300 es\nvoid main(){}\n"), defines, false);
        const QString text = QString::fromUtf8(assembled);
        check(text.contains(QStringLiteral("#define WRAP true")),
              "boolean true define serializes as bare 'true' literal");
        check(text.contains(QStringLiteral("#define RIDGES false")),
              "boolean false define serializes as bare 'false' literal");
    }

    // 3) No defines at all -> header + stripped body only, no #define lines.
    {
        const QByteArray assembled = nm::assembleShader(
            QStringLiteral("#version 300 es\nvoid main(){}\n"), QJsonObject(), false);
        check(!QString::fromUtf8(assembled).contains(QStringLiteral("#define")),
              "empty defines object produces no #define lines");
    }

    // 4) Polyfill presence iff needsPackPolyfill is set.
    {
        const QString source = QStringLiteral(
            "#version 300 es\nuint pack(vec2 v){ return packHalf2x16(v); }\n");
        const QByteArray withPolyfill = nm::assembleShader(source, QJsonObject(), true);
        const QByteArray withoutPolyfill = nm::assembleShader(source, QJsonObject(), false);
        check(QString::fromUtf8(withPolyfill).contains(QStringLiteral("packHalf2x16(vec2 v)")),
              "polyfill definition present when needsPackPolyfill=true");
        check(!QString::fromUtf8(withoutPolyfill).contains(QStringLiteral("packHalf2x16(vec2 v)")),
              "polyfill definition absent when needsPackPolyfill=false");
        // The call site itself is body content and must survive either way.
        check(QString::fromUtf8(withoutPolyfill).contains(QStringLiteral("return packHalf2x16(v);")),
              "call site is untouched regardless of polyfill injection");
    }

    // 5) shaderNeedsPackHalfPolyfill: whole-word detection.
    {
        check(nm::shaderNeedsPackHalfPolyfill(QStringLiteral("uint h = packHalf2x16(v);")),
              "shaderNeedsPackHalfPolyfill true when source calls packHalf2x16");
        check(nm::shaderNeedsPackHalfPolyfill(QStringLiteral("vec2 v = unpackHalf2x16(h);")),
              "shaderNeedsPackHalfPolyfill true when source calls unpackHalf2x16");
        check(!nm::shaderNeedsPackHalfPolyfill(QStringLiteral("float x = 1.0;\nvoid main(){}\n")),
              "shaderNeedsPackHalfPolyfill false when source references neither builtin");
        check(!nm::shaderNeedsPackHalfPolyfill(QStringLiteral("float packHalf2x16Extra = 1.0;")),
              "shaderNeedsPackHalfPolyfill does not false-positive on a longer identifier");
    }

    // 6) RIDGES-style boolean-CONTEXT define: a source that declares its own
    //    `#ifndef K` + `#define K true|false` fallback must serialize K as a
    //    bare true/false literal even when the incoming JSON value is a
    //    NUMBER (1/0), not a JSON boolean -- the actual curl.frag bug this
    //    fix addresses (T6's triage: the compiled graph's RIDGES define
    //    arrives as QJsonValue::Double, not Bool, and desktop GLSL 330 core
    //    rejects `if (1)` -- "Condition must be of type bool" -- where GLSL
    //    ES/ANGLE tolerates it). Mirrors the TouchDesigner port's
    //    `_bool_define_keys`/`_truthy` precedent (td_backend.py).
    {
        const QString source = QStringLiteral(
            "#version 300 es\n"
            "#ifndef RIDGES\n"
            "#define RIDGES true\n"
            "#endif\n"
            "void main() { if (RIDGES) {} }\n");

        QJsonObject numericTrue;
        numericTrue.insert(QStringLiteral("RIDGES"), 1);
        const QString textNumericTrue = QString::fromUtf8(nm::assembleShader(source, numericTrue, false));
        check(textNumericTrue.contains(QStringLiteral("#define RIDGES true")),
              "numeric 1 for a source-declared bool-fallback key emits bare 'true'");
        check(!textNumericTrue.contains(QStringLiteral("#define RIDGES 1")),
              "numeric 1 for a source-declared bool-fallback key does NOT emit the raw integer");

        QJsonObject numericFalse;
        numericFalse.insert(QStringLiteral("RIDGES"), 0);
        const QString textNumericFalse = QString::fromUtf8(nm::assembleShader(source, numericFalse, false));
        check(textNumericFalse.contains(QStringLiteral("#define RIDGES false")),
              "numeric 0 for a source-declared bool-fallback key emits bare 'false'");
        check(!textNumericFalse.contains(QStringLiteral("#define RIDGES 0")),
              "numeric 0 for a source-declared bool-fallback key does NOT emit the raw integer");

        QJsonObject jsonTrue;
        jsonTrue.insert(QStringLiteral("RIDGES"), true);
        const QString textJsonTrue = QString::fromUtf8(nm::assembleShader(source, jsonTrue, false));
        check(textJsonTrue.contains(QStringLiteral("#define RIDGES true")),
              "genuine JSON true for a source-declared bool-fallback key still emits bare 'true'");

        QJsonObject jsonFalse;
        jsonFalse.insert(QStringLiteral("RIDGES"), false);
        const QString textJsonFalse = QString::fromUtf8(nm::assembleShader(source, jsonFalse, false));
        check(textJsonFalse.contains(QStringLiteral("#define RIDGES false")),
              "genuine JSON false for a source-declared bool-fallback key still emits bare 'false'");
    }

    // 7) A numeric define (NOISE_TYPE 10) in the same source as a bool-
    //    fallback-declared key (RIDGES) is unaffected -- the bool detection
    //    is scoped per-key to the source's own declared key set, not a
    //    blanket "any define near a #define true/false pattern" heuristic.
    {
        const QString source = QStringLiteral(
            "#version 300 es\n"
            "#ifndef RIDGES\n"
            "#define RIDGES true\n"
            "#endif\n"
            "void main() { if (RIDGES) {} }\n");

        QJsonObject defines;
        defines.insert(QStringLiteral("RIDGES"), 1);
        defines.insert(QStringLiteral("NOISE_TYPE"), 10);
        const QString text = QString::fromUtf8(nm::assembleShader(source, defines, false));
        check(text.contains(QStringLiteral("#define NOISE_TYPE 10")),
              "NOISE_TYPE (a numeric define) alongside a bool-fallback key in the same source stays a bare integer, unchanged");
        check(text.contains(QStringLiteral("#define RIDGES true")),
              "RIDGES (numeric 1, source-declared bool fallback) still converts to bare 'true' in the same pass");
    }

    // 8) A source WITHOUT any `#define K true|false` fallback pattern at all
    //    leaves EVERY define's serialization exactly as formatDefineValue()
    //    already produced it -- a genuine JSON boolean still serializes as
    //    true/false (pre-existing behavior, reconfirmed here alongside an
    //    unrelated define) and, critically, a flag-shaped numeric value
    //    (0/1) is NOT reinterpreted as a bool just because it happens to be
    //    0 or 1 -- the detection is anchored to the source's own declared
    //    key set, never inferred from a value's shape.
    {
        const QString source = QStringLiteral(
            "#version 300 es\n"
            "uniform float scaleX;\n"
            "void main() { if (OUTPUT_MODE == 1) {} }\n");

        QJsonObject defines;
        defines.insert(QStringLiteral("OUTPUT_MODE"), 1); // flag-shaped (0/1) but genuinely numeric
        defines.insert(QStringLiteral("RIDGES"), true);   // genuine JSON bool, unrelated to source text
        const QString text = QString::fromUtf8(nm::assembleShader(source, defines, false));
        check(text.contains(QStringLiteral("#define OUTPUT_MODE 1")),
              "a flag-shaped numeric define (0/1) is NOT reinterpreted as bool without a source-declared fallback for that key");
        check(text.contains(QStringLiteral("#define RIDGES true")),
              "a genuine JSON boolean define still serializes as true/false even with no source fallback pattern (pre-existing, unaffected behavior)");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_shader_assembly)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_shader_assembly)\n", g_failures);
    return 1;
}
