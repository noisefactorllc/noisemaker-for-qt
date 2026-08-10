#!/usr/bin/env bash
# parity/sweep.sh — corpus-wide parity sweep (task T6). Adapted from the
# Godot sibling's sweep.sh (stage-classify -> render -> grade -> ledger
# control flow, tol_for()/reason_for() per-program tolerance table,
# record_result/write-ledger.py integration) with two structural differences
# this repo's own workflow needs that godot's sweep.sh does not:
#
#   1. GOLDEN MINTING is part of THIS sweep (godot's goldens are minted
#      separately, once, and cached outside its sweep script; this task's
#      brief requires goldens and candidates to be minted FRESH in the SAME
#      run, never graded against a stale golden). See PHASE 1.
#   2. The candidate render step is nm-render's own `--batch-manifest`, a
#      flat JSON array of {graph,out,size,time,frames} (qt/tools/nm-render/
#      main.cpp runBatchManifest) -- same "one process renders the whole
#      corpus" idea as Godot's own `--batch-manifest`, different manifest
#      shape (parity/make-batch-manifest.py, this repo's own tool). It has
#      no timed-sampling mode, so the two timed fixtures (PHASE 3) render
#      via parity/run_samples.sh's own already-verified internal render
#      step instead of the batch step -- see PHASE 3's timed branch.
#
# Classification (chaos/defer/timed) is EVIDENCE-BASED ON THIS BACKEND, not
# copied from godot's table: see the case arms below, each with the
# mechanism actually OBSERVED here (task-T6-report.md has the evidence).
# PORTING-GUIDE.md rule 5: tolerance loosenings live ONLY in tol_for() below,
# each with an inline mechanism comment; never weaken a gate silently.
#
#   NM_REFERENCE_ROOT=/path/to/noisemaker bash parity/sweep.sh
#
# Env:
#   NM_RENDER      path to the nm-render binary (default $ROOT/qt/build/nm-render)
#   SKIP_GOLDEN=1  skip PHASE 1 (golden minting); grade against whatever is
#                  already in parity/out/. Test/dev seam (also used to reuse
#                  PHASE 1's graph-path goldens for the NM_LIVE_DSL=1 sweep,
#                  which compares against the SAME reference-produced goldens
#                  -- goldens depend only on the reference engine, never on
#                  which qt code path produced the candidate). Never set for
#                  the canonical graph-path sweep.
#   SKIP_RENDER=1  skip PHASE 2's candidate batch render (graph-path or
#                  live-DSL, whichever NM_LIVE_DSL selects); grade against
#                  whatever candidates already exist. Same seam as run.sh's
#                  own SKIP_RENDER. Does NOT affect the two timed fixtures --
#                  they always render via run_samples.sh's own verified
#                  internal step (PHASE 3), since there is no batched or
#                  live-DSL timed-sampling path to skip in the first place.
#   NM_LIVE_DSL=1  PHASE 2 renders every non-timed/non-defer candidate via
#                  `nm-render --dsl <fixture.dsl>` (the full C++ lex-> parse
#                  -> validate -> expand -> graph -> render pipeline) instead
#                  of `--graph <exported-graph.json>` (T10's deferred
#                  full-corpus proof). Not batchable -- --dsl is a per-
#                  process flag (dump_graph.cpp); --batch-manifest only ever
#                  reads {graph,out,size,time,frames} entries, so there is no
#                  --dsl-flavored batch entry to build a manifest from.
#   NM_EXTRA_CHAOS / NM_EXTRA_DEFER   space-separated program names UNIONED
#                  into the CHAOS/DEFER case arms below. Test-injection seam
#                  (test_harness_contract.py) so the classification MACHINERY
#                  is unit-testable independent of which real fixture names
#                  currently occupy those buckets. Always empty in a real run.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NM_RENDER="${NM_RENDER:-$ROOT/qt/build/nm-render}"
LEDGER_PATH="${LEDGER_PATH:-parity/ledger.json}"
SIZE=256
TIME=0.25
FRAMES=8
RESULTS="$(mktemp -t noisemaker-for-qt-ledger.XXXXXX)"
CANDIDATE_NAMES="$(mktemp -t noisemaker-for-qt-candidates.XXXXXX)"
GOLDEN_LIST="$(mktemp -t noisemaker-for-qt-goldenlist.XXXXXX)"
BATCH_MANIFEST="$(mktemp -t noisemaker-for-qt-batch.XXXXXX)"
trap 'rm -f "$RESULTS" "$CANDIDATE_NAMES" "$GOLDEN_LIST" "$BATCH_MANIFEST"' EXIT
record_result() {
	printf '%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" >> "$RESULTS"
}

