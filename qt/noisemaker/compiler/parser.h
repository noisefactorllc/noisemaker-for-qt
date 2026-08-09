#pragma once

// parser.h — recursive-descent parser for the Polymorphic DSL. Port of the
// REFERENCE shaders/src/lang/parser.js (source of truth), cross-checked
// against td/noisemaker/compiler/lang/parser.py and
// godot/addons/noisemaker/compiler/lang/parser.gd. See parser.cpp for the
// PARITY-CRITICAL details (constant folding, special-form transforms,
// namespace validation, error-flag-vs-exception handling).

#include <QJsonArray>
#include <QJsonObject>

namespace nm {

// Parses a token stream (as produced by nm::lex) into a Program AST
// (QJsonObject, same node `type` strings as the reference parser.js).
// Throws nm::DslSyntaxError on malformed input, exactly where the
// reference throws.
QJsonObject parse(const QJsonArray& tokens);

} // namespace nm
