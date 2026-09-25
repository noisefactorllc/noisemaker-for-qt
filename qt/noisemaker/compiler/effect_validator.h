#pragma once

// effect_validator.h -- structural validation of an effect definition against
// the grammar the runtime consumes. Port of the REFERENCE
// shaders/src/runtime/effect-validator.js validateEffectDefinition()
// (upstream GAP-003, commits ba87ffae + 9d3474df: full definition-grammar
// validation contract -- metadata, globals, passes, textures, dimension
// expressions, uniform layouts, enabledBy conditions, ui controls, and
// top-level unknown-field diagnosis, with byte-identical error strings).
//
// Contract (mirrors the reference doc comment): deterministic and
// side-effect-free. Returns a list of error strings; empty for valid input.
// Never throws for malformed/null/array/non-object containers, never mutates
// the input, never invokes lifecycle hooks, and never sorts runtime globals.
// Declaration/schema validation is distinct from shader compilation, GPU
// capability, and runtime behavior.
//
// PORT-SHAPE NOTES (documented deviations, all forced by the port's JSON
// definition grammar -- definitions ship as generated JSON under
// qt/noisemaker/effects/, produced by tools/convert-definitions.mjs):
//   - The input is a QJsonValue. The reference's Effect-class-instance and
//     subclass-constructor branches are not representable in JSON (the
//     converter would have flattened them), so every input here is treated
//     as a plain definition object and top-level unknown-field diagnosis
//     always runs.
//   - Lifecycle hooks (onInit/onUpdate/onDestroy/asyncInit) cannot be
//     encoded in JSON: the reference accepts a function there and rejects a
//     non-function. The port's valid shape is therefore "absent", and ANY
//     present hook value is reported with the reference's non-function
//     message (a hook in the JSON means the converter dropped a function
//     that the reference keeps).
//   - `starter` is accepted as a top-level key: it is port-authored
//     bootstrap metadata the converter bakes in from the reference manifest
//     (the reference derives starters at registration; its own definition
//     objects never carry the field, so its TOP_LEVEL_KEYS omits it).
//   - builtinMeshes accepts both the reference's map form
//     ({name: "path"}) and the port's generated array form
//     ([{name, path}] -- see effects/render/meshLoader.json), which the
//     converter emits for this port's mesh loader.
//
// Not wired into EffectRegistry::loadAll(): the reference ships the
// validator as an authoring/test-time surface (only its own test suite and
// the corpus gate call it), and this port mirrors that surface.

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <string>
#include <vector>

namespace nm {

// Validate a definition. `def` may be any QJsonValue shape; the error list
// is empty iff the definition satisfies the full grammar.
std::vector<std::string> validateEffectDefinition(const QJsonValue& def);

// Convenience overload for already-extracted definition objects.
std::vector<std::string> validateEffectDefinition(const QJsonObject& def);

// The std-enum table the validator resolves `member` defaults and `enum`
// paths against. Injectable so tests can use the live nm::Enums::std()
// tree (the reference imports std_enums.js directly).
void setEffectValidatorStdEnums(const QJsonObject& stdEnums);

} // namespace nm