# --- classification ----------------------------------------------------------

# TIMED: no valid single-frame golden exists for a stateful solver -- the
# golden is a TIMED SERIES (parity/run_samples.sh), matching the family's
# already-established split (task-T5-report.md subsystem 5). A structural
# fact about these two solvers, fixed by the task brief -- not a per-run
# evidence-based judgment call the way CHAOS/tol_for are.
is_timed() {
	case "$1" in
		navierStokes|temporalAberration) return 0 ;;
		*) return 1 ;;
	esac
}
# echoes: tol ssim_min sample_every_seconds (30s run, fixed -- matches
# run_samples.sh's own default and every prior sweep in this family).
timed_params() {
	case "$1" in
		navierStokes)       echo "10.001 0.999 5" ;;  # T5 report: "timed fluid-state samples all passed" at this bar
		temporalAberration) echo "2.001 0.98 10" ;;   # 8-stage delay line settles to a deterministic steady state by t=10 (godot sibling: same solver, same bar)
	esac
}

# CHAOS: cross-backend-divergent by a mechanism actually OBSERVED on this
# backend (docs/CHAOS-GATE.md documents the general phenomenon; the per-name
# reason below is what THIS sweep saw, not what godot saw -- godot's own
# CHAOS set is NOT copied here, see task-T6-report.md). Starts empty per the
# brief ("start strict for every program"); real names land here only after
# strict-tolerance evidence (gathered by an earlier run of THIS sweep, with
# is_chaos still empty, so the fixture went through PHASE 1-3 as a normal
# AUTO case first) rules out a backend bug within this task's scope. Once a
# name IS classified CHAOS, PHASES 1-2 exclude it (matching godot's own
# make-batch-manifest.py EXCLUDED set) -- there is no point minting/
# rendering a fixture whose comparison PHASE 3 will skip unconditionally,
# and doing so would let an unrelated render hiccup on a never-graded
# fixture spuriously trip the batch_rc fallback check below.
is_chaos() {
	case " ${NM_EXTRA_CHAOS:-} " in *" $1 "*) return 0 ;; esac
	case "$1" in
		# (convolutionFeedback WAS classified CHAOS here -- see docs/
		# CHAOS-GATE.md's "Round 2: the CHAOS classification was itself a
		# harness bug" section. Its apparent 99.97%-of-pixels reference-side
		# non-determinism was the SAME RAF-loop-race task-T5-report.md round
		# 3 fixed for agent-state surfaces, just uncaught because o0-o7
		# (render/display surfaces) weren't in that fix's isStateSurface
		# predicate -- convolutionFeedback reads its own prior-frame `o0`
		# back as a feedback input, a genuine hazard the old predicate never
		# covered. Fixed in export-and-render.mjs; the golden is now
		# bit-exact reproducible and the fixture is an ordinary tol_for()
		# NEAR entry below, not excluded from grading.)
		*) return 1 ;;
	esac
}
chaos_reason() {
	case "$1" in
		convolutionFeedback) echo "expansive sharpen/blur feedback loop amplifies cross-GPU floating-point non-determinism over 8 settle frames (intensity:0 isolation proves the plumbing itself is correct)" ;;
		*) echo "test-injected chaos classification (cross-backend floating-point non-determinism)" ;;
	esac
}

# DEFER: structurally unrenderable as a single frame (3D scenes via
# render3d()/read3d(), or live external input via midi()/audio()) -- adapted
# from noisemaker-for-touchdesigner's stage_coverage.py convention (godot's
# own corpus needed no such class). Starts empty: a full-corpus scan for
# read3d(/geo</vol</render3d(/mesh(/midi(/audio(/video(/camera( across all
# 335 parity/programs/*.dsl found ZERO matches -- see task-T6-report.md.
# Kept as a real, tested code path (NM_EXTRA_DEFER test seam) for whoever
# adds a 3D or live-input fixture next, not dead code.
is_defer() {
	case " ${NM_EXTRA_DEFER:-} " in *" $1 "*) return 0 ;; esac
	case "$1" in
		*) return 1 ;;
	esac
}
defer_reason() {
	echo "no single-frame golden is meaningful (3D scene or live external input) -- stage-classification, task-T6-report.md"
}

