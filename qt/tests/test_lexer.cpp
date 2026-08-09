// Unit tests for nm::lex (qt/noisemaker/compiler/lexer.{h,cpp}).
// Plain assert-style checks, no test framework dependency (matches
// test_graph_load.cpp / test_shader_assembly.cpp convention).
//
// Covers the T8 brief's named lexer hazards -- rule-ORDER-dependent
// disambiguation, ported from shaders/src/lang/lexer.js. Every fixture
// below was cross-checked against the reference oracle
// (NM_REFERENCE_ROOT tools/dump-tokens.mjs) before being hard-coded here,
// not derived from reading the JS alone -- see task report "hazards
// encountered" for the one case (token column immediately after a block
// comment) where a hand-traced prediction was wrong and the oracle catch
// caught it:
//   - output/source refs o0/s1 vs plain identifiers ('o'/'s' must be
//     immediately followed by a digit)
//   - vol BEFORE vel (3rd-char disambiguation) + geo/xyz (3-char prefix)
//     and rgba/mesh (4-char prefix) refs
//   - hex color literals gated to length 4/7/9 (3/6/8 hex digits) --
//     anything else falls through every rule to the final "unexpected
//     character" throw
//   - arrow-function FUNC token (() => expr), including nested-paren
//     depth tracking and the un-consumed depth-0 terminator
//   - triple-quoted (multi-line) strings vs single/double-quoted strings
//     (backslash escapes NOT decoded -- raw lexeme)
//   - 1-based line/col, including the triple-quote and block-comment
//     multi-line column-fixup paths
//
// RED (before lexer.cpp has a real definition): this binary fails to LINK
// ("undefined symbols: nm::lex(...)"). GREEN: all checks below print PASS
// and the process exits 0.

#include "../noisemaker/compiler/diagnostics.h"
#include "../noisemaker/compiler/lexer.h"
#include "../noisemaker/compiler/tokens.h"

#include <QJsonArray>
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

struct Tok {
    QString type;
    QString lexeme;
    int line;
    int col;
};

Tok at(const QJsonArray& toks, int i) {
    const QJsonObject o = toks.at(i).toObject();
    return Tok{o.value(QStringLiteral("type")).toString(), o.value(QStringLiteral("lexeme")).toString(),
               o.value(QStringLiteral("line")).toInt(), o.value(QStringLiteral("col")).toInt()};
}

bool isTok(const Tok& t, const QString& type, const QString& lexeme, int line, int col) {
    return t.type == type && t.lexeme == lexeme && t.line == line && t.col == col;
}

} // namespace

