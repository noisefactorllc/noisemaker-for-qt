#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace nm {

// True if `source` references packHalf2x16 or unpackHalf2x16 as a whole
// identifier (not merely as a substring of a longer name). Combined with
// "context GLSL < 4.20" — always true for our pinned `#version 330 core`
// target, since macOS caps desktop GL at 4.1 — this is the activation
// condition for the polyfill injected by assembleShader().
bool shaderNeedsPackHalfPolyfill(const QString& source);

// Assembles a load-ready shader source, mirroring the reference WebGL2
// backend's injectDefines() byte-for-byte in spirit
// (shaders/src/runtime/backends/webgl2.js ~L889-903), with the GLSL ES 3.00
// header swapped for desktop `#version 330 core` per PORTING-GUIDE.md
// "Shader assembly contract":
//
//   #version 330 core\n
//   precision highp float;\n
//   precision highp int;\n
//   [#define KEY VALUE\n ...]        one line per `defines` entry
//   [packHalf2x16 polyfill]          iff needsPackPolyfill is true
//   <source with the first #version line's text stripped (its trailing
//    newline is preserved, matching the reference's non-global regex
//    replace)>
//
// `defines` values are formatted the way the reference formats them:
// booleans as bare `true`/`false` literals, integer-valued numbers as bare
// integers.
//
// NOTE on define order: Qt's QJsonObject does not preserve insertion/parse
// order — it always iterates keys in sorted order regardless of how the
// object was built (verified empirically against this Qt build; a JS plain
// object, by contrast, preserves string-key insertion order, which is what
// the reference's own `Object.entries(defines)` relies on). This function
// iterates `defines` in the QJsonObject's own (sorted) order rather than
// re-deriving the original source-JSON-text order. This does not affect
// rendered output: shader `#define` lines here are independent scalar
// constants with no cross-references, so their relative order cannot change
// GLSL compilation behavior. It only means the assembled text does not
// byte-match the reference's own define-block ordering.
//
// Pure function; no GL state, no I/O. Unit-tested directly.
QByteArray assembleShader(const QString& source, const QJsonObject& defines, bool needsPackPolyfill);

} // namespace nm
