#pragma once

// tokens.h — DSL token kinds + the token record. Port of the REFERENCE
// shaders/src/lang/lexer.js RESERVED_KEYWORDS / token shape (reference/01
// §1.1), cross-checked against td/noisemaker/compiler/lang/token.py and
// godot/addons/noisemaker/compiler/lang/token.gd.
//
// Every token is {type, lexeme, line, col}:
//   type   : the token kind — kept as the SAME uppercase strings the
//            reference lexer emits (TokenType below), never an enum, so a
//            dumped token stream diffs directly against the reference's
//            JSON (parity/check_lex.mjs).
//   lexeme : the matched source substring; for STRING/FUNC it is the
//            *content* without delimiters / arrow head (the lexer strips
//            those) — escapes are NOT decoded (raw inter-delimiter text).
//   line   : 1-based line at token start.
//   col    : 1-based column at token start (per-UTF-16-code-unit via
//            QString indexing, matching JS `src[i]`; tabs count as 1).
//
// NOTE: the reference's EOF token type string is "EOF", but the bare
// identifier EOF is a <cstdio> macro (usually -1) — every reference to it
// here is spelled EOF_ to dodge textual macro substitution; the STRING
// VALUE is still exactly "EOF".

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <cmath>

namespace nm {

// Token `type` string constants, identical to the reference lexer.js's
// emitted strings (RESERVED_KEYWORDS values + the literal/punctuation/
// trivia type names it hands to `add()`).
namespace TokenType {
// literals / identifiers
inline const QString NUMBER = QStringLiteral("NUMBER");
inline const QString STRING = QStringLiteral("STRING");
inline const QString HEX = QStringLiteral("HEX");
inline const QString FUNC = QStringLiteral("FUNC");
inline const QString IDENT = QStringLiteral("IDENT");

// surface refs
inline const QString OUTPUT_REF = QStringLiteral("OUTPUT_REF");
inline const QString SOURCE_REF = QStringLiteral("SOURCE_REF");
inline const QString VOL_REF = QStringLiteral("VOL_REF");
inline const QString GEO_REF = QStringLiteral("GEO_REF");
inline const QString XYZ_REF = QStringLiteral("XYZ_REF");
inline const QString VEL_REF = QStringLiteral("VEL_REF");
inline const QString RGBA_REF = QStringLiteral("RGBA_REF");
inline const QString MESH_REF = QStringLiteral("MESH_REF");

// punctuation
inline const QString DOT = QStringLiteral("DOT");
inline const QString LPAREN = QStringLiteral("LPAREN");
inline const QString RPAREN = QStringLiteral("RPAREN");
inline const QString LBRACE = QStringLiteral("LBRACE");
inline const QString RBRACE = QStringLiteral("RBRACE");
inline const QString LBRACKET = QStringLiteral("LBRACKET");
inline const QString RBRACKET = QStringLiteral("RBRACKET");
inline const QString COMMA = QStringLiteral("COMMA");
inline const QString COLON = QStringLiteral("COLON");
inline const QString EQUAL = QStringLiteral("EQUAL");
inline const QString SEMICOLON = QStringLiteral("SEMICOLON");
inline const QString PLUS = QStringLiteral("PLUS");
inline const QString MINUS = QStringLiteral("MINUS");
inline const QString STAR = QStringLiteral("STAR");
inline const QString SLASH = QStringLiteral("SLASH");

// keywords (RESERVED_KEYWORDS — reference/01 §1.3)
inline const QString LET = QStringLiteral("LET");
inline const QString RENDER = QStringLiteral("RENDER");
inline const QString WRITE = QStringLiteral("WRITE");
inline const QString WRITE3D = QStringLiteral("WRITE3D");
inline const QString TRUE = QStringLiteral("TRUE");
inline const QString FALSE = QStringLiteral("FALSE");
inline const QString IF = QStringLiteral("IF");
inline const QString ELIF = QStringLiteral("ELIF");
inline const QString ELSE = QStringLiteral("ELSE");
inline const QString BREAK = QStringLiteral("BREAK");
inline const QString CONTINUE = QStringLiteral("CONTINUE");
inline const QString RETURN = QStringLiteral("RETURN");
inline const QString SEARCH = QStringLiteral("SEARCH");
inline const QString SUBCHAIN = QStringLiteral("SUBCHAIN");

// trivia / end
inline const QString COMMENT = QStringLiteral("COMMENT");
inline const QString EOF_ = QStringLiteral("EOF"); // identifier dodges the <cstdio> EOF macro
} // namespace TokenType

struct Token {
    QString type;
    QString lexeme;
    int line = 0;
    int col = 0;
    bool hasLine = false;
    bool hasCol = false;
    QString rawLine;
    QString rawCol;

