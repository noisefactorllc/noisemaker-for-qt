# noisemaker-for-qt: compatibility report

## 1. Source and authority revisions

Audit: 2026-09-27. Audited source: [`d54050d935a160a0478825573012ec807bcd1688`](https://github.com/noisefactorllc/noisemaker-for-qt/commit/d54050d935a160a0478825573012ec807bcd1688). Local HEAD matched remote main. The checkout was clean.
Full rendered parity remains **unverified** beyond the fixture suite on Linux llvmpipe. No release approval follows from this audit.
Graded reference content: `fa83eeabf278f1f4999c1d1fff43e2e5338b72ba`, the pin the CI sweep resolves from STATUS.md. The committed range audit proves the graded files equal `6a0af04d3c4f345ffab5e9f8e54e532216b4cdaa` (`1.0.185`).
Current upstream discovery: `7dc0f5640534855d73f8c812ca071fe6b1e09197`, published `1.0.189`. Versions `1.0.186` to `1.0.189` change `shaders/src/lang` and remain unqualified for this port.
Current served kit: `0.1.50`, source `478beb560ca58b4315cf5aa7f4564cf14756cc0b`. [Served metadata](https://kits.noisedeck.app/qt/0/deployment-meta.json). Artifact identity does not establish host qualification.
The observations below retain their original source and authority identities. They do not qualify later updates.

Daily review: 2026-09-27. Reviewed source: `15c6255fec052043ab512cf9d882c90ef0b5003e`. Upstream discovery then: `c2252f0caa66b7c5e133a2aad3328e832564b567`. The three commits above `7dc0f56` touch only tests and docs. The unqualified range is unchanged. Served kit unchanged: `0.1.50` at `478beb5`.

### Earlier source observations

Daily review: 2026-09-25. Inspected source: [`1dd0a0b49b13a9e84ecc365770a1fb83fd01edd5`](https://github.com/noisefactorllc/noisemaker-for-qt/commit/1dd0a0b49b13a9e84ecc365770a1fb83fd01edd5). Upstream discovery then: `bbdeb56c4b75cf33379766c3e87b0f5a18bcbba8`. Published authority then: `1.0.179`, source `fca611fd8f91424661d4e531d39313d24ea21134`. Served kit then: `0.1.42`, source `bd8f4b71f756ddaca9cd04c4159f0e608460517e`.

Report date: 2026-09-24. Source inspected: [`8460cfd77798d4e79d37828c16b0b29a09fbdbda`](https://github.com/noisefactorllc/noisemaker-for-qt/commit/8460cfd77798d4e79d37828c16b0b29a09fbdbda).
Full rendered parity at this SHA: **unverified**. This is not a release approval.
A later documentation-only commit does not change this tested source identity.
Any runtime, package, or authority update requires fresh evidence before this report can qualify it.

Qt 6 C++17 library, offscreen CLI, and QOpenGLWidget viewer using desktop OpenGL. Compiler structure and rendered parity are separate contracts. [Source contract](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md).

Latest status entry records upstream `5b81e04f8a4b53c2be43b8e328cee0c3365f352f`. Historical render tables refer to older authority snapshots.
Current upstream discovery SHA: `c9ee8a049b2b63cd300da67c01ee40baf29dc288`.
Published authority: `1.0.176`, source `c9ee8a049b2b63cd300da67c01ee40baf29dc288`.
[Immutable published manifest](https://shaders.noisedeck.app/1.0.176/effects/manifest.json) contains 210 effect IDs.
Its SHA-256 is `05c4d7b7744837ae90a3bb4c89e5403ff09448a74d9d7e824abb3d719ad3314e`.
These IDs do not define complete parameter, state, input, or platform coverage.

Served kit `0.1.17` records `8460cfd77798d4e79d37828c16b0b29a09fbdbda`. [Source metadata](https://kits.noisedeck.app/qt/0/deployment-meta.json).
Historical measurements remain bound to their original revisions in [completion gaps](COMPLETION_GAPS.md).

## 2. Host and distribution matrix

Current tests and qualification limits are in [section 3](#3-parity-coverage).
The matrix below retains the earlier measured scope. A historical verified row is not a current-source or full-platform certification.

| Dimension | Status | Measured scope or limit |
|---|---|---|
| Source-level checks | verified | Byte gates 317/317, 210/210, 211/211 at reference `6a0af04d`, each exit 0. Python harness tests pass 54 of 54. CI gates pass 388/388 per compiler stage at `478beb5`. |
| Actual host rendering | verified | The exact-source llvmpipe sweep grades the 362-program fixture suite, 362 strict passes, 0 skipped. macOS keeps 45 NEAR rows. This is not full host qualification. |
| Minimum and current host versions | unverified | Declared requirements are not a tested version matrix. |
| Supported operating systems and backends | unverified | CI ctest passed on ubuntu-latest, windows-latest, and macos-latest at `478beb5`. A full platform matrix remains untested. |
| Installed package and first useful result | verified | The installed-workflow CI job passed on all three OSes at `478beb5` (GAP-002). This pass adds no new host evidence. |
| Parameters, external inputs, state, and chains | unverified | Full current-authority combinations remain unmeasured. |
| Invalid input and recovery | unverified | Unit checks do not establish every installed public entry point. |
| Upgrade, removal, and resource cleanup | unverified | Prior defects and missing workflows remain in the gap register. |
| Accessibility of provided controls | unverified | Keyboard, focus, labels, and diagnostics need host observations where applicable. |
| Release readiness | blocked | Full parity, installation, host, and artifact evidence remain incomplete. |

## 3. Parity coverage

### Audit, 2026-09-27

The exact-source llvmpipe sweep (CI run 36274290692 at `478beb5`) graded the full fixture suite: 362 executed, 362 strict passes, 0 failures, 0 skips. The sweep minted the goldens from the reference at the STATUS.md pin `fa83eeab`. NM_REFERENCE_OVERLAYS=1 supplied the reference overlays.
This grades the fixture suite on Linux llvmpipe only. The macOS ledger keeps 45 NEAR rows. The reference overlays mean the port-generated overlay canvas is not graded (GAP-025).
Parameters, define choices, seeds, external inputs, sizes, chains, and stateful frames beyond the fixture set remain unmeasured.
Local byte gates at reference `6a0af04d`: SHADERS 317/317, DEFINITIONS 210/210, EFFECTS_UI 211/211, each exit 0. Python harness tests pass 54 of 54.
Upstream `1.0.186` to `1.0.189` (through `7dc0f56`) change `shaders/src/lang` and remain unqualified. [Run evidence](/series/evidence-audit-20260927-010500/result-noisemaker-for-qt.json).

### Daily review, 2026-09-27

Local byte gates re-run at `15c6255`, reference `6a0af04d`: SHADERS 317/317, DEFINITIONS 210/210, EFFECTS_UI 211/211, each exit 0. Python harness tests pass 54 of 54.
The sweep job log at `478beb5` records pin `fa83eeab`, NM_REFERENCE_OVERLAYS=1, minted=360 failed=0, renderer ANGLE (Mesa, llvmpipe), and ledger `{'PASS': 362}`. The CI gates there also pass EFFECTS_UI 211/211.
GAP-004 and GAP-022 closures re-verified against the committed raw logs and CI. The GAP-002 Wine leg and the GAP-044/045 macOS CoreText legs stay carried evidence. GAP-046 stays open.
Upstream `1.0.186` to `1.0.189` remain unqualified. The three newer upstream commits touch only tests and docs. [Run evidence](/series/review-20260927-051000/result.json).

### Daily review, 2026-09-25

Exact-source CI passes 25 C++ tests on Linux and Windows. MacOS runs 24 plus the separate device-limits test. The five compiler stages each pass 388 cases. The llvmpipe suite accepts 362 fixture programs at its existing numerical contracts. Of 360 static records, 359 have maximum difference 0 and bloom has maximum difference 1. Two further programs exercise timed samples. NM_REFERENCE_OVERLAYS=1 supplies the reference overlays for fibers, scratches, and strayHair. Those rows do not qualify the port-generated canvas. Local harness tests pass 54 of 54. Full parameter and platform parity remains unverified. [Raw evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/review-20260925-053200/qt-ci-36095317236.log).

The current full case denominator remains incomplete. Missing parameters, hosts, external inputs, and stateful sequences remain qualification gaps. No skip or tolerated difference counts as exact parity.

### Earlier measurements

Full parity requires complete applicable coverage with no skips or missing cases.
Historical NEAR, CHAOS, and tolerated differences do not count as strict equality.
The existing numerical contracts remain separate from exact comparison. This report does not change tolerances or goldens.
Unknown values mean `not measured`, never zero.

| Gate | Expected cases | Executed | Strict passes | Failures | Skips | Status |
|---|---|---|---|---|---|---|
| Fixture suite, Linux llvmpipe CI (`478beb5`) | 362 programs | 362 | 362 | 0 | 0 | verified, llvmpipe scope only |
| Fixture suite, macOS ledger | 362 programs | 362 | 317 | 0 | 0 | 45 NEAR tolerance rows, historical (`c6ab84a`) |
| Full current-authority parameter, input, and state coverage | not measured | not measured | not measured | not measured | not measured | unverified |

Earlier served compatibility inventory declares 203 effect IDs. Declaration does not establish execution or parity.
IDs absent from the served declaration: `filter/text`, `render/meshLoader`, `render/meshRender`, `synth/media`, `synth/roll`, `synth/scope`, `synth/spectrum`.
Missing effects remain visible toward the full-parity goal. Contract exclusions do not become successful tests.

These probes use current candidate sources and retained golden files. Their historical authority provenance remains unresolved in this pass.
They do not qualify the current upstream revision or full catalog. Exact comparison uses zero byte tolerance.

| Probe | Evidence | Exact comparison |
|---|---|---|
| `noise` | [Retained-golden measurement](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/qt-noise-comparison.json) | failed |

Current served declaration: 210 effect IDs. This inventory is not evidence of execution. The declaration column below reflects kit `0.1.42`.

### Effect inventory

| Effect ID | Declared in served kit | Current full parity |
|---|---|---|
| `classicNoisedeck/bitEffects` | yes | unverified |
| `classicNoisedeck/caustic` | yes | unverified |
| `classicNoisedeck/cellNoise` | yes | unverified |
| `classicNoisedeck/cellRefract` | yes | unverified |
| `classicNoisedeck/coalesce` | yes | unverified |
| `classicNoisedeck/colorLab` | yes | unverified |
| `classicNoisedeck/composite` | yes | unverified |
| `classicNoisedeck/effects` | yes | unverified |
| `classicNoisedeck/fractal` | yes | unverified |
| `classicNoisedeck/glitch` | yes | unverified |
| `classicNoisedeck/kaleido` | yes | unverified |
| `classicNoisedeck/lensDistortion` | yes | unverified |
| `classicNoisedeck/moodscape` | yes | unverified |
| `classicNoisedeck/noise` | yes | unverified |
| `classicNoisedeck/noise3d` | yes | unverified |
| `classicNoisedeck/refract` | yes | unverified |
| `classicNoisedeck/shapeMixer` | yes | unverified |
| `classicNoisedeck/shapes` | yes | unverified |
| `classicNoisedeck/shapes3d` | yes | unverified |
| `classicNoisedeck/splat` | yes | unverified |
| `filter/adjust` | yes | unverified |
| `filter/bloom` | yes | unverified |
| `filter/blur` | yes | unverified |
| `filter/bulge` | yes | unverified |
| `filter/celShading` | yes | unverified |
| `filter/channel` | yes | unverified |
| `filter/chroma` | yes | unverified |
| `filter/chromaticAberration` | yes | unverified |
| `filter/chrome` | yes | unverified |
| `filter/clouds` | yes | unverified |
| `filter/colorReplace` | yes | unverified |
| `filter/convolutionFeedback` | yes | unverified |
| `filter/corrupt` | yes | unverified |
| `filter/craquelure` | yes | unverified |
| `filter/crt` | yes | unverified |
| `filter/degauss` | yes | unverified |
| `filter/deriv` | yes | unverified |
| `filter/directionalBlur` | yes | unverified |
| `filter/dither` | yes | unverified |
| `filter/edge` | yes | unverified |
| `filter/emboss` | yes | unverified |
| `filter/extrude` | yes | unverified |
| `filter/feedback` | yes | unverified |
| `filter/fibers` | yes | unverified |
| `filter/flipMirror` | yes | unverified |
| `filter/fxaa` | yes | unverified |
| `filter/glowingEdge` | yes | unverified |
| `filter/glyphMap` | yes | unverified |
| `filter/grade` | yes | unverified |
| `filter/grain` | yes | unverified |
| `filter/grime` | yes | unverified |
| `filter/halftone` | yes | unverified |
| `filter/hatch` | yes | unverified |
| `filter/highPass` | yes | unverified |
| `filter/historicPalette` | yes | unverified |
| `filter/invert` | yes | unverified |
| `filter/lens` | yes | unverified |
| `filter/lensFlare` | yes | unverified |
| `filter/lensWarp` | yes | unverified |
| `filter/lightLeak` | yes | unverified |
| `filter/lighting` | yes | unverified |
| `filter/lowPoly` | yes | unverified |
| `filter/median` | yes | unverified |
| `filter/morphology` | yes | unverified |
| `filter/mosaicTiles` | yes | unverified |
| `filter/motionBlur` | yes | unverified |
| `filter/normalMap` | yes | unverified |
| `filter/normalize` | yes | unverified |
| `filter/octaveWarp` | yes | unverified |
| `filter/oilPaint` | yes | unverified |
| `filter/osd` | yes | unverified |
| `filter/outline` | yes | unverified |
| `filter/palette` | yes | unverified |
| `filter/parallax` | yes | unverified |
| `filter/patchwork` | yes | unverified |
| `filter/photocopy` | yes | unverified |
| `filter/pinch` | yes | unverified |
| `filter/pixelSort` | yes | unverified |
| `filter/pixels` | yes | unverified |
| `filter/plasticWrap` | yes | unverified |
| `filter/polar` | yes | unverified |
| `filter/pondRipples` | yes | unverified |
| `filter/posterize` | yes | unverified |
| `filter/prismaticAberration` | yes | unverified |
| `filter/reindex` | yes | unverified |
| `filter/relief` | yes | unverified |
| `filter/repeat` | yes | unverified |
| `filter/reverb` | yes | unverified |
| `filter/ridge` | yes | unverified |
| `filter/rotate` | yes | unverified |
| `filter/scale` | yes | unverified |
| `filter/scanlineError` | yes | unverified |
| `filter/scatter` | yes | unverified |
| `filter/scratches` | yes | unverified |
| `filter/scroll` | yes | unverified |
| `filter/seamless` | yes | unverified |
| `filter/sharpen` | yes | unverified |
| `filter/simpleAberration` | yes | unverified |
| `filter/sine` | yes | unverified |
| `filter/skew` | yes | unverified |
| `filter/smooth` | yes | unverified |
| `filter/smoothstep` | yes | unverified |
| `filter/snow` | yes | unverified |
| `filter/sobel` | yes | unverified |
| `filter/spatter` | yes | unverified |
| `filter/spinBlur` | yes | unverified |
| `filter/spiral` | yes | unverified |
| `filter/spookyTicker` | yes | unverified |
| `filter/stamp` | yes | unverified |
| `filter/step` | yes | unverified |
| `filter/stipple` | yes | unverified |
| `filter/strayHair` | yes | unverified |
| `filter/strokes` | yes | unverified |
| `filter/temporalAberration` | yes | unverified |
| `filter/tetraColorArray` | yes | unverified |
| `filter/tetraCosine` | yes | unverified |
| `filter/text` | yes | unverified |
| `filter/texture` | yes | unverified |
| `filter/threshold` | yes | unverified |
| `filter/tile` | yes | unverified |
| `filter/tint` | yes | unverified |
| `filter/translate` | yes | unverified |
| `filter/tunnel` | yes | unverified |
| `filter/unsharpMask` | yes | unverified |
| `filter/vaseline` | yes | unverified |
| `filter/vignette` | yes | unverified |
| `filter/warp` | yes | unverified |
| `filter/watercolor` | yes | unverified |
| `filter/waves` | yes | unverified |
| `filter/wind` | yes | unverified |
| `filter/wobble` | yes | unverified |
| `filter/wormhole` | yes | unverified |
| `filter/zoomBlur` | yes | unverified |
| `filter3d/flow3d` | yes | unverified |
| `filter3d/palette3d` | yes | unverified |
| `mixer/alphaMask` | yes | unverified |
| `mixer/applyMode` | yes | unverified |
| `mixer/blendMode` | yes | unverified |
| `mixer/cellSplit` | yes | unverified |
| `mixer/centerMask` | yes | unverified |
| `mixer/channelCombine` | yes | unverified |
| `mixer/distortion` | yes | unverified |
| `mixer/focusBlur` | yes | unverified |
| `mixer/mashup` | yes | unverified |
| `mixer/patternMix` | yes | unverified |
| `mixer/shadow` | yes | unverified |
| `mixer/shapeMask` | yes | unverified |
| `mixer/split` | yes | unverified |
| `mixer/thresholdMix` | yes | unverified |
| `mixer/uvRemap` | yes | unverified |
| `points/attractor` | yes | unverified |
| `points/buddhabrot` | yes | unverified |
| `points/dla` | yes | unverified |
| `points/flock` | yes | unverified |
| `points/flow` | yes | unverified |
| `points/heightGrid` | yes | unverified |
| `points/hydraulic` | yes | unverified |
| `points/lenia` | yes | unverified |
| `points/life` | yes | unverified |
| `points/physarum` | yes | unverified |
| `points/physical` | yes | unverified |
| `render/loopBegin` | yes | unverified |
| `render/loopEnd` | yes | unverified |
| `render/meshLoader` | yes | unverified |
| `render/meshRender` | yes | unverified |
| `render/pointsBillboardRender` | yes | unverified |
| `render/pointsEmit` | yes | unverified |
| `render/pointsRender` | yes | unverified |
| `render/render3d` | yes | unverified |
| `render/renderCubemap3d` | yes | unverified |
| `render/renderCubemapSurface` | yes | unverified |
| `render/renderLandscape3d` | yes | unverified |
| `render/renderLit3d` | yes | unverified |
| `synth/bitwise` | yes | unverified |
| `synth/cell` | yes | unverified |
| `synth/cellularAutomata` | yes | unverified |
| `synth/curl` | yes | unverified |
| `synth/gabor` | yes | unverified |
| `synth/gradient` | yes | unverified |
| `synth/julia` | yes | unverified |
| `synth/mandala` | yes | unverified |
| `synth/mandelbrot` | yes | unverified |
| `synth/media` | yes | unverified |
| `synth/mnca` | yes | unverified |
| `synth/modPattern` | yes | unverified |
| `synth/navierStokes` | yes | unverified |
| `synth/newton` | yes | unverified |
| `synth/noise` | yes | unverified |
| `synth/osc2d` | yes | unverified |
| `synth/pattern` | yes | unverified |
| `synth/perlin` | yes | unverified |
| `synth/polygon` | yes | unverified |
| `synth/reactionDiffusion` | yes | unverified |
| `synth/remap` | yes | unverified |
| `synth/roll` | yes | unverified |
| `synth/sacredGeometry` | yes | unverified |
| `synth/scope` | yes | unverified |
| `synth/shape` | yes | unverified |
| `synth/solid` | yes | unverified |
| `synth/spectrum` | yes | unverified |
| `synth/subdivide` | yes | unverified |
| `synth/testPattern` | yes | unverified |
| `synth3d/cell3d` | yes | unverified |
| `synth3d/cellularAutomata3d` | yes | unverified |
| `synth3d/flythrough3d` | yes | unverified |
| `synth3d/fractal3d` | yes | unverified |
| `synth3d/heightmap3d` | yes | unverified |
| `synth3d/noise3d` | yes | unverified |
| `synth3d/reactionDiffusion3d` | yes | unverified |
| `synth3d/shape3d` | yes | unverified |

### Native observations, 2026-09-24

Rebuilt Qt desktop OpenGL runtime, with 12 of 12 C++ tests passing. 1 selected fixtures rendered. Exact comparison: 0 passes and 1 differences.
The graphs and goldens are retained historical inputs. Their full authority provenance remains unresolved in this pass.
These results do not qualify current upstream parity. Exact comparison uses zero byte tolerance.
Existing tolerance-based acceptance remains separate. No tolerance or golden changed.

| Inventory | Fixtures | Executed | Exact passes | Exact differences | Not executed | Full qualification |
|---|---|---|---|---|---|---|
| Tracked program files | 347 | 1 | 0 | 1 | 346 | unverified |

Every unexecuted fixture remains visible in the [fixture inventory](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/qt-fixture-inventory.json).
Fixture counts do not prove coverage of every current effect, parameter, or stateful workflow.

| Case | Exact result | Measurement | Evidence |
|---|---|---|---|
| `noise` | failed | [FAIL] noise: max-abs-diff=1.000 mean-abs-diff=0.0000 ssim=1.00000 (tol=0.0, ssim_min=0.98) | [Raw command](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/qt-noise-comparison-command.json) |

## 4. Evidence

[Bounded test evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/qt-tests.json). [Exact-source Actions](https://github.com/noisefactorllc/noisemaker-for-qt/actions?query=head_sha%3A8460cfd77798d4e79d37828c16b0b29a09fbdbda).
[This run evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents) retains commands, exit codes, source identities, and distribution metadata.
Official ecosystem reference: [Qt 6 documentation, accessed 2026-09-24](https://doc.qt.io/qt-6/qopenglwidget.html).
Source CI, export dispatch, artifact delivery, and rendered parity are separate evidence dimensions.
A successful dispatch or unit-test summary does not establish a full rendered gate.

## 5. Open compatibility limits

Next bounded check: Retain raw acceptance evidence for the reopened closure-verification entries (GAP-017 CI leg, GAP-025, 026, 027, 034, 035, 037, 038, 041). The native mints need a macOS host with Chromium. Sync upstream `7dc0f56` (implementation job). Fix the README count (GAP-046).
See the stable entries in [completion gaps](COMPLETION_GAPS.md).

See [GAP-001 and the complete gap register](COMPLETION_GAPS.md#4-known-gaps) for evidence, dependencies, and acceptance criteria.

1. Reconcile the current authority and complete case inventory, including parameters, inputs, stateful frames, and host versions.
2. Run the existing actual-renderer suite without skip options. Record every missing, failed, refused, or timed-out case.
3. Verify installation, useful output, errors, recovery, upgrades, and removal with the actual distribution.
4. Inspect exact-source CI and retain artifact hashes. Keep unresolved qualification failed or unverified.

All eligible ports have equal priority. Full parity and zero skipped cases remain the goal.
Implementation corrections remain with the separate job. This report does not advance the parity checkpoint.

## 6. History

2026-09-27 daily review at `15c6255fec052043ab512cf9d882c90ef0b5003e`: audit audit-20260927-010500 reviewed. Byte gates and harness tests re-run. CI, sweep ledger, and served kit re-verified. GAP-004 and GAP-022 closures verified. Two evidence corrections recorded. [Run evidence](/series/review-20260927-051000/result.json).

2026-09-27 audit at `d54050d935a160a0478825573012ec807bcd1688`: exact-source CI at `478beb5` passes all three workflows. The llvmpipe sweep grades 362 of 362 strict. Local byte gates and 54 harness tests pass at reference `6a0af04d`. Added GAP-046. No closure. [Run evidence](/series/evidence-audit-20260927-010500/result-noisemaker-for-qt.json).

2026-09-25 daily review at `1dd0a0b49b13a9e84ecc365770a1fb83fd01edd5`: source freshness and bounded evidence reviewed. Open qualification limits retained. [Retained review evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/review-20260925-053200/qt-ci-36095317236.log). No new closure claimed.

| Date | Source | Result | Change |
|---|---|---|---|
| 2026-09-27 | `15c6255fec052043ab512cf9d882c90ef0b5003e` | Fixture suite verified on llvmpipe, full qualification unverified | Daily review. Evidence re-verified, two corrections, no closure change. |
| 2026-09-27 | `d54050d935a160a0478825573012ec807bcd1688` | Fixture suite verified on llvmpipe, full qualification unverified | Audit refresh. Upstream 1.0.186 to 1.0.189 recorded unqualified. GAP-046 added. |
| 2026-09-24 | `8460cfd77798d4e79d37828c16b0b29a09fbdbda` | Full qualification unverified | Created the requested maintained compatibility report. Preserved historical evidence and open gaps. |

Run: `20260924-remaining-gap-documents`. Later audits and reviews update this report with source-bound results.
