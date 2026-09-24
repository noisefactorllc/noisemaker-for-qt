#pragma once

// parameters.h -- host-side parameter conversion helpers shared by the live
// parameter APIs on nm::Backend (applyStepParameterValues / setUniform).
// Ports of the reference shaders/src/renderer/canvas.js
// isAutomationControlled / convertParameterForUniform and
// shaders/src/runtime/palette-expansion.js expandPalette.

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

} // namespace nm