    bool hasPosition = false;
    int posLine = 0;
    int posColumn = 0;
    int posStart = -1;
    int posEnd = -1;
};

// {type, lexeme, line, col} — matches the reference token shape exactly
// (key order is irrelevant to the parity gates, which compare key-order-
// insensitively). Tokens carrying source position attach it under "position".
inline QJsonObject toJson(const Token& t) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), t.type);
    o.insert(QStringLiteral("lexeme"), t.lexeme);
    o.insert(QStringLiteral("line"), t.line);
    o.insert(QStringLiteral("col"), t.col);
    if (t.hasPosition) {
        QJsonObject pos;
        pos.insert(QStringLiteral("line"), t.posLine);
        pos.insert(QStringLiteral("column"), t.posColumn);
        pos.insert(QStringLiteral("start"), t.posStart);
        pos.insert(QStringLiteral("end"), t.posEnd);
        o.insert(QStringLiteral("position"), pos);
    }
    return o;
}

inline Token tokenFromJson(const QJsonObject& o) {
    Token t;
    t.type = o.value(QStringLiteral("type")).toString();
    t.lexeme = o.value(QStringLiteral("lexeme")).toString();

    const bool containsLine = o.contains(QStringLiteral("line"));
    const QJsonValue lineVal = o.value(QStringLiteral("line"));
    if (containsLine && lineVal.isDouble()) {
        const double d = lineVal.toDouble();
        if (std::floor(d) == d) {
            t.line = static_cast<int>(d);
            t.hasLine = (t.line > 0);
            t.rawLine = QString::number(t.line);
        } else {
            t.hasLine = false;
            t.rawLine = QString::number(d);
        }
    } else if (containsLine && lineVal.isString()) {
        t.hasLine = false;
        t.rawLine = lineVal.toString();
    } else {
        t.hasLine = false;
        t.rawLine = containsLine && lineVal.isNull() ? QStringLiteral("null") : QStringLiteral("undefined");
    }

    const bool containsCol = o.contains(QStringLiteral("col"));
    const QJsonValue colVal = o.value(QStringLiteral("col"));
    if (containsCol && colVal.isDouble()) {
        const double d = colVal.toDouble();
        if (std::floor(d) == d) {
            t.col = static_cast<int>(d);
            t.hasCol = (t.col > 0);
            t.rawCol = QString::number(t.col);
        } else {
            t.hasCol = false;
            t.rawCol = QString::number(d);
        }
    } else if (containsCol && colVal.isString()) {
        t.hasCol = false;
        t.rawCol = colVal.toString();
    } else {
        t.hasCol = false;
        t.rawCol = containsCol && colVal.isNull() ? QStringLiteral("null") : QStringLiteral("undefined");
    }

    if (o.contains(QStringLiteral("position"))) {
        const QJsonValue posVal = o.value(QStringLiteral("position"));
        if (posVal.isObject()) {
            const QJsonObject posObj = posVal.toObject();
            const QJsonValue pLineVal = posObj.value(QStringLiteral("line"));
            const QJsonValue pColVal = posObj.value(QStringLiteral("column"));
            const QJsonValue startVal = posObj.value(QStringLiteral("start"));
            const QJsonValue endVal = posObj.value(QStringLiteral("end"));
            if (pLineVal.isDouble() && pColVal.isDouble() && startVal.isDouble() && endVal.isDouble()) {
                const int pLine = pLineVal.toInt();
                const int pCol = pColVal.toInt();
                const int pStart = startVal.toInt();
                const int pEnd = endVal.toInt();
                if (pLine > 0 && pCol > 0 && pStart >= 0 && pEnd >= pStart) {
                    t.hasPosition = true;
                    t.posLine = pLine;
                    t.posColumn = pCol;
                    t.posStart = pStart;
                    t.posEnd = pEnd;
                }
            }
        }
    }

    return t;
}

} // namespace nm