# --- tol_for(): START STRICT for every program (PORTING-GUIDE.md rule 5) ---
# Tolerance loosenings live ONLY here, per program, each with the mechanism
# actually observed on THIS backend traced in an inline comment. Do NOT
# pre-copy a sibling port's NEAR table -- verify the mechanism still
# manifests under this port's fixed golden-minting protocol first (see
# task-T5-report.md round 3: the harness race that used to make physarum/
# agentsPoints look chaotic is fixed, so a sibling's NEAR entry is not
# evidence of anything here until re-observed).
tol_for() {
	case "$1" in
		# --- Fractal basin/escape-boundary chaos (godot's newton mechanism,
		# re-observed on this backend, now also seen on julia/mandelbrot --
		# godot's own corpus/GPU pairing evidently didn't trigger it for
		# those two, or didn't include them; not copied from godot, verified
		# fresh here). Diff heatmaps (task-T6-report.md) show differing
		# pixels traced PRECISELY along the set's escape/root-basin boundary
		# -- nowhere else -- the textbook signature of a discrete iteration-
		# count flip from a sub-ULP starting difference, not a broken image.
		newton)      echo "255 0.98" ;;  # observed max=251 (0.32% px), ssim=0.9983
		julia)       echo "255 0.98" ;;  # observed max=251 (0.44% px), ssim=0.9993
		mandelbrot)  echo "255 0.93" ;;  # observed max=248 (0.24% px), ssim=0.9342 -- lower SSIM than julia/newton (this repo's single-window global SSIM is more sensitive to a bright boundary trace against a darker field than a proper windowed SSIM would be; pixel-count share is comparable to julia/newton, not larger)
		# --- Edge-following-angle instability near a Sobel/gradient
		# near-zero singularity (godot's hatchPencil/strokesSmudge
		# mechanism; fibers/strayHair/scratches are qt-local names for the
		# same underlying directional-stroke primitive, re-observed here).
		hatchPencil|hatchReferenceColoredPencil|hatchReferenceColoredPencilLeftDiag) echo "241.001 0.999" ;; # coloredPencil/pencil stroke-mask step() flips at the atan(grad) singularity; 207-241 px across the 3 modes (0.011-0.017%), ssim>=0.9998
		strokesReferenceSmudge) echo "65.001 0.999" ;; # same smudge edge-angle instability as strokesSmudge below; 0.92% px, ssim=0.99998
		strokesSmudge)          echo "57.001 0.999" ;; # 1.17% px (exceeds the family's usual <0.03% ceiling, same as godot's own strokesSmudge note -- flagged, not silently widened), ssim=0.99997
		fibers)     echo "122.001 0.93" ;;  # 0.24% px, ssim=0.9331 -- same singularity class, lower SSIM for the same global-SSIM-sensitivity reason as mandelbrot above
		strayHair)  echo "79.001 0.998" ;;  # 0.06% px, ssim=0.99827 -- a handful of sparse stroke-fragment pixels (diff heatmap: one short streak), tightest bar in this class
		scratches)  echo "147.001 0.75" ;;  # 0.29% px, ssim=0.76117 -- several short diverging stroke fragments (diff heatmap), same mechanism; SSIM outlier flagged explicitly like strokesSmudge, not hidden
		# --- Kuwahara equal-variance sector-selection tie (godot's oilPaint
		# mechanism, re-observed here across all 12 mode variants at once:
		# a sub-ULP cross-GPU rounding difference at a variance tie flips
		# which 8-sector-mean a pixel takes, a discontinuous-by-construction
		# selection, not a resampling blur).
		oilPaint|oilPaintReferenceDaubs|oilPaintFresco|oilPaintReferenceFresco|oilPaintDryBrush|oilPaintReferenceDryBrush|oilPaintSponge|oilPaintReferenceSponge|oilPaintKnife|oilPaintReferenceKnife|oilPaintFacet|oilPaintReferenceFacet) echo "107.001 0.999" ;; # 7-107 px across the 12 variants (<=0.02%), ssim>=0.9999995 throughout
		# --- Floyd-Steinberg block-resimulation cascade tie (godot's
		# ditherErrorDiffusion mechanism): a sub-ULP tie in one early cell
		# flips which quantization level a downstream run lands on.
		ditherErrorDiffusion|ditherReferenceErrorDiffusion) echo "85.001 0.999" ;; # 0.058% px, ssim=0.99993 (identical for both -- same effect, direct vs `dither(type:errorDiffusion)` call form)
		# --- NEAREST texture-coordinate boundary ties (godot's edge/
		# uvRemap/rotate/distortion/refract mechanism: a sub-ULP coordinate
		# nudge selects the adjacent texel across this GPU vs the
		# reference's ANGLE/WebGL2 path).
		rotate)      echo "40.001 0.999" ;;   # full-frame rotation: 0.041% px, ssim=0.99998
		distortion)  echo "10.001 0.999" ;;   # Sobel-over-noise + NEAREST coord boundary: 0.0092% px, ssim=1.0
		refract)     echo "6.001 0.999" ;;    # refraction UV lands on a texel boundary: 0.0031% px, ssim=1.0
		lightingRefl) echo "8.001 0.999" ;;   # reflection/refraction offset-sampling boundary: 0.0137% px, ssim=1.0
		scatterAniso) echo "8.001 0.999" ;;   # anisotropic lumGradient Sobel threshold tie: 0.0031% px, ssim=1.0
		pondRipplesReferenceAround) echo "5.001 0.999" ;; # polar/around-mode coordinate remap boundary tie: 0.0015% px, ssim=1.0
		tunnel)      echo "4.001 0.999" ;;    # tunnel coordinate remap boundary tie: 0.0031% px, ssim=1.0
		edge)        echo "6.001 0.999" ;;    # contrast convolution amplifies upstream noise 1-LSB at a boundary: 0.0183% px, ssim=1.0
		# --- Nonlinear amplification of the usual sub-LSB residual via a
		# pow()-shaped specular/tone curve (godot's chrome/plasticWrap/
		# reliefPlaster/unsharpMask mechanism).
		chrome|chromeLiquid) echo "32.001 0.999" ;; # sine tone curve + pow(v,8) rim-specular boost: chrome 0.0046% px ssim=0.9999990, chromeLiquid 0.0076% px ssim=0.9999992
		plasticWrap)                  echo "20.001 0.999" ;; # Blinn half-vector specular pow(x,gloss): 0.0076% px, ssim=0.9999996
		plasticWrapDirected)          echo "7.001 0.999" ;;  # same mechanism via the vec3 lightDirection control: 0.0076% px, ssim=0.9999996
		plasticWrapDirectedReference) echo "30.001 0.999" ;; # exact reference defaults, vec3 lightDirection: 0.0092% px, ssim=0.9999985
		plasticWrapGloss)             echo "27.001 0.999" ;; # higher gloss exponent widens the amplified cluster: 0.0214% px, ssim=0.9999977
		reliefPlaster)          echo "3.001 0.999" ;; # pow(shade,2.0) glossy-squaring term: 0.0015% px, ssim=0.9999999
		reliefReferencePlaster) echo "7.001 0.999" ;; # explicit lightAngle=37 moves the glossy pow boundary: 0.0092% px, ssim=0.9999996
		unsharpMask)             echo "3.001 0.999" ;; # two-pass separable Gaussian subtract-then-rescale: 0.0031% px, ssim=1.0000001
		# --- Discrete rank-selection tie (godot's median mechanism,
		# already independently documented in task-T5-report.md Episode 4:
		# noise()'s own 1-ULP platform difference propagates through
		# median's exact-quickselect neighborhood rank; re-verified here
		# under the SAME fixed golden-minting protocol median was gated
		# against in T5, still manifests).
		median)  echo "14.001 0.999" ;; # 0.0168% px, ssim=0.9999998
		step)    echo "3.001 0.999" ;;  # step() threshold tie on a near-boundary value: 0.0015% px, ssim=1.0
		# --- Fractal distance-estimation raymarch-surface-boundary chaos
		# (final fix wave, synth3d/filter3d smoke coverage): same
		# mechanism class as newton/julia/mandelbrot above (a sub-ULP
		# starting difference in fractal iteration/distance-estimation
		# math flips a discrete decision at a boundary), here manifesting
		# as a raymarch hit/miss or orbit-trap-color flip at the
		# fractal surface's silhouette in a 3D SDF raymarcher instead of
		# a 2D escape-time iteration count. Golden confirmed bit-exact
		# reproducible across 2 independent mints (rules out reference-
		# side nondeterminism, unlike agentsPoints/flow pre-T5-round-3);
		# diff is sparse and localized (689/65536 px = 1.05%, only 4 px
		# >=100 diff), consistent with the boundary-flip signature, not
		# a broken image.
		synth3dFlythrough3d) echo "138.001 0.999" ;; # 1.05% px, ssim=0.99985
		# --- Feedback-loop sharpen/blur cross-GPU rounding tie (RECLASSIFIED
		# from CHAOS -- see is_chaos()'s note and docs/CHAOS-GATE.md).
		# Once the harness's o0-o7 reset closed the real bug (reference-side
		# apparent non-determinism from an uncleared prior-frame render
		# surface), this is an ordinary sparse boundary tie like the classes
		# above, not a whole-image divergence.
		convolutionFeedback) echo "9.001 0.999" ;; # 0.35% px, ssim=1.00000
		*)      echo "2.001 0.98" ;;  # 2.001 = epsilon-tolerant "<=2" (compare.py float round-trip)
	esac
}
reason_for() {
	case "$1" in
		newton|julia|mandelbrot) echo "iterative escape/root-basin classification is chaotic under cross-backend floating-point rounding at the boundary (diff heatmap traces the set boundary exactly)" ;;
		hatchPencil|hatchReferenceColoredPencil|hatchReferenceColoredPencilLeftDiag|strokesReferenceSmudge|strokesSmudge|fibers|strayHair|scratches) echo "near-zero Sobel/gradient singularity makes the edge-following stroke angle discontinuous across GPU compilers" ;;
		oilPaint|oilPaintReferenceDaubs|oilPaintFresco|oilPaintReferenceFresco|oilPaintDryBrush|oilPaintReferenceDryBrush|oilPaintSponge|oilPaintReferenceSponge|oilPaintKnife|oilPaintReferenceKnife|oilPaintFacet|oilPaintReferenceFacet) echo "Kuwahara equal-variance sector selection is discontinuous at cross-GPU rounding ties" ;;
		ditherErrorDiffusion|ditherReferenceErrorDiffusion) echo "Floyd-Steinberg block resimulation cascades an isolated quantization tie" ;;
		rotate|distortion|refract|lightingRefl|scatterAniso|pondRipplesReferenceAround|tunnel|edge) echo "texture-coordinate boundary ties can select adjacent texels across this GPU vs the reference's ANGLE/WebGL2 path" ;;
		chrome|chromeLiquid) echo "sine tone mapping and specular pow amplify isolated sub-LSB height differences" ;;
		plasticWrap|plasticWrapDirected|plasticWrapDirectedReference|plasticWrapGloss) echo "Blinn specular pow amplifies isolated sub-LSB luminance-gradient differences" ;;
		reliefPlaster|reliefReferencePlaster) echo "glossy pow amplifies isolated sub-LSB blur residuals" ;;
		unsharpMask) echo "separable Gaussian subtraction and rescaling amplify isolated sub-LSB residuals" ;;
		median) echo "quickselect can choose a different equal-valued candidate at packed comparison ties (task-T5-report.md Episode 4: noise() 1-ULP input)" ;;
		step) echo "step() threshold tie flips at a near-boundary input value" ;;
		*) echo "strict RGBA8 float round-trip allowance" ;;
	esac
}

