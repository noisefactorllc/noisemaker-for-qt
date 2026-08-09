#pragma once

// ast.h — DSL AST node `type` strings + shape helpers. Port of the
// REFERENCE shaders/src/lang/parser.js node shapes (source of truth),
// cross-checked against td/noisemaker/compiler/lang/ast.py's NodeKind
// table and godot/addons/noisemaker/compiler/lang/parser.gd's inline
// {"type": "..."} literals.
//
// The reference parser emits plain JS objects discriminated by a `type`
// string (PORTING-GUIDE.md "Compiler porting rules": keep `type` strings
// identical -- the dump gates diff structures, and structure drift is how
// bugs hide). Per that same rule ("AST/graph nodes are QJsonObjects"),
// there is no typed C++ class hierarchy here -- AST nodes are QJsonObject/
// QJsonArray trees built directly in parser.cpp; NodeKind below supplies
// the `type` string constants, and the helpers below build the shapes
// that recur across multiple call sites (numeric constant folding,
// {line,col} locations, 2-segment enum-default members).
//
// PARITY notes (matching ast.py's, since both mirror the same reference):
//   - Number.value is a DOUBLE carrying the parse-time constant fold.
//   - Color.value is a 4-double array in 0..1; no colorspace conversion.
//   - String.value is RAW (backslash escapes not decoded; lexer.cpp).
//   - The top-level chain-statement wrapper has NO `type` key -- it is
//     identified by the presence of its `chain` key (parser.cpp
//     parseStatement's default branch).
//   - Member.path has >= 2 segments; a single segment is an Ident.

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace nm::NodeKind {

inline const QString Program = QStringLiteral("Program");
inline const QString VarAssign = QStringLiteral("VarAssign");
inline const QString IfStmt = QStringLiteral("IfStmt");
inline const QString Break = QStringLiteral("Break");
inline const QString Continue = QStringLiteral("Continue");
inline const QString Return = QStringLiteral("Return");
inline const QString Call = QStringLiteral("Call");
inline const QString Write = QStringLiteral("Write");
inline const QString Write3D = QStringLiteral("Write3D");
inline const QString Subchain = QStringLiteral("Subchain");
inline const QString Read = QStringLiteral("Read");
inline const QString Read3D = QStringLiteral("Read3D");
inline const QString Number = QStringLiteral("Number");
inline const QString String = QStringLiteral("String");
inline const QString Boolean = QStringLiteral("Boolean");
inline const QString Color = QStringLiteral("Color");
inline const QString ArrayLiteral = QStringLiteral("ArrayLiteral");
inline const QString Func = QStringLiteral("Func");
inline const QString Ident = QStringLiteral("Ident");
inline const QString Member = QStringLiteral("Member");
inline const QString Chain = QStringLiteral("Chain");
inline const QString OutputRef = QStringLiteral("OutputRef");
inline const QString SourceRef = QStringLiteral("SourceRef");
inline const QString VolRef = QStringLiteral("VolRef");
inline const QString GeoRef = QStringLiteral("GeoRef");
inline const QString XyzRef = QStringLiteral("XyzRef");
inline const QString VelRef = QStringLiteral("VelRef");
inline const QString RgbaRef = QStringLiteral("RgbaRef");
inline const QString MeshRef = QStringLiteral("MeshRef");
inline const QString Oscillator = QStringLiteral("Oscillator");
inline const QString Midi = QStringLiteral("Midi");
inline const QString Audio = QStringLiteral("Audio");

} // namespace nm::NodeKind

namespace nm::ast {

// {type:'Number', value:double} -- the parse-time constant-folded literal.
inline QJsonObject number(double value) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), NodeKind::Number);
    o.insert(QStringLiteral("value"), value);
    return o;
}

// A 2-segment Member, used for special-form defaults (oscKind.sine,
// midiMode.velocity).
inline QJsonObject memberOf(const QString& a, const QString& b) {
    QJsonObject o;
    o.insert(QStringLiteral("type"), NodeKind::Member);
    QJsonArray path;
    path.append(a);
    path.append(b);
    o.insert(QStringLiteral("path"), path);
    return o;
}

// The {line,col} source location the reference attaches to
// Write/Write3D/Subchain/Read/Read3D/Oscillator/Midi/Audio/ArrayLiteral.
inline QJsonObject loc(int line, int col) {
    QJsonObject o;
    o.insert(QStringLiteral("line"), line);
    o.insert(QStringLiteral("col"), col);
    return o;
}

} // namespace nm::ast
