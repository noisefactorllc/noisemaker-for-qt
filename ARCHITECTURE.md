# Noisemaker for Qt — Architecture

Noisemaker for Qt brings the Noisemaker Rendering Pipeline — the Polymorphic DSL compiler, the
render-graph runtime, and the full effects library — to Qt 6 as an idiomatic C++ library plus an
offscreen render CLI, with pixel-level parity gates against the reference engine.

It is a sibling of the Unity (HLSL), Godot (GDShader), TouchDesigner (GLSL), and Blender (GLSL)
ports and reuses their shared, engine-agnostic assets: the `reference/01–10` re-implementer specs,
the graph JSON contract (`docs/GRAPH-JSON-SCHEMA.md`), the parity harness design (`parity/`), and
the reference tooling (`tools/export-graph.mjs`, `tools/convert-definitions.mjs`).

## The seam: the Render Graph

Identical to every sibling port. The reference pipeline is
`lex → parse → validate → expand → allocateResources → Pipeline`; the point where everything
upstream is pure data logic and everything downstream is backend-specific is the compiled
**Render Graph**: `{passes[], programs{}, textures{}, renderSurface, id, source}`
(spec: `docs/GRAPH-JSON-SCHEMA.md`).

Two producers, one consumer:

- **Golden producer (parity only):** `tools/export-graph.mjs` runs the *unmodified* reference
  `compileGraph` from `NM_REFERENCE_ROOT`. Zero parity risk — it is literally the reference.
- **Live producer (production):** the C++ compiler in `qt/noisemaker/compiler/`
  (`nm::compileGraph(source)`), a stage-by-stage port of the reference frontend. Both producers
  must emit byte-identical graph JSON, enforced by `parity/check_graph.mjs` over the full fixture
  corpus.
- **Consumer:** `nm::Backend` (`qt/noisemaker/runtime/`), a QOpenGL executor of graph JSON.

The renderer is validated first, against golden graphs, before the live compiler exists — the
build order every sibling port converged on (see `docs/IMPLEMENTATION-PLAN.md`).

## Why classic OpenGL (QOpenGL*), not RHI/qsb or ShaderEffect

Decision record, evaluated 2026-08-08:

1. **QML `ShaderEffect`** requires shaders precompiled offline to `.qsb`. A runtime DSL compiler
   generates programs at runtime; there is no runtime qsb path in public API. Dead end.
2. **QRhi** (public since Qt 6.6) abstracts Metal/Vulkan/D3D/GL, but shader conditioning is
   likewise offline (`qsb`); `QShaderBaker` lives in QtShaderTools private API. Beyond that, RHI
   backends differ in NDC/Y conventions, so parity against the WebGL2 golden would re-import the
   exact cross-backend hazards the Godot and Unity ports had to fight — for zero parity benefit.
3. **QOpenGL (chosen).** Runtime GLSL compilation is first-class. Desktop GL shares WebGL2's
   bottom-left origin, texture conventions, and GLSL language family, so the two hardest
   cross-backend hazards of the sibling hand-ports — the per-effect Y-flip question and shader
   re-derivation — are structurally absent. `QOffscreenSurface` + FBO gives headless parity
   rendering (verified on macOS: 4.1-core Metal-backed context, byte-exact readback).

The graph seam keeps a future RHI consumer possible without touching the compiler, effects, or
parity corpus. Until Qt exposes runtime shader conditioning publicly, OpenGL is the correct
foundation, and remains a supported first-class Qt path (`QOpenGLContext`, `QOpenGLWidget`).

## Shader corpus: byte-identical reference GLSL

The reference engine ships per-effect GLSL ES 3.00 alongside WGSL; its WebGL2 backend is the
golden path. Because desktop GLSL is the same language family, **this port commits the reference
GLSL byte-identical** — `qt/noisemaker/shaders/effects/<ns>/<func>/<prog>.frag` is a verbatim copy
of the reference's `shaders/effects/<ns>/<func>/glsl/<prog>.glsl`. No hand-porting, no macro
wrapper layer. Enforced by `parity/check_shaders.mjs` (byte-diff against `NM_REFERENCE_ROOT`), the
shader-corpus analog of the definitions-freshness gate.

Dialect is resolved at load time, mirroring the reference's own `injectDefines` exactly:

```
#version 330 core            (reference: #version 300 es)
precision highp float;
precision highp int;
#define <KEY> <VALUE>        (pass.defines, values formatted identically to the reference)
<source with any #version line stripped>
```

