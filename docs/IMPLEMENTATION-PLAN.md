# Implementation Plan — Noisemaker for Qt

Staged build plan, following the phase order the Blender and Godot ports proved out: renderer
before compiler, simplest effect first, a parity gate at the end of every phase. Each phase lands
as its own commit(s); a phase is done when its gate is green, never before.

## P0 — Scaffold + de-risk

- Repo skeleton, family meta (LICENSE, TRADEMARK.md, CODE_OF_CONDUCT.md), design docs.
- Copy shared engine-agnostic assets from sibling ports byte-identical: `reference/01–10`,
  `docs/GRAPH-JSON-SCHEMA.md` (+ Qt consumer section), `docs/CHAOS-GATE.md`, `parity/compare.py`,
  `parity/programs/*.dsl`, `parity/corpus/*.dsl`, `parity/test_artistic_matrix.py`,
  `tools/export-graph.mjs`, `tools/convert-definitions.mjs` (retargeted output path only).
- De-risk (already done, 2026-08-08): offscreen 4.1-core context + runtime GLSL compile + FBO
  readback verified byte-exact on macOS/Metal via a throwaway Qt smoke program.
- **Gate:** `tools/export-graph.mjs` produces a graph for `solid.dsl` from `NM_REFERENCE_ROOT`.

## P1 — Generated corpus

- `tools/convert-shaders-qt.mjs`: verbatim copy of reference `glsl/*.frag` sources into
  `qt/noisemaker/shaders/effects/`, plus `parity/check_shaders.mjs` (byte gate).
- Run `convert-definitions.mjs` → 210 JSONs; `parity/check_definitions.mjs` (byte gate).
- **Gate:** both byte gates green; counts match the reference (210 defs / 295 programs).

## P2 — Minimal backend + Tier-1 pixel parity

- `nm::Backend` minimum: graph JSON load, single fullscreen effect pass, chained passes, named
  uniforms, defines injection, rgba16f FBOs, NEAREST/CLAMP, blit/present, PNG write.
- `nm-render --graph` (single fixture) + `--batch-manifest`.
- Tier-1 fixture set (mirrors the family's): `solid` first — the explicit highest-risk
  integration probe — then 7 more spanning uniforms/defines/multi-pass/inputs
  (`gradient`, `noise`, `adjust`, `blur`, `bloom`, `mixer blend`, `palette`).
- **Gate:** Tier-1 8/8 `[PASS]` via `parity/run.sh` (golden+candidate minted together).

## P3 — Full executor

- MRT (`glDrawBuffers`), points/billboards (agent state `texelFetch`, additive blend,
  `GL_PROGRAM_POINT_SIZE`), feedback ping-pong with the family's hazard rule, `repeat:N`
  intra-frame iteration, settle frames, `renderSamples` (dt = 1/600), `synth/remap` UBO,
  `median` polyfill, 3D-texture and cubemap surfaces to the extent the DEFER harness needs.
- **Gate:** the multi-pass/feedback/points fixtures of the Tier-1+ set pass; `navierStokes`
  timed samples pass.

## P4 — Corpus-wide sweep

- Adapt `parity/sweep.sh` (render step = one `nm-render --batch-manifest` invocation),
  `run.sh`, `run_samples.sh`, `write-ledger.py`, `tol_for()` seeded from the Godot/TD tables but
  re-derived: a NEAR is only carried here with a mechanism observed on THIS backend.
- Chaos/defer classification adopted from siblings (`reactionDiffusion`, `agentsPoints`,
  `convolutionFeedback` chaos; external-input and 3D/MRT/points singles deferred/timed).
- **Gate:** full ledger written by the sweep, **0 FAIL**; every NEAR mechanism-annotated;
  `test_harness_contract.py` (adapted to `nm-render`) green.

## P5 — Integration surface

- Library polish: install rules, `find_package(noisemaker-qt)` consumability, README quickstart.
- `examples/viewer`: minimal `QOpenGLWidget` live render of a DSL program.
- **Gate:** example builds and renders against the installed library; README commands verified.

## P6 — Live DSL compiler + compiler parity

- C++ port of lexer → parser → validator → expander → resources → orchestrator; `EffectRegistry`
  over the generated JSONs; `nm-render --dsl` and `--dump-*` modes.
- `parity/check_{lex,parse,validate,expand,graph,registry}.mjs` (oracle = reference dumps via
  `tools/dump-*.mjs`; candidate = `nm-render --dump-*`), corpus = all fixture DSLs.
- **Gate:** all six compiler gates green over the full corpus; pixel sweep re-run green with
  candidates compiled by the LIVE compiler (`--dsl`), not golden graphs.

## Crystallization

- `STATUS.md`: coverage, ledger numbers, reference content pin, known limits.
- Full re-verification pass: gates re-run from scratch, ledger re-written, spot-checked.
