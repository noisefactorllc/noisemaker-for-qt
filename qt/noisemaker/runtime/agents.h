#pragma once

#include <QJsonValue>
#include <QString>
#include <QStringList>

namespace nm {

// True for the two agent-scatter draw modes (reference/05 executePass /
// godot `is_points`). Every other `drawMode` (including the common empty/
// absent case) is a fullscreen-triangle effect pass.
bool isAgentDrawMode(const QString& drawMode);

// Vertex count to actually issue to glDrawArrays for an agent pass with
// `agentCount` live agents: one vertex per agent for GL_POINTS, six
// (two triangles) per agent for procedural billboard quads (reference
// webgl2.js executePass: "Billboard mode: 6 vertices per particle").
int agentVertexCount(const QString& drawMode, int agentCount);

// GL primitive mode name for an agent pass -- callers map this to the
// actual GLenum (GL_POINTS / GL_TRIANGLES); kept as a string here so this
// header stays GL-header-free like graph.h/surface.h.
QString agentPrimitiveMode(const QString& drawMode);

// Sampler names tried, in priority order, when resolving a `count:"input"`
// agent pass's live agent count from its own state-texture dimensions
// (reference webgl2.js executePass points/billboards branch: prefers
// `xyzTex` over a generic `inputTex`; TD's flow3d volume-agent variant
// additionally names its position sampler `stateTex1` -- td_backend.py
// `_build_points`: "is_volume = 'stateTex1' in p.inputs"). Every 2D points/
// billboards program in this corpus (physarum, flow, pointsRender,
// pointsBillboardRender) declares `xyzTex`, so the first entry resolves in
// practice; the later entries are cross-port-informed fallbacks for a
// drawMode:"points" program shaped differently than any current fixture.
QStringList agentStateSamplerPriority();

// reference webgl2.js `resolveBlendFactor` / godot `_blend_factor`: maps a
// GL blend-factor NAME (as it appears in a pass's `blend` array, e.g.
// `["ONE", "ONE_MINUS_SRC_ALPHA"]`) to the matching GLenum value. Returns
// the value as a plain `unsigned int` (not `GLenum`) so this header does
// not need to pull in GL headers, matching surface.h's convention; the
// numeric values are the standard OpenGL blend-factor constants and are
// ABI-stable across GL header versions. Unknown/unrecognized names fall
// back to GL_ONE, matching the reference's own fallback.
unsigned int resolveBlendFactor(const QString& name);

} // namespace nm