- **`#version 330 core`** is the pinned dialect: the lowest desktop version whose feature set
  covers the corpus (the TouchDesigner port established the corpus uses no 4.x feature), and the
  broadest hardware reach for Qt targets. Desktop GLSL ≥1.30 accepts `precision` qualifiers as
  no-ops, and `#ifdef GL_ES` blocks drop out on their own.
- **One shim: `packHalf2x16`/`unpackHalf2x16`** (`filter/median` only) are GLSL 4.20 builtins on
  desktop, and macOS caps at 4.10. When (and only when) a source references them under a context
  without native support, the backend injects a bit-exact IEEE-binary16 polyfill (pure uint math,
  round-to-nearest-even) between the header and the source. Parity on the `median` fixtures gates
  its exactness. See `docs/QT-PLATFORM-NOTES.md`.

## Runtime model (`nm::Backend`)

A `QOpenGLContext` + `QOffscreenSurface` (or a host-provided context) executor mirroring the
reference WebGL2 backend:

| Concern | Behavior (= reference WebGL2 semantics) |
|---|---|
| Surfaces | `GL_RGBA16F`, linear, never sRGB; `GL_NEAREST` min/mag, `GL_CLAMP_TO_EDGE` wrap on intermediates; 3D/cubemap filter modes per texture spec |
| Effect passes | Fullscreen triangle-pair draw into an FBO; `passType:"blit"` = present-program copy |
| MRT | `glDrawBuffers` over N color attachments |
| Points / billboards | `GL_POINTS` / procedural billboard triangles, vertex shader `texelFetch`ing agent state by vertex index; additive `ONE, ONE` blend, no clear (deposits accumulate). Desktop GL requires `GL_PROGRAM_POINT_SIZE` enabled — WebGL2 has it always-on |
| Feedback / state | Ping-pong double-buffering; a `global_*` surface double-buffers iff it has a read/write hazard (same-pass read+write, or read at-or-before first write); state surfaces persist final binding across frames, display surfaces toggle |
| `repeat: N` / iterations | Intra-frame ping-pong loop (Jacobi solvers etc.) |
| Uniforms | Named uniforms, set via location lookup per active uniform — the reference's `extractUniforms` model. `synth/remap`'s `std140` UBO is kept as the reference wrote it (real UBO, `glUniformBlockBinding`) |
| Defines | Prepended above source per `injectDefines` mirror (above); program cache keyed `(effect, progName, defines)` |
| Time | `render(t)` pins normalized time, 8 settle frames for feedback graphs, 1 otherwise; `renderSamples(frames, sampleEvery)` steps `deltaTime = 1/600` for stateful sims |
| Readback | `glReadPixels` as float, bottom-up → top-down flip at the edge, `round(v*255)` clamp to 8-bit, no gamma — PNG via `QImage` |

Everything above is contract, not preference: each row is what the golden renders with, and the
parity ledger is the enforcement.

## The compiler (`qt/noisemaker/compiler/`)

A C++17 port of the reference frontend, the third mechanical re-port of this code family
(JS → C# for Unity, JS → Python for TouchDesigner, → C++ here). Stage per file, mirroring the
sibling structure 1:1 so the ports stay cross-checkable:

```
lexer.cpp → parser.cpp → validator.cpp → expander.cpp → resources.cpp
effect_registry.cpp (loads effects/**.json)   dsl_compiler.cpp (orchestration + normalize)
```

AST and graph are `QJsonObject` trees with the same `type` strings and key shapes as the
reference, so every stage byte-diffs against the reference oracles
(`parity/check_{lex,parse,validate,expand,graph,registry}.mjs`, key-order-insensitive).
Unsupported DSL surface fails loudly (`UnsupportedDsl`) exactly where the siblings fail:
`if/elif/else`, `break/continue/return`, `midi()`/`audio()` automation, `Func` params,
compute/MRT DSL pass fields.

## Effects library

`qt/noisemaker/effects/<ns>/<func>.json` — all **210** definitions, generated by
`tools/convert-definitions.mjs` from the reference and byte-gated by
`parity/check_definitions.mjs` (the definitions-freshness gate the Godot port proved necessary:
six green gates once hid 31 silently-drifted definitions because oracle and candidate both read
the same stale JSON).

Namespaces: `synth` 29 · `filter` 116 · `mixer` 15 · `classicNoisedeck` 20 · `points` 10 ·
`render` 11 · `synth3d` 7 · `filter3d` 2.

## Parity

The harness (`parity/`) is the sibling design, adopted whole: golden PNGs minted from the
reference engine's own demo page in headless Chromium (Playwright, pinned ANGLE backend, fixed
normalized time, 8-frame protocol; `parity/export-and-render.mjs`), candidates rendered by
`nm-render --batch-manifest` (one process, many fixtures), graded by `parity/compare.py`
(max-abs-diff in 8-bit units + dependency-free global SSIM), ledgered by `parity/write-ledger.py`
into `parity/ledger.json`.

