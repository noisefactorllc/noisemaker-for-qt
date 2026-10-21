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
QJsonObject parse(const QJsonArray& tokens, const QJsonObject& options = {});

// Same grammar, but the returned AST keeps the parser's private compiler
// keys (Ident position/node identity, VarAssign plan index) the way the
// reference's non-enumerable JS properties travel parse -> validate.
// validate() reads them and strips them from everything it emits. Never
// dump this AST directly; use parse() for the dump shape.
QJsonObject parseKeepingPrivateKeys(const QJsonArray& tokens, const QJsonObject& options = {});

} // namespace nm
