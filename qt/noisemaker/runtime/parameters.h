#pragma once

// parameters.h -- host-side parameter conversion helpers shared by the live
// parameter APIs on nm::Backend (applyStepParameterValues / setUniform).
// Ports of the reference shaders/src/renderer/canvas.js
// isAutomationControlled / convertParameterForUniform,
// shaders/src/runtime/palette-expansion.js expandPalette, and the demo
// host's (demo/shaders/lib/program-state.js) coercion of function-valued
// params.

#include <QJsonObject>
#include <QJsonValue>

namespace nm {

class Enums;

// canvas.js isAutomationControlled: a variable reference or an
// Oscillator/Midi/Audio automation descriptor (by `type` or `_ast.type`).
bool isAutomationControlled(const QJsonValue& value);

// palette-expansion.js expandPalette: maps a 1-based classicNoisedeck
// palette index to paletteOffset/paletteAmp/paletteFreq/palettePhase
// (vec3 arrays) and paletteMode (int). Empty object when out of range.
QJsonObject expandPalette(double index);

// canvas.js convertParameterForUniform: resolves enum names (when the spec
// names an enum or is a `member`), then coerces by spec.type (boolean,
// int, float, color hex/array, vec3/vec4). `spec` is the effect's
// globals[param] entry; `enums` may be null (enum names then pass through).
QJsonValue convertParameterForUniform(const QJsonValue& value, const QJsonObject& spec, const Enums* enums);

// A function-valued parameter: the compiled value of an arrow function
// (`() => expr`) or of a bare state value (`time`, `frame`, ...). The
// reference validator stores {fn}, {fn, min, max} or {fn, min, max, _ast};
// graph JSON drops fn, and no runtime calls it. True for an object that
// is not an automation value and has no keys other than fn, min, max and
// _ast (so {} and {min, max} qualify).
bool isFunctionValue(const QJsonValue& value);

// True when the reference demo host shows a control for the param
// (effect.js groupGlobalsByCategory): neither ui.control == false nor
// ui.hidden == true.
bool hasHostControl(const QJsonObject& spec);

// The value the reference demo host binds for a function-valued param with
// a host control: the control sets it through ProgramState._validateValue,
// then convertParameterForUniform converts it. float and int: the spec
// default (0 when absent), clamped to min and max; an int is then rounded.
// boolean: true. vec2, vec3, vec4 and color: the spec default, else zeros.
// Undefined for any other type, which the host leaves unchanged, and for a
// float or int whose default is not a number (no definition has one).
QJsonValue resolveFunctionValue(const QJsonObject& spec);

} // namespace nm
