# noisemaker-for-qt: completion gaps

Current compatibility matrix: [compatibility report](COMPATIBILITY.md).

## 1. Scope and source revisions

Date: 2026-09-24. Reviewed SHA: [`8460cfd77798d4e79d37828c16b0b29a09fbdbda`](https://github.com/noisefactorllc/noisemaker-for-qt/commit/8460cfd77798d4e79d37828c16b0b29a09fbdbda).
Local HEAD matched remote main before checks. The operator requested registers for all remaining eligible ports in this run.
This initial register contains bounded evidence. It is not a completed port audit or release approval.
No implementation or parity checkpoint changed. Full audits remain in the rotation.

Qt 6 C++17 library, offscreen CLI, and QOpenGLWidget viewer using desktop OpenGL. Compiler structure and rendered parity are separate contracts. [Contract](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md).

Latest status entry records upstream `5b81e04f8a4b53c2be43b8e328cee0c3365f352f`. Historical render tables refer to older authority snapshots.
Current upstream at discovery: `c9ee8a049b2b63cd300da67c01ee40baf29dc288`.
Current CPU authority: `f2eb495d70abcb74e3632e7a652a4f83e4f3b11e`.
These authority heads are review targets, not qualification results. No goldens were regenerated.

Served kit `0.1.17` identifies `8460cfd77798d4e79d37828c16b0b29a09fbdbda`. [Metadata](https://kits.noisedeck.app/qt/0/deployment-meta.json). Inventory and compatibility metadata were retrieved. Complete artifact bytes were not checked.

These document paths do not match the current publication workflow filters.
The containing commit identifies this register's publication revision. The shared run record retains commits, remote hashes, and downstream results.

## 2. Completion claims

| Claim ID | Claim source | Claimed scope | Finding | Evidence |
|---|---|---|---|---|
| CLAIM-001 | [Historical source](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/STATUS.md) | Source status reports compiler gates and historical rendered PASS, NEAR, and CHAOS classifications with effect-specific tolerances. | partial | 31 Python harness tests passed. A later rebuild passed 12 C++ tests and rendered noise. Full GPU and installed-viewer qualification remain open. |
| CLAIM-002 | [README](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md) | Human usability: installation, output, errors, and recovery | unverified | Complete installed workflows were not observed. GAP-002. |
| CLAIM-003 | [Ecosystem reference](https://doc.qt.io/qt-6/qopenglwidget.html) | Ecosystem fit and version support | partial | Source entry points were examined. Installed integration and version qualification remain open. |
| CLAIM-004 | [README](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md) | Release readiness | unverified | Metadata and CI do not replace installation of the actual artifact. GAP-003. |
| CLAIM-005 | [Exact-source Actions](https://github.com/noisefactorllc/noisemaker-for-qt/actions?query=head_sha%3A8460cfd77798d4e79d37828c16b0b29a09fbdbda) | Workflow status only | supported | [Export kit](https://github.com/noisefactorllc/noisemaker-for-qt/actions/runs/35953721777): `success`. |

## 3. Methods and evidence

Environment: macOS 26.5, Darwin arm64.
[Source SHA-256 records](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/noisemaker-for-qt-source-hashes.json) bind these checks to the reviewed revision.
[Raw command evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/qt-tests.json). [Remote evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents/noisemaker-for-qt-remote.json).

Executed command:

```sh
python3 -m unittest discover -s parity -p "test_*.py"
```

31 Python harness tests passed. A later rebuild passed 12 C++ tests and rendered noise. Full GPU and installed-viewer qualification remain open. Final exit code: 0.
No image denominator or tolerance follows from a unit-test or generated-file result.
Official reference: [Qt 6 documentation, accessed 2026-09-24](https://doc.qt.io/qt-6/qopenglwidget.html).

| Outcome | Observed scope | Remaining work |
|---|---|---|
| Installation | Instructions and metadata inspected | Install the actual artifact privately. |
| First useful output | Selected checks only | Build and install under a private prefix. Load with find_package, render DSL to PNG, run viewer --selfcheck, then test resize and teardown. |
| Host integration | Not fully exercised | Check parameters, external inputs, state, resize, and cleanup. |
| Errors and recovery | Only the selected checks above | Fail through the installed entry point, correct input, and render again. |
| Distribution | Metadata inspection | Install to a private prefix and build an independent CMake consumer. Check shader/data relocation, deployment dependencies, and removal. |
| Accessibility | Not observed | Check keyboard, focus, labels, and diagnostics for provided interfaces. |

Headless libraries do not require an editor accessibility test. Their CLI diagnostics and failure handling still require checks.
Host presence does not prove host qualification. This pass made no global installation or user-project changes.

### Native observations, 2026-09-24

Rebuilt Qt desktop OpenGL runtime, with 12 of 12 C++ tests passing. 1 selected fixtures rendered. Exact comparison: 0 passes and 1 differences.
The candidate source is the source listed in the [compatibility report](COMPATIBILITY.md#1-source-and-authority-revisions).
These probes compare retained historical goldens. They do not establish full current-authority parity.
346 of 347 tracked fixtures did not execute in this bounded pass.
[Per-case measurements](COMPATIBILITY.md#native-observations-2026-09-24) retain every difference and the unexecuted fixture inventory.
No gap closes. The next rendered gate must include all missing fixtures and resolve authority provenance without replacing goldens.

## 4. Known gaps

P1 means false completion or major correctness failure. P2 means coverage or integration uncertainty. P3 means documentation inconsistency.
These entries record missing qualification. They do not infer implementation defects from absent tests.

### GAP-001: current authority and parity qualification

- Status: open. Priority: P2. Category: verification.
- Affected scope: qt/noisemaker/, qt/tests/, examples/viewer/, parity/, STATUS.md
- Expected behavior: Reproducible evidence binds each supported claim to the port and authority revisions.
- Observed behavior: Historical rendered tables cannot qualify newer shader and compiler changes. A harness test is not a GPU or installed-package result.
- Evidence: [Historical source](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/STATUS.md) and section 3.
- Next action: Rebuild the C++ source, run ctest, then rerun compiler and image gates. Preserve NEAR, CHAOS, exclusions, and each tolerance.
- Dependencies: Resolve immutable authority inputs. Preserve historical goldens and provenance.
- Acceptance criteria: Report every applicable case, parameter choice, exclusion, error, and tolerance. Do not reduce the denominator to report success.
- Required checks: Existing compiler and rendered parity gates, with raw output and exact source hashes.
- Last verification: 2026-09-24. A full sweep graded 347 of 347 fixtures: 298 PASS, 47 NEAR, 2 FAIL, 0 CHAOS, 0 skipped. See GAP-010, GAP-011 and the sweep evidence below. Only macOS was measured.

### GAP-002: installed developer workflow qualification

- Status: open. Priority: P2. Category: usability.
- Affected scope: Public API, examples (viewer, Qt Quick), the installed CMake package, supported hosts, errors, recovery, and lifecycle.
- Expected behavior: Developers can install, produce useful output, integrate it, recover from errors, and remove the package.
- Observed behavior: On macOS the installed workflow passes: build, install, versioned find_package consumer, nm-render, the viewer and Qt Quick examples from the tree and the install, resize and teardown cycles, error recovery, relocation, and uninstall. Before this pass the viewer could not resize, the Backend had no resize, --selfcheck wrote /tmp/viewer.png, the quick start needed Ninja, find_package with a version failed, no license was installed, uninstall was undocumented, and there was no Qt Quick integration. A wrong data root still reports every effect as unknown (GAP-032).
- Evidence: Earlier pass: [README](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md), [official reference](https://doc.qt.io/qt-6/qopenglwidget.html), and section 3. This pass: a literal README run in a fresh clone at the candidate commit (exit codes, hashes, and grabs retained in the run record). A find_package(noisemaker-qt 0.1) consumer renders byte-identical to nm-render; versions 0.2 and 1.0 are refused. The uninstall leaves 0 files. ctest 22/22, including test_backend_lifecycle, test_quick_item, and test_nm_render_cli. Commits ed3eb58 to 6a70ee0. macOS arm64, Qt 6.11.1. CI run for 217ac1e: test_backend_lifecycle, test_quick_item, test_nm_render_cli, and test_export_kit_host pass on Linux, Windows, and macOS. Linux arm64 Docker (Ubuntu 24.04, Mesa llvmpipe, gcc 13.3), Qt 6.9.3: ctest 22/22, and the README installed workflow passes (build, install of 570 files, viewer and quick --selfcheck against the install, a versioned find_package consumer, uninstall to 0 files). Qt 6.4.2 builds and passes 20/22; only the text weight checks fail, because Qt before 6.9 cannot set the wght axis. The README states Qt 6.9 as the minimum, and CMake warns below it (93d00f1).
- Next action: Run the installed workflow on Windows (CI covers ctest). Fix GAP-032.
- Dependencies: GAP-032.
- Acceptance criteria: The README commands pass as written on each supported OS. A wrong data root names the directory. The minimum Qt version is stated and built.
- Required checks: ctest on all three OSes, viewer and quick --selfcheck, a consumer build against an installed prefix, and an uninstall that leaves no files.
- Last verification: 2026-09-24, macOS arm64 Qt 6.11.1; Linux arm64 Qt 6.9.3 and 6.4.2; CI on three OSes.

### GAP-003: distribution and release qualification

- Status: open. Priority: P2. Category: release.
- Affected scope: Actual artifact, dependencies, notices, version promises, and release evidence.
- Expected behavior: The delivered artifact supports its documented installation and first useful result.
- Observed behavior: Complete artifact reproduction, installation, upgrade, and removal remain unverified.
- Evidence: [Distribution instructions](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md), section 1, and exact-source CI in section 2.
- Next action: Install to a private prefix and build an independent CMake consumer. Check shader/data relocation, deployment dependencies, and removal.
- Dependencies: Complete GAP-002 for the candidate. Distinguish source CI from downstream publication and native rendering.
- Acceptance criteria: Match artifact bytes to their inventory. Check notices and dependencies. Pass installation, examples, upgrade, and removal.
- Required checks: Inspect exact-source CI jobs and actual render legs. Count skips and errors rather than trusting green summaries.
- Last verification: 2026-09-24. This register does not approve a release.

### GAP-004: embedded CMake build pollutes the host project

- Status: closed. Priority: P2. Category: ecosystem.
- Affected scope: qt/CMakeLists.txt, qt/tests/CMakeLists.txt, qt/cmake/noisemaker-qt-config.cmake.in, EffectRegistry::defaultDataRoot().
- Expected behavior: add_subdirectory or FetchContent builds only the library. The host can locate the data tree without a working-directory assumption.
- Observed behavior: Before the fix, the embedded build always built nm-render and the tests, and called enable_testing(). Data lookup was working-directory-relative.
- Evidence: NM_QT_BUILD_TOOLS, NM_QT_BUILD_TESTS and NM_QT_INSTALL default to PROJECT_IS_TOP_LEVEL. NOISEMAKER_QT_DATA_ROOT names the data tree. Checks are listed below.
- Next action: None. Keep the throwaway consumer check in future CI.
- Dependencies: None.
- Acceptance criteria: Top-level build builds nm-render and passes ctest. An embedded consumer with default options has no tool or test targets, links, and renders.
- Required checks: Top-level configure, build, and ctest. Embedded consumer configure, build, ctest, and run from an unrelated working directory. Installed consumer build and run.
- Last verification: 2026-09-24, macOS 26.5 arm64, Qt 6 from /opt/homebrew/opt/qt, CMake 4.4.3.

<details><summary>GAP-004 evidence</summary>

- Fresh top-level build: `cmake -S qt -B <scratch>/build-top -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt -DCMAKE_BUILD_TYPE=Release` exit 0. `cmake --build` exit 0, nm-render built. `ctest --test-dir <scratch>/build-top` exit 0, 12 of 12 passed.
- Embedded consumer, default options: a throwaway project under /private/tmp used `add_subdirectory(qt)`. Configure exit 0 without nm-render or test_lexer targets. Its ctest listed 1 test only, its own.
- The consumer compiled `search synth` / `noise().write(o0)` / `render(o0)` against `NOISEMAKER_QT_DATA_ROOT`. It rendered 64x64 from working directory /private/tmp. Exit 0. Pixel (10,10) = `ff3d837b`.
- Embedded consumer with `NM_QT_BUILD_TESTS=ON NM_QT_BUILD_TOOLS=ON`: build exit 0. ctest 13 of 13 passed (12 port tests and the consumer test).
- FetchContent consumer: `FetchContent_Declare(noisemaker_qt GIT_REPOSITORY <this checkout> GIT_TAG 6bfda22 SOURCE_SUBDIR qt)`. Configure and build exit 0, no tool or test targets. ctest 1 of 1. Pixel `ff3d837b`.
- Installed consumer: `cmake --install` exit 0. `find_package(noisemaker-qt CONFIG)` exposed the same target, variable, and property names. The same render produced pixel `ff3d837b`. Exit 0.

</details>

### GAP-005: compiler warnings break hosts that build with warnings as errors

- Status: closed. Priority: P2. Category: ecosystem.
- Affected scope: qt/noisemaker/compiler/lexer.cpp, validator.cpp, expander.cpp, dsl_compiler.cpp, qt/tests/test_output_runtime.cpp.
- Expected behavior: The library and tests build without warnings under `-Wall -Wextra -Wpedantic` (Clang, GCC) and `/W4` (MSVC).
- Observed behavior: Apple Clang reported 8 warnings. Causes were unused functions, an unused field, unused parameters, and partial aggregate initializers.
- Evidence: Unused code was removed, not suppressed. Token and GpuSurface initializers now set every field. Checks are listed below.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: Zero warnings with warnings as errors on macOS Clang, Linux GCC, and Windows MSVC `/W4 /WX`.
- Required checks: Warnings-as-errors build of the library, nm-render, and tests. ctest and the compiler gates on the same source.
- Last verification: 2026-09-24. Local Apple Clang passes. CI run 35968600167 (`3a4f421`): Linux GCC passed. The macOS job failed to link (AGL, a Qt packaging issue), and MSVC failed at configure (Git Bash rewrote the flags). Both fixes are unverified. CI run 35974199816 (`12c1d1c`): warnings-as-errors passed on macos-latest, ubuntu-latest and windows-latest (MSVC `/W4 /WX`, after the C4456 rename in `49131bd`); build-test ctest 15/15 and gates passed on the same SHA.

<details><summary>GAP-005 evidence</summary>

- Before: `cmake --build` with `-DCMAKE_CXX_FLAGS="-Wall -Wextra -Wpedantic"` (Debug) listed 8 warnings: lexer.cpp:89 and :424, expander.cpp:324 and :621, validator.cpp:238 and :271, dsl_compiler.cpp:144, test_output_runtime.cpp:66.
- After: the same Debug build reported 0 warnings. A Release build with `-Wall -Wextra -Wpedantic -Werror` exited 0. Its ctest passed 12 of 12.
- `qt/build` rebuild exit 0, ctest 12 of 12. Compiler gates, each exit 0: SHADERS 317/317, DEFINITIONS 210/210, REGISTRY 5/5, LEX 357/357, PARSE 357/357, VALIDATE 357/357, EXPAND 357/357, GRAPH 357/357.

</details>

### GAP-006: no CI builds or tests the library

- Status: closed. Priority: P2. Category: verification.
- Affected scope: .github/workflows/ci.yml, all C++ sources, parity/check_*.mjs, parity/run.sh.
- Expected behavior: Every push builds and tests the library on macOS, Linux, and Windows. The compiler gates and a render smoke run against the reference pin.
- Observed behavior: Before this change, the only workflow was export-kit.yml. macOS was the only platform ever built.
- Evidence: ci.yml has jobs build-test, warnings, gates, and render-smoke on GitHub-hosted runners. Actions are pinned by commit SHA. The Windows Mesa archive is pinned by SHA-256.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: All four jobs pass on the pushed SHA. Linux and Windows ctest report 13 of 13 on llvmpipe. Smoke results stay at the strict tolerance.
- Required checks: `gh run view` job logs for the exact SHA. Count skips and failures in the logs; do not rely on the summary.
- Last verification: 2026-09-24. CI run 35968600167 on `3a4f421`: Linux build-test, gates, render-smoke and Linux warnings passed. macOS and Windows failed; fixes pending a run. CI run 35974199816 (`12c1d1c`): all eight jobs passed. build-test ctest 15/15 on ubuntu-latest (llvmpipe), windows-latest (Mesa opengl32sw) and macos-latest, none skipped or not run. Gates: shaders 317/317, definitions 210/210, registry 5/5, lex/parse/validate/expand/graph 358/358, MIDI_STATE 66/66, AUDIO_STATE 93/93, AUDIO_ANALYZER 570/570. Render smoke PASS at the strict tolerance (max diff 1, tol 2.001).

<details><summary>GAP-006 evidence</summary>

- `actionlint .github/workflows/ci.yml` (1.7.12): exit 0.
- Gates job, reproduced locally: the STATUS.md pin parse gave `5b81e04f`. A clean `git clone --filter=blob:none` checked out `5b81e04f8a4b53c2be43b8e328cee0c3365f352f` with no npm install. All 8 gates exited 0: 317/317, 210/210, 5/5, and 357/357 for the other 5.
- Build-test and warnings jobs: the macOS commands passed locally (ctest 13 of 13; `-Werror` build with 0 warnings). The Linux Xvfb/llvmpipe and Windows Mesa opengl32.dll paths are untested.
- The Windows /W4 /WX build is untested. GAP-005 depends on it.
- First remote run, 35968600167 (`3a4f421e351e76e28e4bdb58e19f38a96c8d9c7d`, pushed by the operator session):
  - build-test (ubuntu-latest): renderer `llvmpipe (LLVM 20.1.2, 256 bits)`, OpenGL 4.5 core, Mesa 25.2.8. ctest: 14 of 14 passed.
  - gates: all 8 passed. SHADERS 317/317, DEFINITIONS 210/210, REGISTRY 5/5, and 357/357 for LEX, PARSE, VALIDATE, EXPAND and GRAPH.
  - render-smoke: adjust, blendMode, cell and feedback passed at 2.001 / 0.98. Each had max diff 1 against goldens from Chromium in CI.
  - warnings-as-errors (ubuntu-latest): passed.
  - build-test and warnings (macos-latest): link failed with `ld: framework 'AGL' not found` under Qt 6.8.3. Fix: Qt 6.11.1, the locally verified version.
  - build-test (windows-latest): the GL tests failed ("OpenGL 4.1 functions initialize" FAIL, then 0xc0000409). Qt loads desktop opengl32.dll from System32 only. Fix: Mesa installed as opengl32sw.dll and QT_OPENGL=software.
  - warnings (windows-latest): configure failed because Git Bash rewrote the /W4-style flags. Fix: MSYS_NO_PATHCONV=1.

</details>

### GAP-007: host-supplied external textures

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: tools/convert-definitions.mjs, synth/media, filter/text, nm::Backend, qt/tests/test_host_inputs.cpp.
- Expected behavior: A host supplies the texture for `imageTex_step_N` and `textTex_step_N`, as the reference `updateTextureFromSource` does.
- Observed behavior: Before the fix, the converter dropped `externalTexture`. Inputs bound to unaddressable node ids. The Backend had no upload API.
- Evidence: The converter keeps `externalTexture`. The Backend has `updateTextureFromSource` (QImage and raw RGBA8), `setExternalTexture` (host GL id), `removeExternalTexture`, and `externalTextureIds`. The kit's media() output with the reference testcard matches a reference golden at 256x256 with max difference 0. test_text_texture checks the textTex upload, orientation and matte through a compiled graph (ccf0969, ebb21bf).
- Next action: None. The export kit supplies media and text: `--media FILE` uploads an image with flipY false and sets imageSize; nm::updateTextTextures draws filter/text on the CPU and uploads it with flipY true.
- Dependencies: GAP-006 for platforms other than macOS.
- Acceptance criteria: Media renders an uploaded image upright with flipY false. flipY true reverses rows. The raw stride, GL id, removal, text, and release paths pass.
- Required checks: test_host_inputs pixel checks, ctest, and the compiler gates.
- Last verification: 2026-09-24, macOS. ctest 17/17. check_text_canvas 10/10.

### GAP-008: live parameter updates without a recompile

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: nm::Pass (stepIndex, inheritsVolumeSize, scopedParams), nm::Backend, qt/noisemaker/runtime/parameters.{h,cpp}.
- Expected behavior: A host updates step parameters and global uniforms of a compiled graph, as reference applyStepParameterValues and Pipeline.setUniform do.
- Observed behavior: Before the fix, the only update path was a full recompile. The graph loader dropped stepIndex and scopedParams.
- Evidence: `applyStepParameterValues` and `setUniform` port the reference rules, palette expansion, and convertParameterForUniform. The feedback surface persisted across a live update and a same-structure recompile.
- Next action: None for live parameters. Correction (2026-09-24): reference checkAsyncRegen and asyncInit are not ported, and three effects do use them (fibers, scratches, strayHair); tracked as GAP-025.
- Dependencies: GAP-006 for platforms other than macOS.
- Acceptance criteria: Pixel checks show updated colors without a recompile. Automation values stay intact. The live update matches the recompiled swap byte for byte.
- Required checks: test_host_inputs, ctest, and the compiler gates.
- Last verification: 2026-09-24, macOS.

### GAP-009: engine deltaTime and frame globals were not bound

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: nm::Backend engine uniforms. Readers: synth/cellularAutomata, synth/mnca, synth/roll, points/dla.
- Expected behavior: deltaTime and frame follow reference Pipeline.render and updateGlobalUniforms.
- Observed behavior: Before the fix, both uniforms were never set, so the shaders read 0.
- Evidence: The Backend computes deltaTime with the reference rules (first frame 0, wrap 1/600) and counts frames. syncTime and normalizedLoopTime were added.
- Next action: Measure timed-sample parity for cellularAutomata and mnca in the GAP-001 sweep.
- Dependencies: GAP-001 sweep for rendered evidence.
- Acceptance criteria: Engine values captured during render match the reference rules for first, normal, wrapped, and synced frames.
- Required checks: test_host_inputs. Fixed-time fixture renders must not change.
- Last verification: 2026-09-24, macOS. cellularAutomata, mnca, scroll, and text fixture renders were byte-identical before and after.

<details><summary>GAP-007, GAP-008 and GAP-009 evidence</summary>

- `ctest --test-dir qt/build`: 13 of 13 passed. The new test_host_inputs executable reported 40 PASS and 0 FAIL.
- Media upright check: flipY false gave top (255,0,0,255) and bottom (0,0,255,255) at 64x64. Text check: black input became (255,255,255) with a white textTex.
- Feedback check: after 4 red frames, a blue swap gave (119,0,128). A fresh Backend gave (0,0,128). The live update gave identical bytes.
- Release build with `-Wall -Wextra -Wpedantic -Werror`: exit 0, 0 warnings, ctest 13 of 13.
- Compiler gates, each exit 0: SHADERS 317/317, DEFINITIONS 210/210, REGISTRY 5/5, LEX, PARSE, VALIDATE, EXPAND and GRAPH 357/357.
- `python -m unittest discover -s parity -p "test_*.py"`: 31 tests OK.
- Fixed-time renders (256x256, t 0.25, 8 frames) of cellularAutomata, mnca, scroll and text: pre-change and post-change nm-render PNGs were byte-identical (`cmp`).

</details>

### GAP-010: heightGrid_billboard_alpha renders wrong

- Status: closed. Priority: P1. Category: verification.
- Affected scope: parity/export-and-render.mjs and parity/batch-golden.mjs (golden minting). Fixture: parity/programs/heightGrid_billboard_alpha.dsl. The render/pointsBillboardRender alpha path in qt/noisemaker/runtime was not at fault.
- Expected behavior: The candidate matches the reference golden at the strict tolerance 2.001 / SSIM 0.98.
- Observed behavior: The recorded golden was the demo's default filter/adjust program, not the fixture. Both minters passed the waitForFunction options object in the page-argument slot. The DSL-swap wait then returned true on its first poll, and a slow compile (about 130 ms here) was captured from the previous program. With the fix, the fixture passes bit-exact: max diff 0, SSIM 1.00000.
- Evidence: The old-harness golden equals a mint of the default DSL byte for byte. An NM_DUMP_INTERMEDIATES mint equals the Qt candidate (0 of 65536 px differ). With the fix, 3 single mints were byte-identical and the batch mint equals the single mint. All 7 points/billboard fixtures pass with max diff 0. Commit 728969f.
- Next action: None. The full re-mint in the GAP-001 sweep checks every other golden minted by the old harness.
- Dependencies: None.
- Acceptance criteria: The fixture passes at 2.001 / 0.98 with no tolerance change. The other heightGrid and points fixtures stay PASS. The minters capture the loaded program after a slow compile.
- Required checks: `bash parity/run.sh heightGrid_billboard_alpha` against a fresh golden; the two new golden_mint tests in parity/test_harness_contract.py; a full sweep.
- Last verification: 2026-09-24, macOS (Apple M4). run.sh [PASS] max-abs-diff 0.000. 7 of 7 points fixtures PASS. Python tests 33 OK. ctest 15/15.

### GAP-011: heightmap3d_landscape exceeds the strict tolerance by one level

- Status: closed. Priority: P2. Category: verification.
- Affected scope: render/renderLandscape3d voxel path (FILTERING 1, VIEW_MODE 1) over synth3d/heightmap3d. Fixture: parity/programs/heightmap3d_landscape.dsl. Policy: parity/sweep.sh tol_for().
- Expected behavior: PASS at 2.001 / 0.98, or a NEAR policy backed by a traced mechanism.
- Observed behavior: 2 of 65536 px differ (max 3, SSIM 1.00000) at top-down row 131, x=53 and x=202. At each, two ray-origin components lie 7.418e-06 (0.49 ulp of nextT) from a voxel-edge tie in exact arithmetic. The left-to-right float32 sum (Apple GL) steps one axis first. The reference's ANGLE Metal compile rounds to a tie and crosses both axes. The two sides select adjacent voxels.
- Evidence: All landscape-pass inputs and its geo output are bit-identical (intermediate dumps, 8 frames). A CPU float32 model reproduces both colors: 3 of 12 equal orders give the candidate voxel and 9 give the golden voxel. Reassociating only the origin sum in a scratch shader copy made the candidate match the golden (max diff 0); the committed shader stays byte-identical to the reference. Golden and candidate were each bit-exact across 2 runs. Commit in the tol_for() entry for heightmap3d_landscape.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: A PASS, or a NEAR policy with a documented mechanism and a measured bound. Met: tol_for() 3.001 / 0.999, mechanism inline, bound 2 px / max 3.
- Required checks: parity/run.sh at the tol_for() policy; a full sweep.
- Last verification: 2026-09-24, macOS (Apple M4). `bash parity/run.sh heightmap3d_landscape 3.001 0.999`: [PASS] max 3.000 ssim 1.00000.

### GAP-012: hosts had to build MIDI state JSON by hand

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: qt/noisemaker/runtime/midi_state.{h,cpp}, the Backend midi() evaluator, parity/check_midi_state.mjs, qt/tests/test_midi_state.cpp.
- Expected behavior: The engine owns the reference MidiState machine. Hosts feed raw bytes with a port identity and pass snapshot() to setMidiState().
- Observed behavior: Before this change, setMidiState() accepted JSON only, and each host had to port the reference message logic. The evaluator ignored the port inventory.
- Evidence: nm::MidiState ports MidiChannelState and MidiState. The evaluator now resolves name selectors through `portInventory`, as reference getPortState does.
- Next action: None. The midiNoteGrid texture and midiClockCount uniform are bound as of GAP-013.
- Dependencies: None.
- Acceptance criteria: Every state dump matches the reference exactly for handcrafted and fuzzed event streams. The host contract test passes.
- Required checks: `node parity/check_midi_state.mjs`, test_midi_state, ctest.
- Last verification: 2026-09-24, macOS.

<details><summary>GAP-012 evidence</summary>

- `NM_REFERENCE_ROOT=<reference 285e50f5> node parity/check_midi_state.mjs`: MIDI_STATE 66/66, exit 0. Scenarios: 1 handcrafted scenario (61 events, 12 dumps) and 3 seeded fuzz scenarios (4569, 4607 and 4556 events). Each dump compares every channel field, per-origin bookkeeping, ports, the unscoped state and the name indexes.
- Mutation checks on the candidate: MPE other-zone default, CC74 neutral value, CC121 reset copy, and pitch-bend shift each failed the gate (15, 12, 23 and 12 of 66). The restored source passed 66/66.
- test_midi_state: 15 PASS, 0 FAIL. `snapshot()` plus `setMidiState()` took 1.206 ms per call with 3 ports and the unscoped state.
- ctest 14 of 14. Release `-Wall -Wextra -Wpedantic -Werror` build: 0 warnings.
- external-input.js is unchanged between the pin `5b81e04f` and the checkout (`git diff --stat` empty).

</details>

### GAP-013: hosts had to analyze audio and build audio state JSON by hand

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: qt/noisemaker/runtime/audio_analyzer.{h,cpp}, audio_state.{h,cpp}, the Backend audio() evaluator and engine inputs, parity/check_audio_*.mjs, qt/tests/test_audio_input.cpp.
- Expected behavior: The engine owns AnalyserNode analysis and the reference AudioState. Hosts feed interleaved float samples; snapshot() feeds setAudioState().
- Observed behavior: Before this change, setAudioState() accepted JSON only. audioWaveform, audioSpectrum, midiClockCount and midiNoteGrid were never bound, so synth/scope, synth/spectrum and synth/roll saw no input.
- Evidence: nm::AudioAnalyzer follows Chromium AnalyserNode. nm::AudioState ports the reference class. nm::AudioInput reproduces the Noisedeck wiring (FFT 256, smoothing 0.5, -80 to -30 dB, 3-frame bands, quantum-mean raw).
- Next action: None. The AudioInput composition has unit tests only; Noisedeck's pump is host code and has no importable oracle.
- Dependencies: None.
- Acceptance criteria: Analyser outputs match Chromium within the documented tolerance. AudioState dumps match the reference exactly. The end-to-end test passes.
- Required checks: `node parity/check_audio_analyzer.mjs`, `node parity/check_audio_state.mjs`, test_audio_input, ctest.
- Last verification: 2026-09-24, macOS arm64, Chromium 151.0.7922.34.

<details><summary>GAP-013 evidence</summary>

- check_audio_analyzer.mjs: AUDIO_ANALYZER 570/570, exit 0. 6 cases (mono, stereo, quad, 5.1; FFT 256 to 2048; several smoothing and dB ranges), 570 reads at render-quantum boundaries from an OfflineAudioContext. Tolerance: byte frequency within 1, float frequency within 0.05 dB, time domain exact. Measured: 1 byte difference of 1 in 186,368 byte values; largest dB difference 1.28e-3; 0 time-domain mismatches.
- Chromium measured facts built into the port: byte time-domain data adds 1 in float before scaling; the 5.1 down-mix accumulates with fused multiply-add on arm64 (a sequential non-fused sum mismatched 171 of 1920 frames; the fused chain mismatched 0).
- check_audio_state.mjs: AUDIO_STATE 93/93, exit 0. 3 seeded scenarios of 3031 events, exact comparison including smoothing buffers, device registry, inventory and default channels. Mutation checks failed the gate (4, 32, 0 and 66 of 93).
- test_audio_input: 13 PASS, 0 FAIL. It covers the aggregate and per-channel bands, raw DC, device selection by name, inventory and id, disconnects, and synth/scope and synth/roll pixel changes.
- ctest 15 of 15; `-Werror` build 0 warnings; all 11 parity gates exit 0; 31 Python harness tests OK.

</details>

### GAP-014: status and architecture documents contradicted the code

- Status: closed. Priority: P3. Category: contract.
- Affected scope: README.md, STATUS.md, ARCHITECTURE.md.
- Expected behavior: Documents state the real GL requirement, current counts, real classes, and verified platforms.
- Observed behavior: Before this change, the documents claimed OpenGL 3.3 (the context is 4.1 core), 311 shaders (317), 345 fixtures (357), and ctest 8/8 (15). They listed a nonexistent `nm::Pipeline` class and said midi()/audio() raise UnsupportedDsl. README reported an old sweep (289/45/1 of 335).
- Evidence: Each statement was checked against source: `setVersion(4, 1)` in backend.cpp, `#version 330 core` in shader_assembly.cpp, and the UnsupportedDsl throw sites in validator.cpp and expander.cpp. Gate and ctest counts come from this stream's runs.
- Next action: None for these files. PORTING-GUIDE.md already matched the code. See GAP-015 for AGENTS.md.
- Dependencies: None.
- Acceptance criteria: No listed contradiction remains. Counts match the latest measured runs.
- Required checks: `grep -n "3\.3\|nm::Pipeline\|311\|345/345"` finds only dated historical sync notes.
- Last verification: 2026-09-24.

### GAP-015: AGENTS.md states an OpenGL 3.3 runtime

- Status: closed. Priority: P3. Category: contract.
- Affected scope: AGENTS.md line 3.
- Expected behavior: The instruction file names the OpenGL 4.1 core requirement.
- Observed behavior: "Desktop OpenGL 3.3 runtime and compiler for the Noisemaker shader platform."
- Evidence: `grep -n "3\.3" AGENTS.md` returns line 3.
- Next action: None.
- Dependencies: None. The operator approved the instruction-file edit on 2026-09-24.
- Acceptance criteria: AGENTS.md names the 4.1 core context and GLSL 330 core.
- Required checks: grep of AGENTS.md.
- Last verification: 2026-09-24. AGENTS.md line 3 now reads "Desktop OpenGL runtime (4.1 core context, GLSL 330 core shaders) and compiler for the Noisemaker shader platform." `grep -n "3\.3" AGENTS.md` returns nothing.

### GAP-016: bare state values in params raised UnsupportedDsl

- Status: closed. Priority: P2. Category: contract.
- Affected scope: qt/noisemaker/compiler/validator.cpp (boolean, member, numeric params), qt/tests/test_validator.cpp, parity/corpus/state_values.dsl.
- Expected behavior: `noise(octaves: frame, ridges: time)` compiles to the graph the reference produces.
- Observed behavior: Before this change, Qt threw UnsupportedDsl. The reference compiled to `{}` (boolean, member) and `{min, max, _ast}` (numeric), because its `{fn}` closures are never called or serialized.
- Evidence: New corpus fixture parity/corpus/state_values.dsl covers all three positions. VALIDATE, EXPAND and GRAPH gates are 1/1 on it and 358/358 on the full pool.
- Next action: None. The runtime binds these objects as 0, as WebGL2 does for a non-numeric uniform.
- Dependencies: None.
- Acceptance criteria: Byte-identical validate, expand and graph output against the reference for every state-value position.
- Required checks: check_validate, check_expand, check_graph; test_validator; ctest.
- Last verification: 2026-09-24. ctest 15 of 15; LEX, PARSE, VALIDATE, EXPAND and GRAPH each 358/358; `-Werror` build 0 warnings.

### GAP-017: arrow-function params raise UnsupportedDsl

- Status: open. Priority: P2. Category: contract.
- Affected scope: validator.cpp Func branches (boolean and numeric params); Func conditions in if/elif (the same check).
- Expected behavior: `noise(octaves: () => time * 4)` compiles like the reference: `{min, max}` numeric, `{}` boolean, or S001 for invalid JavaScript.
- Observed behavior: Qt throws UnsupportedDsl. The reference graph for that program carries `octaves: {min: 1, max: 8}` and `ridges: {}` (probe with tools/dump-graph.mjs).
- Evidence: The reference decides validity with `new Function(...)`, a JavaScript parse. This port has no JavaScript expression parser to reproduce S001.
- Next action: Port a JavaScript expression syntax check for arrow bodies, then emit the reference values. Gate with corpus fixtures, including invalid bodies.
- Dependencies: A JavaScript expression grammar check that matches V8 for the DSL's arrow-body subset.
- Acceptance criteria: check_validate and check_graph byte-identical on valid and invalid arrow bodies.
- Required checks: new corpus fixtures, compiler gates, test_validator.
- Last verification: 2026-09-24.

### GAP-018: if/elif/else, break, continue and return raise UnsupportedDsl

- Status: closed. Priority: P3. Category: contract.
- Affected scope: validator.cpp control-flow statements (compileStmt, compileBlock, evalExpr, evalCondition); expander.cpp expandPlan; parity/corpus/control_flow_{conditions,blocks,statements,block_let,block_comment}.dsl; test_validator, test_expander.
- Expected behavior: Match the reference at each stage. Lex and parse succeed. Validate returns Branch, Break, Continue and Return plans. Expand fails with "plan.chain is not iterable", so compileGraph fails.
- Observed behavior: Before this change, validate threw UnsupportedDsl, and nm::expand silently expanded nothing for a plan without a chain. The port now returns the reference plans and fails at expand with the reference text.
- Evidence: With the previous binaries, VALIDATE was 0/3 on the 3 fixtures the reference validates. After the change, LEX, PARSE, VALIDATE, EXPAND and GRAPH are each 5/5 on the control-flow fixtures and 364/364 on the full pool. The fixtures cover conditions (numbers, NaN/Infinity via the clone, booleans, let-bound values, state values, unknown identifiers, resolved and unresolved members, strings, colors, refs, calls, chains, osc), nested blocks with the shared temp counter, every Return value form, the `let`-in-block TypeError, and the parse failures. Commit ff10380.
- Next action: None. Func conditions (`if (() => ...)`) use the same check as Func params and are tracked under GAP-017.
- Dependencies: None.
- Acceptance criteria: check_validate byte-identical on the control-flow fixtures. Expand and compileGraph fail for the same programs.
- Required checks: check_lex, check_parse, check_validate, check_expand, check_graph; test_validator; test_expander; ctest.
- Last verification: 2026-09-24, reference c9ee8a04, macOS arm64, ctest 15/15.

### GAP-019: triangle mesh rendering and meshLoader are not implemented

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: render/meshRender (`drawMode: "triangles"`), render/meshLoader, nm::Backend, qt/noisemaker/runtime/obj_parser.{h,cpp}, qt/noisemaker/share/meshes, nm-render, the golden and candidate harness.
- Expected behavior: Draw triangles from the mesh textures that the reference fills through its mesh upload and OBJ parser (webgl2.js drawMode triangles; obj-parser.js).
- Observed behavior: Backend draws `drawMode: "triangles"` with the reference state: the pass's own vertex stage, a DEPTH_COMPONENT24 depth buffer cleared per pass, LESS, back-face culling with CCW front faces, gl_VertexID draws, and the count from the mesh position texture. Hosts supply meshes through loadOBJFromFile, loadOBJFromString, and uploadMeshData. externalMeshes(graph) lists meshLoader steps and their built-in meshes. The engine loads no default mesh, as the reference engine does. nm-render loads the first built-in mesh (sphere), as the reference demo host does. The parser is bit-identical to obj-parser.js.
- Evidence: parity/run.sh at 2.001/0.98: meshLoaderDefault, meshLoaderPreview, meshRenderEmpty, meshRenderParams, meshRenderWireframe, and meshRenderCustom pass with max-abs-diff 0 and ssim 1.0, also through --dsl and the sweep's batch render. Full sweep: 350/353 pass; the 3 failures (fibers, scratches, text) read no mesh and are GAP-025/GAP-026 cases. check_obj_parser.mjs: 443/443 inputs bit-identical (5043/5043 with 5000 fuzz cases); built-in meshes 7/7 byte-identical. Compiler gates 370/370; ctest 19/19; pytest parity 37/37. Independent integration check: goldens minted again from the reference for meshLoaderDefault and meshRenderCustom match with max-abs-diff 0; the mesh covers 19.5% and 61% of those frames. Commits f0d9eef, f58f1c1, 0ef82d1, ed509df, 79b356a. macOS arm64, Qt 6.11.1, reference c9ee8a04. CI run 36031919085 (e6c8f72): test_obj_parser and test_mesh_render pass on Linux llvmpipe, Windows llvmpipe, and macOS; check_obj_parser BUILTIN MESHES 7/7. The export kit ships the meshes since 3a470b6 and 31b8e2f: an assembled kit renders the default sphere (19.5% of the frame, centre 161), renders --mesh share/meshes/cube.obj differently, and exits 1 naming a missing OBJ.
- Next action: Grade mesh fixtures in CI render-smoke.
- Dependencies: None.
- Acceptance criteria: meshRender fixtures pass at the strict tolerance against reference goldens.
- Required checks: parity/run.sh on the mesh fixtures, check_obj_parser.mjs, test_obj_parser, test_mesh_render, then a full sweep.
- Last verification: 2026-09-24, reference c9ee8a04, macOS arm64 renders; Linux and Windows CI tests.

### GAP-020: compute pass fields raise UnsupportedDsl

- Status: closed. Priority: P3. Category: contract.
- Affected scope: expander.cpp and expander.h (entryPoint, workgroups, storageBuffers, storageTextures); tools/convert-definitions.mjs projectPass; test_expander.
- Expected behavior: The expander copies the four fields from each pass definition verbatim, as expander.js does (storageTextures is not resolved). The normalized graph drops them.
- Observed behavior: Before this change, the expander threw UnsupportedDsl. convert-definitions.mjs dropped workgroups, storageBuffers and storageTextures, so those fields could not reach the expander. No definition declares these fields at reference c9ee8a04.
- Evidence: A synthetic compute definition (entryPoint, workgroups, storageBuffers, storageTextures, outputBuffer output). Raw expand from the reference over its definition.js is EQUAL to Qt expand over the converted JSON. The normalized graph is EQUAL too. The previous converter omitted the three fields; the new converter carries them. check_definitions stays at 210/210. The new test_expander case aborts with the old expander (exit 134) and passes after the change. Commit e9da55d.
- Next action: None for the compiler. Render parity for WebGL2's compute-to-render conversion (outputBuffer renamed to color, and `{color: 'outputTex'}` when a pass has no outputs) waits for the first definition that uses these fields.
- Dependencies: A reference definition with these fields, for render verification only.
- Acceptance criteria: Raw expand and normalized graph equal the reference for a definition with the fields. The converter carries them.
- Required checks: test_expander; check_definitions; check_expand and check_graph; ctest.
- Last verification: 2026-09-24, reference c9ee8a04.

### GAP-021: RGBA16F recreation check assumed the driver reports RGBA16F

- Status: closed. Priority: P2. Category: verification.
- Affected scope: qt/tests/test_device_limits.cpp, .github/workflows/ci.yml (ctest (macOS) step); macOS runners without a GPU.
- Expected behavior: The test checks that a format change recreates the surface with the driver's storage for the requested format.
- Observed behavior: CI run 35970287679 (macos-latest) failed "recreated texture has GL_RGBA16F internal format". The test now compares against a fresh RGBA16F probe. CI run 36025136332 (dc70409), macos-latest: "GL_RENDERER Apple Software Renderer: RGBA16F probe reports 0x8814, recreated texture 0x8814", ALL PASS in 81.63 s.
- Evidence: The hosted macOS runner uses the Apple Software Renderer, which stores a requested RGBA16F texture as RGBA32F (0x8814); the Apple M4 reports RGBA16F (0x881a). On both, the recreated surface reports exactly the probe's format. This is driver storage, not a SurfaceCache bug. The macOS ctest step runs test_device_limits with -V so the renderer and formats are in the log.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: macos-latest test_device_limits passes. The log shows GL_RENDERER and matching probe and recreated formats. Met in CI run 36025136332.
- Required checks: `gh run view <run> --log` for the macos-latest build-test job, "ctest (macOS)" step.
- Last verification: 2026-09-24. CI run 36025136332: Apple Software Renderer, probe 0x8814, recreated 0x8814, pass. Local Apple M4: probe 0x881a, recreated 0x881a, pass.

### GAP-022: AnalyserNode 5.1 down-mix rounding differs by CPU architecture

- Status: closed. Priority: P2. Category: verification.
- Affected scope: qt/noisemaker/runtime/audio_analyzer.cpp, parity/check_audio_analyzer.mjs.
- Expected behavior: Time-domain data matches the Chromium on the same architecture exactly, for 5.1 input too.
- Observed behavior: CI run 35970287679 (ubuntu x86_64, Chromium 153.0.8010.12) failed the surround-5.1 case: 8983 time-domain mismatches, AUDIO_ANALYZER 476/570. At index 914, x86 Chromium gave 0.8555886149406433, the unfused value. arm64 Chromium gives the fused value 0.8555885553359985.
- Evidence: The down-mix now uses std::fma on arm64 and a separate multiply and add elsewhere. The file builds with -ffp-contract=off. The oracle stays the Chromium running on that machine, with the same tolerance and all 570 reads.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: 570/570 on arm64 (local) and on x86_64 (CI), with the tolerances unchanged.
- Required checks: check_audio_analyzer.mjs on both architectures.
- Last verification: 2026-09-24. arm64 local: 570/570, 0 time-domain mismatches. x86_64: CI run 35974199816 (12c1d1c), render-smoke on ubuntu-latest: AUDIO_ANALYZER 570/570.

### GAP-023: fused multiply-add changed automation values against the reference

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: every CPU-side floating-point expression in libnoisemaker-qt, notably the midi()/audio()/osc() evaluator in backend.cpp.
- Expected behavior: Resolved automation values equal the reference's JavaScript double arithmetic, which never fuses a multiply and add.
- Observed behavior: Apple Clang on arm64 contracted `min + value * (max - min)` into an FMA. midi() CC 64 on gradient rotation resolved to 1.4173228346456668. Reference Pipeline.resolvePassUniforms gives 1.4173228346456597.
- Evidence: The library now builds with -ffp-contract=off (GCC, Clang). `otool -tv` finds FMA instructions only in the explicit std::fma of the arm64 down-mix. test_midi_state asserts the reference values -180, 1.4173228346456597 and 180 for CC 0, 64 and 127.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: Exact reference values in test_midi_state. Every parity gate and the rendered ledger unchanged.
- Required checks: ctest, all 11 parity gates, a SKIP_GOLDEN sweep regrade.
- Last verification: 2026-09-24. ctest 15/15. Gates exit 0: 317/317, 210/210, 5/5, 358/358 five times, 66/66, 570/570, 93/93. The regrade gave 345 of 347 with the same 2 FAILs; `git diff parity/ledger.json` is empty. `-Werror` build: 0 warnings.

### GAP-024: member paths ignored array and string own properties

- Status: closed. Priority: P2. Category: implementation.
- Affected scope: validator.cpp resolveEnum (member params, numeric Member args, let bindings, control-flow conditions and Return values); parity/corpus/member_own_properties.dsl; test_validator.
- Expected behavior: resolveEnum walks paths with `cur && hasOwnProperty.call(cur, part)`. Arrays and strings have own index properties and `length`.
- Observed behavior: Before this change, the port walked JSON objects only. `noise(octaves: c.value.length)` with `let c = #ff8800` compiled octaves 2 (the default) instead of 4.
- Evidence: On member_own_properties.dsl, the previous binaries gave VALIDATE 0/1 (octaves ref=4, port=2) and EXPAND 0/1; GRAPH 0/1 failed on an error diagnostic. After the change all three are 1/1, and the full pool is 364/364 at every stage. Commit 525e09c.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: validate, expand and graph byte-identical on the fixture.
- Required checks: check_validate, check_expand, check_graph; test_validator.
- Last verification: 2026-09-24, reference c9ee8a04.

### GAP-025: async-init overlay effects render without their overlay

- Status: open. Priority: P1. Category: implementation.
- Affected scope: filter/fibers, filter/scratches and filter/strayHair (the three definitions with asyncInit at reference c9ee8a04); qt/noisemaker/runtime (no asyncInit, overlayTex or CPU worm tracing); parity/sweep.sh tol_for() entries for the three; the export kit, which ships them.
- Expected behavior: As in the reference, each effect's asyncInit traces its overlay on the CPU (traceWorms into a canvas) and uploads it as overlayTex, progressively through onProgress, and regenerates it when its inputs change (ProgramState.checkAsyncRegen, 300 ms debounce).
- Observed behavior: The overlay is never generated. The fibers candidate is one colour (0,0,0,255) over all 65536 px; the scratches candidate is one colour (43,43,43,255). The tol_for() NEAR entries (fibers 122.001/0.93, scratches 147.001/0.75, strayHair 79.001/0.998) were labelled as a near-zero Sobel singularity; they absorbed the difference between an absent overlay and a partly traced one. The GAP-010 minter race hid this by capturing before the traces drew. This was false completion.
- Evidence: GAP-001 sweep 1 at pin c9ee8a04 (HEAD 130540a) with the fixed minter: fibers FAIL max 122 ssim 0.9206, scratches FAIL max 147 ssim 0.7291. No Qt runtime source mentions overlayTex, traceWorms or asyncInit.
- Next action: Port the reference asyncInit contract and the three effects' CPU overlay generation (traceWorms and its RNG) to the engine, with an oracle test of the traced overlay against the reference under Node, and remove the three mislabelled tol_for() entries in the same change.
- Dependencies: GAP-026 for stable goldens of progressive overlays.
- Acceptance criteria: The overlay is generated and uploaded as the reference does; the CPU overlay matches the reference bit for bit for fixed inputs; the three fixtures pass at strict tolerance, or at a NEAR entry backed by a traced mechanism; the export kit renders them.
- Required checks: An overlay oracle test; parity/run.sh for the three fixtures against deterministic goldens; ctest.
- Last verification: 2026-09-24. Open.

### GAP-026: goldens of async and host inputs depend on capture timing

- Status: open. Priority: P2. Category: verification.
- Affected scope: parity/export-and-render.mjs, parity/batch-golden.mjs; fixtures with asyncInit overlays (fibers, scratches, strayHair) and with host text (text).
- Expected behavior: A golden captures a defined state: async overlays complete and quiescent, and host inputs identical to what the candidate receives.
- Observed behavior: Three single fixed-harness mints of fibers gave mint1 == mint2 while mint3 differed by 28 px: the capture lands at an arbitrary point of the progressive trace. The reference demo UI draws "Hello World" into textTex 50 ms after creating the controls (setTimeout), so under load the sweep golden for text included it (2944 px differ from the candidate) while quiet mints did not (4 px). nm-render supplies no textTex, so the text fixture only tests an empty-text passthrough.
- Evidence: GAP-001 sweep 1 (load average about 96): text FAIL max 204 ssim 0.972. Three quiet single mints of text at load average 8 were byte-identical.
- Next action: Make minting wait for asyncInit completion and debounce quiescence before the 8-frame protocol; give the text fixture a defined text input on both sides (the same rasterized textTex fed to nm-render, or the host text cleared on the reference side).
- Dependencies: None.
- Acceptance criteria: Repeated mints of each affected fixture are byte-identical across quiet and loaded runs, and the text fixture exercises real text.
- Required checks: Three mints per affected fixture compared with cmp, one under load; parity/run.sh.
- Last verification: 2026-09-24. Open.

### GAP-027: filter/text rasterization differs from a browser canvas

- Status: open. Priority: P3. Category: ecosystem.
- Affected scope: qt/noisemaker/runtime/text_texture.{h,cpp}; the export kit's text() output.
- Expected behavior: Text pixels match the reference host's Chromium canvas.
- Observed behavior: Layout matches within 1 px (centroid). Chromium's glyph masks are heavier: the port draws 0.76 to 0.95 of Chromium's coverage. Qt shapes variable fonts with the default instance's GPOS kerning: at Nunito wght 800 and 102 px, "Heavy" is 4.4 px narrower. Generic families resolve to Qt's default families, not the browser's. Before 167a62c, FreeType slanted synthetic italic by about 12 degrees, not Chromium's skew of 1/4; the italic case missed the centroid bound by 0.38 px.
- Evidence: parity/check_text_canvas.mjs 10/10 at the documented tolerances (centroid 1 px, edges 5 px, coverage 0.70 to 1.05), Chromium 151, macOS arm64, Qt 6.11.1. The font file is byte-identical to the reference's demo/font/Nunito (SHA-256 707f6b338cfd21e95f05a88169ef7647d01ad8da76623846c092f3118f762a08). Commit ccf0969. On FreeType (Linux, and macOS with QT_QPA_PLATFORM=cocoa:fontengine=freetype), an unset wght axis draws at the fvar default 200. The renderer sets the CSS weight on the axis, so its layout is the same on both engines; test_text_texture measures its reference advance the same way since aa607d5. The renderer applies Chromium's synthetic italic skew itself since 167a62c. check_text_canvas is 10/10 on the FreeType engine (macOS, cocoa:fontengine=freetype); CI runs it on Linux in render-smoke since d4ba5b3. First Linux result (CI run 36029514281, bd5f1a7, Qt 6.11.1, headless Chromium): 10/10 PASS, centroid max |d| 0.762 px, coverage qt/chrome 0.954 to 0.996.
- Next action: Measure on Windows (DirectWrite). Re-check kerning when Qt applies variation deltas in shaping.
- Dependencies: Qt font shaping (external).
- Acceptance criteria: check_text_canvas passes on each supported platform with the same tolerances, or the tolerances are re-derived from that platform's measurements and recorded.
- Required checks: check_text_canvas.mjs, test_text_texture.
- Last verification: 2026-09-24, macOS (CoreText and FreeType engines) and Linux CI.

### GAP-028: compileGraph failures did not say what was wrong

- Status: closed. Priority: P2. Category: usability.
- Affected scope: qt/noisemaker/compiler/dsl_compiler.{h,cpp} (nm::CompilationError); every host that calls nm::compileGraph (nm-render --dsl and --dump-graph, the export-kit host); qt/tests/test_expander.cpp.
- Expected behavior: A failed compile carries the reference's code and its diagnostics or expand errors. The shown text is compiler.js formatError: error diagnostics as "message (line L, col C)" joined by "; ", or the expand error messages.
- Observed behavior: Before f53f3d6, compileGraph threw "ERR_COMPILATION_FAILED: validate() reported an error-severity diagnostic" or the fixed ERR_EXPANSION_FAILED equivalent. The user got no code, message, or location.
- Evidence: what() equals the reference formatError output byte for byte on 7 failing programs (located and unlocated diagnostics, and the no-render-surface expand error). test_expander checks both codes and their text. nm-render --dsl on an undefined variable now prints "Variable used before assignment: 'bogus'" (e073c14 printed the generic text). Commit f53f3d6.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: nm::CompilationError exposes code(), diagnostics(), and errors(). what() equals the reference formatError text for both codes.
- Required checks: test_expander; ctest.
- Last verification: 2026-09-24, reference c9ee8a04, macOS arm64.

### GAP-029: nm-render has no usage text

- Status: closed. Priority: P3. Category: usability.
- Affected scope: qt/tools/nm-render/main.cpp; the README nm-render section.
- Expected behavior: nm-render --help, or a call without a render mode, prints the modes and their flags and exits 0 or 2.
- Observed behavior: Fixed in c24cf7a. --help prints every mode and flag (--dsl, --graph, --samples, --batch-manifest, --size, --out, --time, --frames, --mesh, the --dump-* modes, --help), the data root, and exit statuses, and exits 0. An unknown option, no arguments, or no mode print the usage with the problem named and exit 2. Before the fix, nm-render --help printed "unimplemented: no recognized render mode in arguments". A developer must read main.cpp or the README to find --dsl, --size, --out, --time, --frames, --graph, --samples, --batch-manifest, and the --dump-* modes.
- Evidence: Before: nm-render built from f53f3d6. After: test_nm_render_cli (5 checks) in ctest; check_lex and check_graph 364/364 and check_parse and check_validate 370/370 still pass. Integration check: --help exits 0; --bogus prints "ERROR: unknown option --bogus" and the usage and exits 2.
- Next action: None.
- Dependencies: None.
- Acceptance criteria: --help lists every mode and flag in main.cpp. An unknown flag names that flag and prints the usage block.
- Required checks: a CLI test that runs nm-render --help and an unknown flag and checks the exit codes and text.
- Last verification: 2026-09-24.

### GAP-030: the reference loses the first mesh draw into a new FBO

- Status: open. Priority: P3. Category: authority.
- Affected scope: reference webgl2.js ensureDepthBuffer() and the executePass triangles branch; render/meshRender on the first frame after its output FBO is created (compile, resize).
- Expected behavior: Every frame draws the mesh into the pass output.
- Observed behavior: ensureDepthBuffer() binds the default framebuffer after it creates a depth buffer. The reference's first mesh draw into a new FBO goes to the canvas. The port draws into the FBO on every frame, so its first frame differs from the reference's.
- Evidence: Reference probe at c9ee8a04 (Chromium WebGL2): after pipeline.resize(200, 200), the frame 1 centre pixel is (25, 25, 38, 255) (background) and frame 2 is (163, 163, 163, 255). Goldens render 8 frames and are unaffected.
- Next action: Fix the reference: bind the pass FBO again after ensureDepthBuffer(). The reference repository owns this change.
- Dependencies: The reference repository.
- Acceptance criteria: The reference draws the mesh on frame 1, or this divergence stays recorded.
- Required checks: The 1-frame reference probe after a resize.
- Last verification: 2026-09-24.

### GAP-031: triangle passes ignore countUniform

- Status: open. Priority: P3. Category: contract.
- Affected scope: nm::Graph (graph.cpp does not parse countUniform); the Backend triangles count.
- Expected behavior: A triangles pass with countUniform draws that uniform's positive value (webgl2.js executePass).
- Observed behavior: The expander emits countUniform, but the runtime ignores it and uses count. No definition declares countUniform at c9ee8a04.
- Evidence: grep -rn countUniform shaders/effects in the reference returns nothing. qt/noisemaker/runtime/graph.cpp has no countUniform field.
- Next action: None until a definition uses it. Then parse it and port the lookup (pass uniforms, then globals).
- Dependencies: A reference definition with countUniform.
- Acceptance criteria: A render fixture for the first such definition passes at strict tolerance.
- Required checks: check_graph and parity/run.sh on that fixture.
- Last verification: 2026-09-24.

### GAP-032: a wrong data root reports every effect as unknown

- Status: open. Priority: P2. Category: usability.
- Affected scope: qt/noisemaker/compiler/effect_registry.cpp (EffectRegistry::loadAll); every library host that passes a data root.
- Expected behavior: A data root without effect definitions fails with a message that names the directory.
- Observed behavior: EffectRegistry::loadAll(root) loads nothing and does not throw when <root>/effects is missing. Each program then fails with "Unknown effect: '<name>'" for every effect. The Qt Quick item checks the root itself; plain library hosts do not.
- Evidence: A literal README run, 2026-09-24: a missing data root gives exit 1 with the unknown-effect message.
- Next action: Throw std::runtime_error naming "<root>/effects" when it holds no definitions. Add a unit test.
- Dependencies: None.
- Acceptance criteria: loadAll on a root without effects/ throws with the directory in the message; nm-render with a wrong root prints it.
- Required checks: test_registry; ctest.
- Last verification: 2026-09-24, macOS arm64.

## 5. Ordered next actions

1. Resolve authority identities for GAP-001. Retain earlier denominators, goldens, tolerances, and exclusions.
2. Execute the installed workflow for GAP-002. Record meaningful output, failure recovery, versions, and cleanup.
3. Run compiler and rendered parity for GAP-001. Keep structural, numerical, and platform evidence separate.
4. Qualify distribution contents and lifecycle for GAP-003 after the installed workflow passes.
5. Record measured results. Close entries only when their acceptance criteria pass.

Implementation belongs to the separate job. Do not port additional effects or advance the current parity checkpoint through this register.

## 6. Pass history

| Date | Source SHA | Changes | Tested scope | Remaining limits |
|---|---|---|---|---|
| 2026-09-24 | `8460cfd77798d4e79d37828c16b0b29a09fbdbda` | Created six-section register and README link. No closures. | 31 Python harness tests passed. A later rebuild passed 12 C++ tests and rendered noise. Full GPU and installed-viewer qualification remain open. | Full audit, installed workflows, current rendered parity, platforms, and releases remain unqualified. |
| 2026-09-24 | `5f912154eb1c3015f24d3e3a613d0e52150b89e5` + local commits | Implementation stream: added GAP-004, GAP-007, GAP-008, GAP-009, GAP-012, GAP-013 and GAP-014 and GAP-016 (closed), GAP-015 (blocked), GAP-017 to GAP-020 (open) and GAP-005, GAP-006, GAP-010 and GAP-011 (open). Full sweep: 298 PASS, 47 NEAR, 2 FAIL of 347. | Top-level ctest 12 of 12. Embedded, embedded-with-tests, and installed consumers built and rendered on macOS. | Other platforms unverified. Remote CI not run. |
| 2026-09-24 | `12c1d1c6f160e5fe30f482e6376f220b3ec982c4` | CI evidence review: GAP-005, GAP-006 and GAP-022 closed; GAP-021 updated (passes on macos-latest, renderer not yet logged). | CI run 35974199816: 8 of 8 jobs passed on macOS, Linux and Windows; ctest 15/15 on each; all gates and the render smoke passed. | GAP-021 needs the renderer in the log. GAP-010, GAP-011, GAP-017 to GAP-020 remain open; GAP-015 blocked. |

Run ID: `20260924-remaining-gap-documents`.
[Operational evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents). Creating this register does not advance successful-audit timestamps or the rotation.

2026-09-24 report initialization: added the maintained compatibility report and bounded native measurements. No full-parity closure.