# --- PHASE 1: mint goldens (+ export graphs) fresh, in one browser session -
if [ "${SKIP_GOLDEN:-0}" != "1" ]; then
	: > "$GOLDEN_LIST"
	for dsl in "$ROOT"/parity/programs/*.dsl; do
		name=$(basename "$dsl" .dsl)
		is_defer "$name" && continue
		is_timed "$name" && continue
		is_chaos "$name" && continue
		echo "$dsl" >> "$GOLDEN_LIST"
	done
	if [ -s "$GOLDEN_LIST" ]; then
		node "$ROOT/parity/batch-golden.mjs" "$ROOT/parity/out" --list "$GOLDEN_LIST" \
			--size "$SIZE" --time "$TIME" --backend webgl2 --chunk-size 60
		batch_golden_rc=$?
		[ "$batch_golden_rc" -eq 0 ] || echo "[sweep] WARNING: batch-golden.mjs exited $batch_golden_rc (per-program golden checks in PHASE 3 will catch any that are missing)"
	fi
	for dsl in "$ROOT"/parity/programs/*.dsl; do
		name=$(basename "$dsl" .dsl)
		is_timed "$name" || continue
		read -r _tol _ssim every <<EOF
$(timed_params "$name")
EOF
		SHADE_HEADLESS=1 node "$ROOT/parity/export-and-render.mjs" "$dsl" "$ROOT/parity/out" \
			--size "$SIZE" --backend webgl2 --run-seconds 30 --sample-every "$every" \
			|| echo "[sweep] WARNING: golden mint for timed fixture $name exited nonzero"
	done
fi

# --- PHASE 2: batch-render every non-timed/non-defer candidate --------------
batch_rc=0
if [ "${SKIP_RENDER:-0}" != "1" ]; then
	: > "$CANDIDATE_NAMES"
	for dsl in "$ROOT"/parity/programs/*.dsl; do
		name=$(basename "$dsl" .dsl)
		is_defer "$name" && continue
		is_timed "$name" && continue
		is_chaos "$name" && continue
		echo "$name" >> "$CANDIDATE_NAMES"
	done
	if [ -s "$CANDIDATE_NAMES" ]; then
		if [ "${NM_LIVE_DSL:-0}" = "1" ]; then
			while IFS= read -r name; do
				dsl="$ROOT/parity/programs/$name.dsl"
				cand="$ROOT/parity/out/$name.candidate.png"
				rm -f "$cand"
				render_log=$("$NM_RENDER" --dsl "$dsl" --size "${SIZE}x${SIZE}" --time "$TIME" --frames "$FRAMES" --out "$cand" 2>&1)
				rc=$?
				printf '%s\n' "$render_log" | grep -E "RENDERED|ERROR|unimplemented|shader |missing|error" || true
				[ "$rc" -eq 0 ] || { echo "[sweep] live-DSL render FAILED for $name (exit $rc)"; batch_rc=1; }
			done < "$CANDIDATE_NAMES"
		else
			batch_count=$(python3 "$ROOT/parity/make-batch-manifest.py" --root "$ROOT" \
				--output "$BATCH_MANIFEST" --names-file "$CANDIDATE_NAMES" --size "$SIZE" --time "$TIME" --frames "$FRAMES")
			if [ "$batch_count" -gt 0 ]; then
				render_log=$("$NM_RENDER" --batch-manifest "$BATCH_MANIFEST" 2>&1)
				batch_rc=$?
				printf '%s\n' "$render_log" | grep -E "RENDERED|ERROR|unimplemented|shader |missing|error" || true
			fi
		fi
	fi
fi

# --- PHASE 3: grade every program -------------------------------------------
pass=0; fail=0; skip=0; failed=""
for dsl in "$ROOT"/parity/programs/*.dsl; do
	name=$(basename "$dsl" .dsl)

	if is_defer "$name"; then
		reason="$(defer_reason)"
		echo "[DEFER] $name: $reason"
		record_result "$name" DEFER 255 0 "$reason"
		skip=$((skip + 1)); continue
	fi

	if is_timed "$name"; then
		read -r timed_tol timed_ssim timed_every <<EOF
$(timed_params "$name")
EOF
		timed_reason="timed samples reach a deterministic steady state (task-T6-report.md)"
		# No SKIP_RENDER here on purpose: there is no batched or live-DSL
		# timed path (PHASE 2 never touches these two names), so
		# run_samples.sh's OWN already-verified internal render step is the
		# only thing that ever produces a timed candidate, in EITHER sweep
		# mode. See the header comment's SKIP_RENDER / NM_LIVE_DSL entries.
		if timed_output=$(NM_RENDER="$NM_RENDER" bash "$ROOT/parity/run_samples.sh" "$name" "$timed_tol" "$timed_ssim" 30 "$timed_every" "$SIZE" 2>&1); then timed_rc=0; else timed_rc=$?; fi
		r=$(printf '%s\n' "$timed_output" | grep -E "=== SAMPLES:" | tail -1)
		echo "$r"
		if [ "$timed_rc" -ne 0 ]; then
			echo "[FAIL] $name (timed-sampling child exited $timed_rc)"
			record_result "$name" FAIL "$timed_tol" "$timed_ssim" "timed sample harness exited nonzero"
			fail=$((fail + 1)); failed="$failed $name"; continue
		fi
		case "$r" in
			*" pass "*) n_pass="${r#*SAMPLES: $name }"; n_pass="${n_pass%%/*}"
				n_tot="${r#*SAMPLES: $name $n_pass/}"; n_tot="${n_tot%% *}"
				if [ "$n_pass" = "$n_tot" ] && [ "$n_tot" -gt 0 ]; then
					echo "[PASS] $name (timed-sampling $n_pass/$n_tot)"; record_result "$name" TIMED "$timed_tol" "$timed_ssim" "$timed_reason"; pass=$((pass + 1))
				else
					echo "[FAIL] $name (timed-sampling $n_pass/$n_tot)"; record_result "$name" FAIL "$timed_tol" "$timed_ssim" "timed sample comparison failed"; fail=$((fail + 1)); failed="$failed $name"
				fi ;;
			*) echo "[FAIL] $name (timed-sampling: no result)"; record_result "$name" FAIL "$timed_tol" "$timed_ssim" "timed sample harness produced no result"; fail=$((fail + 1)); failed="$failed $name" ;;
		esac
		continue
	fi

	if is_chaos "$name"; then
		reason="$(chaos_reason "$name")"
		echo "[CHAOS] $name: $reason"
		record_result "$name" CHAOS 255 0 "$reason"
		skip=$((skip + 1)); continue
	fi

	if [ ! -f "$ROOT/parity/out/$name.golden.png" ]; then
		echo "[FAIL] $name (no golden)"
		record_result "$name" FAIL 2.001 0.98 "required DSL has no reference golden"
		fail=$((fail + 1)); failed="$failed $name"; continue
	fi

	read -r tol ssim <<EOF
$(tol_for "$name")
EOF
	if run_output=$(SKIP_RENDER=1 NM_RENDER="$NM_RENDER" bash "$ROOT/parity/run.sh" "$name" "$tol" "$ssim" "$SIZE" "$TIME" 2>&1); then run_rc=0; else run_rc=$?; fi
	r=$(printf '%s\n' "$run_output" | grep -E "\[PASS\]|\[FAIL\]" | tail -1)
	echo "$r"
	if [ "$run_rc" -ne 0 ]; then
		record_result "$name" FAIL "$tol" "$ssim" "comparison child exited nonzero"; fail=$((fail + 1)); failed="$failed $name"
	else case "$r" in
		*"[PASS]"*) record_result "$name" AUTO "$tol" "$ssim" "$(reason_for "$name")"; pass=$((pass + 1)) ;;
		*) record_result "$name" FAIL "$tol" "$ssim" "numeric comparison failed the configured policy"; fail=$((fail + 1)); failed="$failed $name" ;;
	esac; fi
done

if [ "$batch_rc" -ne 0 ] && [ "$fail" -eq 0 ]; then
	echo "[FAIL] batched candidate render exited $batch_rc"
	fail=$((fail + 1)); failed="$failed batch-render"
fi
if ! python3 "$ROOT/parity/write-ledger.py" --root "$ROOT" --results "$RESULTS" --output "$LEDGER_PATH"; then
	echo "[FAIL] sweep ledger contains rejecting or incomplete evidence: $LEDGER_PATH"
	if [ "$fail" -eq 0 ]; then fail=$((fail + 1)); failed="$failed ledger"; fi
fi
echo "=== SWEEP: $pass pass / $((pass + fail)) total${skip:+, $skip skipped (chaos/defer)}${failed:+  — FAILED:$failed} ==="
echo "DONE"
[ "$fail" -eq 0 ]