int main() {
    // --- output/source refs vs identifiers -------------------------------
    {
        const QJsonArray toks = nm::lex(QStringLiteral("o0 o12 s3 output0"));
        check(toks.size() == 5, "o0 o12 s3 output0 -> 4 tokens + EOF");
        check(isTok(at(toks, 0), nm::TokenType::OUTPUT_REF, QStringLiteral("o0"), 1, 1), "o0 -> OUTPUT_REF");
        check(isTok(at(toks, 1), nm::TokenType::OUTPUT_REF, QStringLiteral("o12"), 1, 4),
              "o12 -> OUTPUT_REF (multi-digit)");
        check(isTok(at(toks, 2), nm::TokenType::SOURCE_REF, QStringLiteral("s3"), 1, 8), "s3 -> SOURCE_REF");
        check(isTok(at(toks, 3), nm::TokenType::IDENT, QStringLiteral("output0"), 1, 11),
              "output0 -> IDENT ('o' ref rule needs 'o' immediately followed by a digit)");
    }

    // --- vol BEFORE vel (3rd-char disambiguation) + geo/xyz/rgba/mesh ----
    {
        const QJsonArray toks = nm::lex(QStringLiteral("vol0 vel1 geo2 xyz3 rgba4 mesh5 vola velvel"));
        check(isTok(at(toks, 0), nm::TokenType::VOL_REF, QStringLiteral("vol0"), 1, 1), "vol0 -> VOL_REF");
        check(isTok(at(toks, 1), nm::TokenType::VEL_REF, QStringLiteral("vel1"), 1, 6),
              "vel1 -> VEL_REF (disambiguated from vol by 3rd char)");
        check(isTok(at(toks, 2), nm::TokenType::GEO_REF, QStringLiteral("geo2"), 1, 11), "geo2 -> GEO_REF");
        check(isTok(at(toks, 3), nm::TokenType::XYZ_REF, QStringLiteral("xyz3"), 1, 16), "xyz3 -> XYZ_REF");
        check(isTok(at(toks, 4), nm::TokenType::RGBA_REF, QStringLiteral("rgba4"), 1, 21),
              "rgba4 -> RGBA_REF (4-char prefix)");
        check(isTok(at(toks, 5), nm::TokenType::MESH_REF, QStringLiteral("mesh5"), 1, 27),
              "mesh5 -> MESH_REF (4-char prefix)");
        check(isTok(at(toks, 6), nm::TokenType::IDENT, QStringLiteral("vola"), 1, 33),
              "vola -> IDENT (vol ref needs a digit right after 'vol')");
        check(isTok(at(toks, 7), nm::TokenType::IDENT, QStringLiteral("velvel"), 1, 38),
              "velvel -> IDENT (vel ref needs a digit right after 'vel')");
    }

    // --- hex color literals: only length 4/7/9 (3/6/8 hex digits) --------
    {
        const QJsonArray toks = nm::lex(QStringLiteral("#fff #ffffff #ffffffff"));
        check(toks.size() == 4, "3 hex literals -> 3 tokens + EOF");
        check(isTok(at(toks, 0), nm::TokenType::HEX, QStringLiteral("#fff"), 1, 1), "#fff (3 digits) -> HEX");
        check(isTok(at(toks, 1), nm::TokenType::HEX, QStringLiteral("#ffffff"), 1, 6), "#ffffff (6 digits) -> HEX");
        check(isTok(at(toks, 2), nm::TokenType::HEX, QStringLiteral("#ffffffff"), 1, 14),
              "#ffffffff (8 digits) -> HEX");
    }
    {
        // Invalid hex lengths (not 4/7/9) fall through every rule and hit
        // the lexer's final "unexpected character" throw (verified against
        // the reference oracle: '#ff' and '#fffff' both throw there).
        bool threw = false;
        try {
            nm::lex(QStringLiteral("#ff"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "#ff (2 digits, invalid length) throws DslSyntaxError");
    }
    {
        bool threw = false;
        try {
            nm::lex(QStringLiteral("#fffff"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "#fffff (5 digits, invalid length) throws DslSyntaxError");
    }

    // --- arrow-function FUNC token ----------------------------------------
    {
        const QJsonArray toks = nm::lex(QStringLiteral("() => sin(time)"));
        check(toks.size() == 2, "() => sin(time) -> 1 FUNC token + EOF");
        check(isTok(at(toks, 0), nm::TokenType::FUNC, QStringLiteral("sin(time)"), 1, 1),
              "() => sin(time) -> FUNC with trimmed body");
    }
    {
        // Nested parens inside the arrow body are depth-tracked; the scan
        // stops at the first depth-0 terminator (',' here) WITHOUT
        // consuming it, so it lexes as its own COMMA token afterward.
        const QJsonArray toks = nm::lex(QStringLiteral("osc(() => a + (b), 0, 1)"));
        check(isTok(at(toks, 0), nm::TokenType::IDENT, QStringLiteral("osc"), 1, 1), "osc( -> IDENT");
        check(isTok(at(toks, 1), nm::TokenType::LPAREN, QStringLiteral("("), 1, 4), "osc( -> LPAREN");
        check(isTok(at(toks, 2), nm::TokenType::FUNC, QStringLiteral("a + (b)"), 1, 5),
              "nested-paren arrow body stops at depth-0 comma");
        check(isTok(at(toks, 3), nm::TokenType::COMMA, QStringLiteral(","), 1, 18),
              "terminator comma re-lexed as its own token");
    }
    {
        // '(' not followed by ')' is not an arrow-function candidate at
        // all -- ordinary LPAREN/RPAREN punctuation.
        const QJsonArray toks = nm::lex(QStringLiteral("()"));
        check(isTok(at(toks, 0), nm::TokenType::LPAREN, QStringLiteral("("), 1, 1), "'()' without '=>' -> LPAREN");
        check(isTok(at(toks, 1), nm::TokenType::RPAREN, QStringLiteral(")"), 1, 2), "'()' without '=>' -> RPAREN");
    }

    // --- triple-quoted (multi-line) strings vs single/double quotes ------
    {
        const QJsonArray toks = nm::lex(QStringLiteral("\"\"\"triple\nline2\"\"\""));
        check(toks.size() == 2, "triple-quoted string -> 1 STRING token + EOF");
        check(at(toks, 0).type == nm::TokenType::STRING, "triple-quote -> STRING");
        check(at(toks, 0).lexeme == QStringLiteral("triple\nline2"),
              "triple-quote content keeps the embedded newline, drops the delimiters");
        check(at(toks, 0).line == 1 && at(toks, 0).col == 1, "triple-quote token starts at 1-based line 1 col 1");
        check(isTok(at(toks, 1), nm::TokenType::EOF_, QStringLiteral(""), 2, 9),
              "EOF after multi-line triple-quote lands via the col=len(lastLine)+4 fixup");
    }
    {
        // Real trailing '\n' after the closing """ advances line/col via
        // the ordinary newline rule on top of the triple-quote's own
        // internal line tracking.
        const QJsonArray toks = nm::lex(QStringLiteral("\"\"\"triple\nline2\"\"\"\n// c"));
        check(toks.size() == 3, "triple-quote then comment -> 2 tokens + EOF");
        check(isTok(at(toks, 1), nm::TokenType::COMMENT, QStringLiteral("// c"), 3, 1),
              "comment after multi-line triple-quote lands on line 3 col 1");
    }
    {
        const QJsonArray toks = nm::lex(QStringLiteral("\"double\" 'single' \"esca\\\"ped\""));
        check(isTok(at(toks, 0), nm::TokenType::STRING, QStringLiteral("double"), 1, 1), "double-quoted string");
        check(isTok(at(toks, 1), nm::TokenType::STRING, QStringLiteral("single"), 1, 10), "single-quoted string");
        check(at(toks, 2).type == nm::TokenType::STRING, "escaped-quote string -> STRING");
        check(at(toks, 2).lexeme == QStringLiteral("esca\\\"ped"),
              "backslash-escape sequence is NOT decoded -- raw lexeme keeps the backslash");
    }
    {
        bool threw = false;
        try {
            nm::lex(QStringLiteral("\"unterminated"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "unterminated single-quoted string throws DslSyntaxError");
    }
    {
        bool threw = false;
        try {
            nm::lex(QStringLiteral("\"\"\"unterminated"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "unterminated triple-quoted string throws DslSyntaxError");
    }

    // --- 1-based line/col across newlines and comments --------------------
    {
        const QJsonArray toks = nm::lex(QStringLiteral("o0\no1\n  o2"));
        check(isTok(at(toks, 0), nm::TokenType::OUTPUT_REF, QStringLiteral("o0"), 1, 1), "line 1 col 1");
        check(isTok(at(toks, 1), nm::TokenType::OUTPUT_REF, QStringLiteral("o1"), 2, 1), "line 2 col 1 (after \\n)");
        check(isTok(at(toks, 2), nm::TokenType::OUTPUT_REF, QStringLiteral("o2"), 3, 3),
              "line 3 col 3 (after 2-space indent)");
        check(isTok(at(toks, 3), nm::TokenType::EOF_, QStringLiteral(""), 3, 5),
              "EOF at 1-based line/col of end of input");
    }
    {
        const QJsonArray toks = nm::lex(QStringLiteral("// line\no0"));
        check(isTok(at(toks, 0), nm::TokenType::COMMENT, QStringLiteral("// line"), 1, 1), "line comment token");
        check(isTok(at(toks, 1), nm::TokenType::OUTPUT_REF, QStringLiteral("o0"), 2, 1),
              "token after line comment resumes at next line col 1");
    }
    {
        const QJsonArray toks = nm::lex(QStringLiteral("/* block\ncomment */o0"));
        check(isTok(at(toks, 0), nm::TokenType::COMMENT, QStringLiteral("/* block\ncomment */"), 1, 1),
              "block comment spans lines, single token");
        // Empirically verified against the reference oracle: "comment */o0"
        // is 12 chars before 'o' (c-o-m-m-e-n-t-space-*-/), so 'o0' starts
        // at col 11 -- NOT col 12, which a naive hand-trace first predicted
        // (see task report "hazards encountered").
        check(isTok(at(toks, 1), nm::TokenType::OUTPUT_REF, QStringLiteral("o0"), 2, 11),
              "token immediately after block comment tracks col correctly");
    }
    {
        bool threw = false;
        try {
            nm::lex(QStringLiteral("/* unterminated"));
        } catch (const nm::DslSyntaxError&) {
            threw = true;
        }
        check(threw, "unterminated block comment throws DslSyntaxError");
    }

    // --- numbers: leading-dot, trailing-dot-without-digit -----------------
    {
        const QJsonArray toks = nm::lex(QStringLiteral(".5 5. 5.5 007"));
        check(isTok(at(toks, 0), nm::TokenType::NUMBER, QStringLiteral(".5"), 1, 1), "leading-dot number");
        check(isTok(at(toks, 1), nm::TokenType::NUMBER, QStringLiteral("5"), 1, 4),
              "'5.' -> NUMBER '5' (dot not followed by digit is not consumed)");
        check(isTok(at(toks, 2), nm::TokenType::DOT, QStringLiteral("."), 1, 5), "'5.' -> trailing DOT token");
        check(isTok(at(toks, 3), nm::TokenType::NUMBER, QStringLiteral("5.5"), 1, 7), "5.5 -> NUMBER");
        check(isTok(at(toks, 4), nm::TokenType::NUMBER, QStringLiteral("007"), 1, 11),
              "leading zeros preserved verbatim in lexeme");
    }

    // --- every RESERVED_KEYWORDS entry, plus a non-keyword ident ----------
    {
        const QJsonArray toks = nm::lex(QStringLiteral(
            "search let render if elif else break continue return write write3d true false subchain notakeyword"));
        const QStringList expected = {
            nm::TokenType::SEARCH, nm::TokenType::LET,     nm::TokenType::RENDER,   nm::TokenType::IF,
            nm::TokenType::ELIF,   nm::TokenType::ELSE,    nm::TokenType::BREAK,    nm::TokenType::CONTINUE,
            nm::TokenType::RETURN, nm::TokenType::WRITE,   nm::TokenType::WRITE3D,  nm::TokenType::TRUE,
            nm::TokenType::FALSE,  nm::TokenType::SUBCHAIN, nm::TokenType::IDENT,
        };
        bool allMatch = toks.size() == expected.size() + 1;
        for (int i = 0; allMatch && i < expected.size(); ++i) {
            allMatch = at(toks, i).type == expected.at(i);
        }
        check(allMatch, "every RESERVED_KEYWORDS entry lexes to its keyword type; non-keyword stays IDENT");
    }

    // --- EOF sentinel -------------------------------------------------------
    {
        const QJsonArray toks = nm::lex(QStringLiteral(""));
        check(toks.size() == 1, "empty source -> EOF only");
        check(isTok(at(toks, 0), nm::TokenType::EOF_, QStringLiteral(""), 1, 1), "EOF on empty source is line 1 col 1");
    }

    if (g_failures == 0) {
        std::printf("ALL PASS (test_lexer)\n");
        return 0;
    }
    std::printf("%d FAILURE(S) (test_lexer)\n", g_failures);
    return 1;
}
