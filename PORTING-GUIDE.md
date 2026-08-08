# Porting Guide — Noisemaker for Qt

How reference behavior becomes Qt behavior in this port. Every rule here is a parity requirement,
not a style preference. The sibling ports' guides (Godot: WGSL→GDShader; TouchDesigner: GLSL→TD)
are the ancestors of this document; this port's rules are shorter because the GLSL→GLSL
same-origin path makes the two biggest cross-backend hazards — per-effect Y-flips and shader
re-derivation — structurally moot.

## Golden rules

1. **Shaders are byte-copies, never ports.** `qt/noisemaker/shaders/effects/**.frag` must be
   byte-identical to the reference's `shaders/effects/**/glsl/*.glsl`. If a shader seems to need
   an edit to run on desktop GL, the fix belongs in the backend's load-time assembly (header,
   polyfill) or is a real reference bug — never a fork of the shader text. Gate:
   `parity/check_shaders.mjs`.
2. **Definitions are generated, never hand-edited.** `tools/convert-definitions.mjs` regenerates
   `qt/noisemaker/effects/**.json`; `parity/check_definitions.mjs` byte-gates them against the
   reference. Both gates exist so drift is impossible, not merely detected.
3. **The golden is the WebGL2 path.** Cross-check numeric behavior against the reference's GLSL
   and its WebGL2 backend, never the WGSL (that's the Godot/Unity lineage). When the two backends
   of the reference disagree, WebGL2 wins here.
4. **Fail loud, never approximate.** Unsupported DSL surface raises `UnsupportedDsl` at the exact
   points the Unity/TouchDesigner frontends do. No silent fallbacks, no "close enough" paths.
5. **Never weaken a gate to make it green.** Tolerance loosenings live only in
   `parity/sweep.sh tol_for()`, per program, each with its mechanism traced in an inline comment.

## Shader assembly contract (load time)

Mirror of the reference `injectDefines`, byte-for-byte in spirit:

```text
#version 330 core\n
precision highp float;\n
precision highp int;\n
[#define KEY VALUE\n ...]        pass.defines in graph order, values formatted as the reference formats them
[packHalf2x16 polyfill]          iff source references it AND context GLSL < 4.20
<source with /^\s*#version.*$/m stripped>
```

- Do not strip `precision` statements or `#ifdef GL_ES` blocks from the body — desktop GLSL ≥1.30
  accepts precision qualifiers as no-ops and `GL_ES` is undefined on desktop.
- Define values must serialize identically to the reference graph's define values (they are part
  of the program cache key and of graph byte-parity). Booleans stay `true`/`false` literals.
- The polyfill implements IEEE binary16 pack/unpack with round-to-nearest-even in pure uint math;
  the `median` parity fixtures are its exactness gate. It must never be injected when unused.

## GL state parity rules

Each of these diverges from a desktop-GL default and silently breaks parity if missed:

| Rule | Why |
|---|---|
| `GL_PROGRAM_POINT_SIZE` enabled before agent/points passes | WebGL2 has vertex-shader point size always on; desktop GL defaults it off — every deposit renders 1px without it |
| Intermediates: `GL_NEAREST` + `GL_CLAMP_TO_EDGE`, allocated `GL_RGBA16F` | Reference sampling contract; `LINEAR` or repeat-wrap produces plausible-but-wrong pixels |
| No `GL_FRAMEBUFFER_SRGB`, ever | The whole pipeline is linear; the reference never converts |
| Deposits: `glBlendFunc(GL_ONE, GL_ONE)`, no clear between trail and deposit | Accumulation semantics; clearing resets the sim |
| `glPixelStorei(GL_PACK_ALIGNMENT, 1)` before readback | Row padding corrupts non-multiple-of-4 widths |
| Readback float, flip bottom-up→top-down once at the edge, `round(v*255)`, no gamma | Matches the reference's own PNG capture exactly; a second flip or a gamma pass shows up as instant whole-image FAIL |
| Uniform lookup by name per program, missing-uniform is silent skip | Reference `extractUniforms` semantics — graphs legitimately carry uniforms a program doesn't declare |

## Compiler porting rules

- Port stage-by-stage from the reference JS (`shaders/src/lang/`, `shaders/src/runtime/`), with
  the TouchDesigner Python (`td/noisemaker/compiler/`) and Unity C# as cross-checkable prior
  ports of the same code. Keep function boundaries and `type` strings identical — the dump gates
  diff structures, and structure drift is how bugs hide.
- AST/graph nodes are `QJsonObject`s. Numeric fidelity: JS numbers are IEEE doubles; keep
  `double` end-to-end, format like the oracle dumps (the gates compare parsed values, not text,
  so `1` vs `1.0` is fine, but `0.30000000000000004` must survive).
- Lexer disambiguation rule ORDER is parity behavior (output refs, `vol` before `vel`, hex
  literal lengths, arrow functions, triple-quoted strings). Do not "clean it up."
- Parse-time constant folding, one-`search`-per-program enforcement, and diagnostics text are all
  observable in dumps: match them.
- `define:` vs `uniform:` in a definition decides compile-time vs runtime binding. Getting this
  wrong renders every non-default mode as the default — the Godot port shipped 7 such bugs before
  its per-mode fixtures caught them. The definitions are generated (rule 2), so the only way to
  reintroduce this bug class is in the expander: port its define-collection logic exactly,
  including the sorted-by-define-name ordering.

## Per-effect verification checklist

For any effect touched by an upstream sync:

1. Regenerate: `node tools/convert-definitions.mjs && node tools/convert-shaders-qt.mjs`
2. Gates: `node parity/check_definitions.mjs && node parity/check_shaders.mjs`
3. Rebuild goldens + candidates together for the affected fixtures, then grade:
   `bash parity/run.sh <fixture>` → expect `[PASS]` (or its documented NEAR entry)
4. If a mode axis changed, confirm `parity/test_artistic_matrix.py` still covers it
5. Never commit a ledger the sweep didn't write
