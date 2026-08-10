# Qt Platform Notes

A distilled reference for maintainers of this port, in the sibling ports' own
`*-PLATFORM-NOTES.md` style (see `noisemaker-for-touchdesigner/docs/TD-PLATFORM-NOTES.md`
for the format this follows). Target toolchain: **Qt 6.11.1** (Homebrew,
`/opt/homebrew`), **C++17**, `find_package(Qt6 COMPONENTS Core Gui OpenGL REQUIRED)`
(`qt/CMakeLists.txt`). Referenced from `ARCHITECTURE.md`'s "Shader assembly
contract" section — this file is what that pointer resolves to.

## GLSL dialect: `#version 330 core`

The pinned dialect is the lowest desktop version whose feature set covers the
corpus (the TouchDesigner port independently established the corpus uses no
4.x-only feature) and the broadest hardware reach for Qt targets. Desktop
GLSL ≥1.30 accepts `precision` qualifiers as no-ops, and `#ifdef GL_ES` blocks
drop out on their own — so the reference's `#version 300 es` shader text
needs no rewriting beyond swapping that one header line (`assembleShader()`,
`qt/noisemaker/runtime/shader_assembly.cpp`).

macOS caps desktop OpenGL at **4.1 core** system-wide (no way around it short
of MoltenVK/ANGLE translation, which this port doesn't use) — `330 core` sits
comfortably under that ceiling with room for the one 4.20 shim below.

## The one shim: `packHalf2x16`/`unpackHalf2x16` (`filter/median` only)

`packHalf2x16`/`unpackHalf2x16` are GLSL **4.20** builtins; macOS's 4.1 cap
means they don't exist natively at `330 core`. `filter/median` is the sole
corpus user (byte-identical shader text — PORTING-GUIDE.md rule 1 — so this
is a backend-side gap, not a shader edit). When (and only when)
`shaderNeedsPackHalfPolyfill()` detects a whole-word reference to either
builtin, `assembleShader()` injects a bit-exact IEEE-binary16 polyfill (pure
uint math, round-to-nearest-even, and its exact lossless inverse) between the
injected `#define` header and the shader body.

Verified against numpy's IEEE round-to-even float16 cast: 200k+ random
`pack()` samples across the full magnitude range plus explicit edge cases
(zero, ±max normal, overflow boundary, subnormal boundary, exact-tie
mantissas, ±Inf, NaN), all 65536 possible half bit patterns for `unpack()`
exactness, and full round-trip consistency for every non-NaN half value —
zero mismatches in all three (task-T5-report.md, Episode 4 / round 1).
`median`'s own 11-pixel residual traced separately to `noise()`'s
pre-existing, already-documented 1-ULP platform difference (proven via the
`medianGradient.dsl` isolation fixture — a `gradient()` input instead of
`noise()` — landing bit-exact), not the polyfill: the polyfill itself is
exact by construction and by the above verification.

## Boolean-context `#define`s: numeric JSON value, GLSL bool condition

**The bug class.** A DSL boolean parameter with a `"define": "KEY"` field in
its `definition.js` (e.g. `curl`'s `ridges`, `render3d`/`renderCubemap3d`'s
`invert`, `noise3d`'s `ridges`) compiles to a `defines` entry that can arrive
as the **JSON number** `1`/`0` rather than the **JSON boolean** `true`/
`false` — confirmed directly against real compiled graphs (`RIDGES: 1` as a
Python `int`, `node tools/export-graph.mjs`), not merely inferred. When the
shader uses that key as a genuine bool condition (`if (RIDGES)`,
`if (INVERT)`), desktop GLSL 330 core's strict bool/int typing rejects
`if (1)` outright ("Condition must be of type bool") — a real compile
failure, not a cosmetic difference — where GLSL ES 3.00 / WebGL2-ANGLE
evidently tolerates or avoids it, so the reference's own golden renders fine
and this class is invisible until a desktop-strict compiler sees it.

**This is not qt-specific.** The TouchDesigner port hit the identical class
independently (`TD-PLATFORM-NOTES.md`: "Bare `if (NAME)` bool defines — the
real cause of the 'magenta' 3D render (a COMPILE ERROR...)"), on a different
GLSL dialect (`#version 460 core`) reached through a completely different
toolchain. Two independent strict-GLSL ports hitting the same reference
behavior independently is corroborating evidence this is a genuine
reference/GLSL-ES-leniency gap the reference's own authors have never needed
to notice, not a porting mistake specific to either backend.

**The fix** (`qt/noisemaker/runtime/shader_assembly.cpp`, `boolDefineKeys()`)
mirrors the TouchDesigner port's own proven fix
(`td/noisemaker/runtime/td_backend.py` `_bool_define_keys`/`_truthy`) with
one deliberate narrowing: TD's detector is the union of two patterns —

1. an in-shader `#define K true|false` fallback declaration (`curl.frag`'s
   own `#ifndef RIDGES` / `#define RIDGES true` / `#endif`), and
2. a bare `if (K)` / `if (!K)` condition with **no** fallback at all
   (`render3d.frag`/`renderCubemap3d.frag`'s `INVERT`,
   `synth3d/*/precompute.frag`'s `RIDGES` — the runtime is always expected
   to inject a value for these).

This port implements both (pattern 2 was added later than pattern 1 — see
git history: the curl fix shipped first covering only pattern 1, then a
re-review corpus-scan found render3d/renderCubemap3d/noise3d-precompute
needed pattern 2 too), plus TD does not implement: a ternary
`K ? a : b`/`!K ? a : b` and a logical-op `K && ...`/`K || ...` condition,
added defensively since "boolean-context" isn't only ever spelled as an
`if` — confirmed to change no current corpus behavior (nothing in the
corpus uses a define that way today) but closes the gap for a future
shader that might.

For every detector, the **scan input is comment-stripped first**
(`stripCommentsForScan()`, a minimal character-by-character state machine,
not a regex) — a hardening beyond the TouchDesigner precedent, whose own
regexes scan raw shader text and would misclassify a define mentioned only
inside a `/* */` or `//` comment. Dormant in this corpus today (curl.frag's
fallback is the only live, uncommented hit anywhere) but a real gap worth
flagging back to the family as a possible backport.

**Both detectors are deliberately loose text scans** — `boolConditionKeys()`
also matches ordinary local GLSL variables used as conditions (`bool wrap =
...; if (wrap) {...}`), which is harmless by construction:
`boolDefineKeys()`'s result is only ever consulted via
`boolKeys.contains(key)` for a key that is **also** a real entry in the
compiled graph's own `defines` object for that pass, and every actual
compile-time define name in the reference is UPPERCASE by convention
(verified against every `definition.js` "define" field in the reference:
exactly `BEHAVIOR`, `COLOR_MODE`, `DIMENSIONS`, `FILTERING`, `INVERT`,
`OCTAVES`, `RIDGES`) while local shader variables follow ordinary
camelCase/lowercase GLSL naming — so a same-file local variable never
collides with a real define name in this corpus. Verified exhaustively: an
independent Python re-implementation of the exact regex set, run against
every `.frag` file in the corpus and cross-referenced against that
authoritative 7-name list, confirms the only names that ever actually
intersect are `INVERT` and `RIDGES` — exactly the fixtures this fix targets,
nothing else changes behavior.

## MRT attachment resolution: name-based, with one alias

The reference's own WebGL2 backend resolves MRT (multiple render target)
attachments **positionally** — `Object.keys(pass.outputs)` iteration index
`N` → `COLOR_ATTACHMENT0 + N` — trusting the compiler's own JSON key order to
match each shader's declared `layout(location=N)` order. There is no
`gl.getFragDataLocation` call anywhere in the reference's MRT path.

This port resolves by **name** instead (`glGetFragDataLocation(program,
outputKey)`) — deliberately, since Qt's `QJsonObject` does not preserve JSON
text/insertion order (it always iterates in sorted-key order; see
`graph.h`'s own note), so a positional strategy would need a structural
change to `Pass::outputs`'s storage type to recover the reference's true key
order. Name-based resolution sidesteps that entirely, **provided the graph's
output key always equals the shader's GLSL out-variable name** — true for
every points/agent-family MRT shader in the corpus (`outXYZ`/`outVel`/
`outRGBA`/`outData`/`outState1-3` — verified corpus-wide, key literally
equals GLSL name every time).

**It is not true for the render3d family.** Every `synth3d/*/precompute.frag`
and every `render3d`/`renderCubemap3d`/`renderLit3d`/`renderCubemapSurface`
`.frag` declares its primary output as `layout(location = 0) out vec4
fragColor;`, while the compiled graph's key for that exact slot is
`"color"` — matching the single-output branch's own "prefer the `color` key"
convention, and `docs/GRAPH-JSON-SCHEMA.md`'s own outputs field comment,
which lists `color` and `fragColor` as separate possible names without
noting they're the same role under different effect families. A name-based
lookup for `"color"` against a program that only declares `fragColor`
returns `-1` and the pre-existing code silently dropped that attachment —
found via direct GPU-state readback (`node_0_volumeCache`, the actual
density field written by `synth3d/noise3d/precompute.frag`, stayed all-zero
while the sibling `geoOut` attachment, whose key does match its GLSL name,
populated correctly) while investigating why every synth3d/render3d fixture
rendered degenerate output despite compiling and running with zero GL
errors. Not a boolean-define bug — a separate, pre-existing MRT
name-resolution gap this investigation surfaced (`backend.cpp`
`executePass()`'s MRT branch): fixed with a narrow, corpus-verified fallback
(`"color"` not found → also try `"fragColor"`) rather than switching to
positional resolution, since exactly one alias pair exists anywhere in the
corpus and preserving true JSON order for a single alias is a much larger
change than the problem warrants.

## Sources

- This port's own build: `qt/CMakeLists.txt` (`find_package(Qt6 ...)`,
  `CMAKE_CXX_STANDARD 17`); `qmake6 --version` / `qmake --version` on this
  machine reports Qt 6.11.1 at `/opt/homebrew/lib`.
- `ARCHITECTURE.md` "Shader assembly contract" (the `#version 330 core` /
  packHalf2x16 summary this file expands on).
- `docs/GRAPH-JSON-SCHEMA.md` (the `outputs` field's `color`/`fragColor`
  naming note).
- `.superpowers/sdd/2026-08-08-qt-port/task-T5-report.md` (packHalf2x16
  verification, `median`/`noise()` 1-ULP isolation) and `task-T6-report.md`
  (curl/curlSeeded triage that first characterized the boolean-define bug).
- `noisemaker-for-touchdesigner/docs/TD-PLATFORM-NOTES.md` (the independent
  cross-port confirmation of the boolean-define bug class, and the format
  this file follows).
