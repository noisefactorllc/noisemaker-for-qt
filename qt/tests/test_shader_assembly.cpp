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

    if (g_failures == 0) {
        std::printf("ALL PASS (test_shader_assembly)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_shader_assembly)\n", g_failures);
    return 1;
}