- **Strict bar:** max-abs-diff ≤ 2.001 and SSIM ≥ 0.98 → PASS.
- **NEAR:** passes only a per-program, documented tolerance from `sweep.sh tol_for()` — every
  entry carries its mechanism (discontinuity ties, `pow` amplification, argmin flips). Never
  silently widened.
- **CHAOS / DEFER / TIMED:** chaotic solvers and agent flows are chaos-gated
  (`docs/CHAOS-GATE.md`); single-frame-ungateable classes deferred to the timed/evolve harness;
  stateful sims graded across timed samples.
- Mint golden + candidate together, immediately before grading — some reference effects are
  nondeterministic at the shader level; stale goldens produce false failures.

Fixture corpus (`parity/programs/*.dsl`, `parity/corpus/*.dsl`) is the shared family pool;
`parity/test_artistic_matrix.py` enforces effect×mode coverage against upstream's own mode
enumeration; `parity/test_harness_contract.py` guards the harness itself (forged results,
missing evidence, stale reports all fail).

## Reference pin

The reference (`NM_REFERENCE_ROOT`, default sibling checkout `../noisemaker`) is READ/EXECUTE-ONLY
and never vendored. The pin is a content snapshot recorded in `STATUS.md` prose; verification is
by tree content, never `git log A..B` (upstream amends/rebases in place). Golden artifacts depend
only on the reference plus capture parameters and are reusable across sibling ports.

## Integration surface

- `libnoisemaker-qt` (CMake target `noisemaker-qt`): `nm::compileGraph`, `nm::Graph`,
  `nm::Backend`, `nm::Pipeline`. Qt 6 Core/Gui/OpenGL only — no Widgets/QML dependency.
- `nm-render` (CLI): `--dsl <file> | --graph <json> | --batch-manifest <json>`, `--size`,
  `--time`, `--frames`, `--samples`, `--out <png>`, plus `--dump-{tokens,ast,validated,graph}`
  for the compiler gates. Offscreen; requires a GPU but no window session interaction.
- `examples/viewer`: a minimal `QOpenGLWidget` live view (DSL in, animated render out) — the
  smallest honest demonstration of embedding in a real Qt app.

## Staged / out of scope (matching siblings)

External-input effects (`media`, `text`, `meshLoader`, `scope`, `spectrum`, `roll`) ship their
definitions but their live input plumbing (camera/video/text-raster/mesh/audio-FFT) is staged;
the parity harness exercises their no-input fallbacks only. DSL control flow, `midi()`/`audio()`,
and compute-pass fields fail loudly as unsupported, exactly as in the Unity and TouchDesigner
ports. 3D volume effects follow the DEFER/cubemap harness posture of the TouchDesigner port.

## Repo layout

```
noisemaker-for-qt/
├── ARCHITECTURE.md PORTING-GUIDE.md README.md STATUS.md LICENSE TRADEMARK.md CODE_OF_CONDUCT.md
├── docs/            GRAPH-JSON-SCHEMA.md · IMPLEMENTATION-PLAN.md · QT-PLATFORM-NOTES.md · CHAOS-GATE.md
├── reference/       01–10 engine-agnostic re-implementer specs (shared, byte-identical family copies)
├── tools/           export-graph.mjs · convert-definitions.mjs · convert-shaders-qt.mjs · dump-*.mjs
├── qt/
│   ├── CMakeLists.txt
│   ├── noisemaker/  compiler/ · runtime/ · effects/<ns>/*.json · shaders/effects/<ns>/<func>/*.frag
│   └── tools/nm-render/
├── parity/          compare.py · sweep.sh · run.sh · run_samples.sh · write-ledger.py ·
│                    check_*.mjs · test_artistic_matrix.py · test_harness_contract.py ·
│                    programs/*.dsl · corpus/*.dsl
└── examples/viewer/
```
