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
- Last verification: 2026-09-24. Full behavior qualification remains unverified.

### GAP-002: installed developer workflow qualification

- Status: open. Priority: P2. Category: usability.
- Affected scope: Public API, examples, supported hosts, errors, recovery, and lifecycle.
- Expected behavior: Developers can install, produce useful output, integrate it, recover from errors, and remove the package.
- Observed behavior: This pass did not exercise the complete installed workflow or supported-version matrix.
- Evidence: [README](https://github.com/noisefactorllc/noisemaker-for-qt/blob/8460cfd77798d4e79d37828c16b0b29a09fbdbda/README.md), [official reference](https://doc.qt.io/qt-6/qopenglwidget.html), and section 3.
- Next action: Build and install under a private prefix. Load with find_package, render DSL to PNG, run viewer --selfcheck, then test resize and teardown.
- Dependencies: Use an isolated consumer. Identify host, GPU, licensing, and input requirements before execution.
- Acceptance criteria: Retain artifact hashes, steps, meaningful output, error diagnostics, recovery results, and cleanup results.
- Required checks: Test minimum and current supported versions. Check cancellation and file preservation where relevant. Keep unavailable platforms explicit.
- Last verification: 2026-09-24. Source inspection does not close this gap.

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
- Installed consumer: `cmake --install` exit 0. `find_package(noisemaker-qt CONFIG)` exposed the same target, variable, and property names. The same render produced pixel `ff3d837b`. Exit 0.

</details>

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
| 2026-09-24 | `5f912154eb1c3015f24d3e3a613d0e52150b89e5` + local commits | Implementation stream: added GAP-004 (closed). | Top-level ctest 12 of 12. Embedded, embedded-with-tests, and installed consumers built and rendered on macOS. | Other platforms unverified. Remote CI not run. |

Run ID: `20260924-remaining-gap-documents`.
[Operational evidence](/Users/alex/.codex/automations/noisemaker-port-completion-audit/evidence-20260924-remaining-gap-documents). Creating this register does not advance successful-audit timestamps or the rotation.

2026-09-24 report initialization: added the maintained compatibility report and bounded native measurements. No full-parity closure.
