#pragma once

// lexer.h — DSL tokenizer. Port of the REFERENCE shaders/src/lang/lexer.js
// (source of truth), cross-checked against
// td/noisemaker/compiler/lang/lexer.py and
// godot/addons/noisemaker/compiler/lang/lexer.gd. See lexer.cpp for the
// PARITY-CRITICAL details (rule order, disambiguation hazards).

#include <QJsonArray>
#include <QString>

namespace nm {

// Tokenizes `src` into a token stream, 1-based line/col, ending in one EOF
// token — {type, lexeme, line, col} objects, same token `type` strings as
// the reference (tokens.h TokenType). Throws nm::DslSyntaxError on
// malformed input (unterminated comment/string).
QJsonArray lex(const QString& src);

} // namespace nm
