#pragma once

// validator.h -- semantic analysis: AST -> flattened plans. Port of the
// REFERENCE shaders/src/lang/validator.js (source of truth), cross-checked
// against td/noisemaker/compiler/lang/validator.py (registry-passed-in
// class shape; UnsupportedDsl fail-loud points) and
// godot/addons/noisemaker/compiler/lang/validator.gd (full argument-
// resolution fidelity -- godot implements if/elif/else, break/continue/
// return, Func/Midi/Audio/state-value args in full, since GDScript has no
// AOT-vs-interpreted distinction to enforce; this port follows TD's
// fail-loud contract at exactly those points instead -- see
// ARCHITECTURE.md "The compiler" and PORTING-GUIDE.md "Compiler porting
// rules" rule 4). See validator.cpp for the PARITY-CRITICAL details.
//
// Produces {plans, diagnostics, render, vars, searchNamespaces} where each
// plan is {chain:[step], write, write3d, final, states} and each step is
// {op, args, from, temp[, builtin]}. Diagnostics are COLLECTED (never
// thrown), matching the reference exactly, EXCEPT the family's shared
// UnsupportedDsl contract (see below), which fails loud immediately.

#include <QJsonObject>

#include <stdexcept>

namespace nm {

class EffectRegistry;

// A DSL feature this first-cut AOT frontend does not implement. Never
// silently wrong -- mirrors the sibling ports' NotImplementedException /
// UnsupportedDsl (TD's validator.py). Thrown at seven logical sites,
// exactly where the reference INTERPRETS the feature at runtime rather
// than resolving it at compile time (control flow: if/elif/else,
// break/continue/return; Func-typed boolean/numeric params; state-value
// boolean/member/numeric params). See
// validator.cpp for each site and its reference/02 section reference.
class UnsupportedDsl : public std::runtime_error {
public:
    explicit UnsupportedDsl(const QString& message) : std::runtime_error(message.toStdString()) {}
};

// Validates `ast` (as produced by nm::parse) against `registry`'s loaded
// ops/enums/aliases/starters, producing the flattened, argument-resolved
// plan structure the (future) Expander consumes. Throws nm::UnsupportedDsl
// at the family's fail-loud points (see above); all other semantic issues
// are collected into the returned object's `diagnostics` array, never
// thrown (matches the reference -- validate() has exactly one throw site
// of its own, "Missing required search directive", which nm::parse
// already guarantees can never reach here).
QJsonObject validate(const QJsonObject& ast, EffectRegistry& registry);

} // namespace nm
