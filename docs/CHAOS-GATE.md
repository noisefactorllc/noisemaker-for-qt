# The Chaos Gate — this port's own finding, and how it resolved

**TL;DR.** This document inherited the godot port's own chaos-gate writeup
verbatim during scaffolding; it never described this port's actual
situation and is rewritten here to. The short version: this port had
**exactly one** fixture ever classified CHAOS (`filter/convolutionFeedback`),
its classification was investigated rather than accepted, and the
investigation found it was **not** inherent reference-engine chaos at all —
it was the same golden-minting-harness race `task-T5-report.md` (round 3)
already root-caused and fixed for agent-state surfaces, just not yet
extended to cover the render/display surface convolutionFeedback's own
feedback loop reads back. Once fixed, the fixture's golden became bit-exact
reproducible and its remaining candidate-vs-golden gap collapsed to an
ordinary sparse boundary tie. **This corpus currently has zero CHAOS
entries.** Godot's/TouchDesigner's own inherited chaos mechanisms
(transcendental-rounding-amplified-by-a-discontinuity, e.g. godot's
`points/flow` `oklab_l`/`pow` finding — real, well-evidenced, documented in
their own repos) are a genuine, recognized class this port isn't claiming
can't happen; it simply hasn't survived investigation for anything found
here yet.

---

## Round 1 — the original classification (task-T6-report.md)

`filter/convolutionFeedback` (a sharpen → blur → feedback-blend loop) was
classified CHAOS after a strict-tolerance sweep failed it badly at default
params (`sharpenAmount=2.5`): **99.97% of pixels differed** (65307/65536),
98.46% by more than 2/255, mean-abs-diff=10.7 — a whole-image divergence,
categorically different from the ~40 `tol_for()` NEAR entries (all <1.2% of
pixels, most under 0.05%). An ablation (`intensity:0`, feedback loop
neutralized) minted and rendered bit-exact-class (max-diff=1, ssim=1.00000),
proving the sharpen/blur/composite plumbing itself was correct — the
divergence was specifically the feedback amplification. This matched the
*general mechanism class* the godot port's own (inherited, unmodified at the
time) chaos-gate write-up described for agent flows, so it was filed the
same way: excluded from strict grading via `sweep.sh`'s `is_chaos()`, not
graded pixel-for-pixel at all.

This was reasonable triage under the evidence available at the time, and
explicitly flagged as unresolved rather than silently accepted:
`task-T6-report.md`'s own Concerns section lists it as a fixture that
"needs a compiler-track or renderer-track follow-up."

## Round 2 — the CHAOS classification was itself a harness bug

Re-investigating for this rewrite, rather than transcribing round 1's
conclusion: the *first* question a "chaos" verdict deserves, per this
family's own established method (`task-T5-report.md` round 2/3), is whether
the reference's own golden is even reproducible run-to-run. It hadn't been
checked for this fixture specifically. It should have been:

```
golden run1 vs run2 (same DSL, back-to-back mints): max-abs-diff=175,
mean-abs-diff=38.9, 65535/65536 pixels differ. bit-exact? False.
```

**The reference's own golden could not reproduce itself.** That is a much
more specific, more actionable signal than "chaos" — it points at the
*minting harness*, not at inherent floating-point non-determinism in the
reference engine or an unavoidable cross-GPU rounding gap in the port.

`task-T5-report.md`'s round 3 already root-caused and fixed exactly this
class of symptom for a different fixture (`physarum`/`physarumNoSense`):
`CanvasRenderer`'s always-on `requestAnimationFrame` loop free-runs on
wall-clock time through DSL-compile and the harness's own async
pause-and-settle setup (a real Playwright/CDP round-trip latency window,
not a fixed delay), silently advancing any surface the graph reads back
frame-to-frame before the "official" 8-frame protocol ever starts. The
round-3 fix clears every surface `export-and-render.mjs` classifies as
"stateful" to a known value immediately before that protocol — but its
`isStateSurface` predicate only covered the `xyz`/`vel`/`rgba`/`trail`
agent-state family, because that was the only family the physarum
investigation had exercised.

`convolutionFeedback`'s own compiled graph shows exactly the same shape of
hazard, on a surface that family never covered:

```
node_1_pass_0 cfSharpen   in={inputTex: global_o0}         out={fragColor: node_1__cfSharpened}
node_1_pass_1 cfBlur      in={inputTex: node_1__cfSharpened} out={fragColor: node_1__cfBlurred}
node_1_pass_2 cfBlend     in={inputTex: node_0_out, feedbackTex: node_1__cfBlurred} out={fragColor: node_1_out}
node_2_write_blit         in={src: node_1_out}              out={color: global_o0}
```

`cfSharpen` reads `global_o0` back as its own input; the final blit writes
`global_o0`. That is a genuine frame-to-frame feedback surface — it is just
a **render/display** surface (`o0`), not an **agent-state** surface, so the
round-3 predicate's `xyz`/`vel`/`rgba`/`trail`/`state`/`pheromone` name
patterns never matched it, and the RAF-loop race was free to silently
advance `o0` an indeterminate number of times before every mint, exactly
like it used to for physarum before that fix — producing a different
"pre-advanced" starting frame on every mint, which cascades through 8
frames of sharpen/blur/feedback amplification into a **whole different
image** each time. The 99.97%-pixels-differ, mean=10.7 signature round 1
attributed to "expansive feedback amplifying chaos" was real, but the thing
being amplified was harness jitter, not floating-point noise.

**Fix**: `export-and-render.mjs`'s `isStateSurface` predicate now also
matches `o0`-`o7` (render/display surfaces), so they get the same
pre-protocol reset as agent-state surfaces. Verified directly:

```
golden run1 vs run2, WITH the o0-o7 reset: max-abs-diff=0, mean=0.0.
bit-exact? True.
```

With a stable golden, `convolutionFeedback` was re-graded against the
existing (already-correct, never-broken) candidate:

```
[FAIL@strict] convolutionFeedback: max-abs-diff=9.000 mean-abs-diff=0.0013
ssim=1.00000 (tol=2.001, ssim_min=0.98) -- 232/65536 px differ (0.35%),
only 6 px with diff>=5.
```

Sparse, small, high-SSIM — the same shape as this file's other cross-GPU
rounding-tie entries (see `parity/sweep.sh` `tol_for()`), not a whole-image
divergence. Reclassified out of `is_chaos()` into an ordinary `tol_for()`
NEAR entry (`9.001 0.999`); the isolation ablation now lives as a real,
committed fixture (`parity/programs/convolutionFeedbackNoFeedback.dsl`,
`intensity:0`) instead of the "scratch DSL" round 1 used, so this evidence
is reproducible by anyone, not just re-derivable from a session transcript.

## What this means going forward

- **Zero CHAOS entries in this corpus today.** `reactionDiffusion` and
  `agentsPoints` — godot's other two inherited CHAOS entries — were already
  confirmed bit-exact PASSES on this backend in round 1 (never carried an
  entry here); `convolutionFeedback` — the one entry this port ever added —
  is now a NEAR pass. Copying a sibling port's CHAOS set wholesale would
  have been wrong for all three.
- **A "chaos" verdict on this port's own future findings should start with
  a golden-reproducibility check**, not end there. `synth3d/flythrough3d`'s
  own `tol_for()` entry (`sweep.sh`) is this port's closest thing to a
  genuine, structural, cross-GPU rounding chaos so far — a fractal
  distance-estimation raymarch-surface-boundary tie — and it earned that
  entry specifically *because* its golden passed the reproducibility check
  (bit-exact across two independent mints) while its candidate still showed
  a real, if sparse, divergence: both sides deterministic, genuinely
  different from each other, nothing a harness fix could close. That is the
  correct shape of evidence for a `tol_for()` (or, for something structural
  enough, a real CHAOS) entry. The check is cheap (mint twice, diff) and
  the two verdicts it distinguishes need different fixes in different
  places (harness vs. nothing-to-fix-just-tolerate), so it should run
  before either label gets applied, not after.
- **The `isStateSurface` predicate is a live, growing list, not a closed
  one.** It started scoped to agent-state surfaces (physarum), grew to
  cover render/display surfaces (convolutionFeedback) once a second fixture
  needed it, and may need a third category for a fixture not yet written.
  `parity/export-and-render.mjs`'s own inline comment on the predicate is
  the authoritative, current list — this document summarizes the story, not
  the mechanism's exact current scope.

## Reproduce

```bash
export NM_REFERENCE_ROOT=/path/to/noisemaker

# 1. Golden-reproducibility check (the first question any chaos-looking
#    verdict deserves): mint twice, diff.
node parity/export-and-render.mjs parity/programs/convolutionFeedback.dsl parity/out
cp parity/out/convolutionFeedback.golden.png /tmp/run1.png
node parity/export-and-render.mjs parity/programs/convolutionFeedback.dsl parity/out
# compare /tmp/run1.png against the freshly-overwritten parity/out/convolutionFeedback.golden.png
# -> bit-exact today (max-abs-diff=0), thanks to the o0-o7 reset above.

# 2. Ablation control (committed fixture, not a scratch DSL):
bash parity/run.sh convolutionFeedbackNoFeedback
# -> PASS, bit-exact-class (max-diff<=1, ssim=1.0) -- the plumbing was
#    always correct; only the feedback surface's pre-protocol state wasn't.

# 3. The fixture itself, at its now-correct tol_for() entry:
bash parity/run.sh convolutionFeedback 9.001 0.999
# -> PASS -- see parity/sweep.sh tol_for() for the live, current number.

# 4. This port's closest analog to genuine structural chaos, for contrast
#    (a real cross-GPU rounding tie, not a harness bug -- both sides
#    deterministic, see synth3d/flythrough3d's own tol_for() entry above):
bash parity/run.sh synth3dFlythrough3d 138.001 0.999
# -> PASS -- golden bit-exact reproducible across independent mints,
#    candidate genuinely differs at a raymarch surface boundary.
```
