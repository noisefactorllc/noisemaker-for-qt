#!/usr/bin/env bash
# parity/run_samples.sh <name> [tol] [ssim_min] [run_seconds] [sample_every] [size]
#
# STATEFUL-SIM parity (reference 30s/5s sampling): render the Qt candidate as
# a TIMED SERIES (run_seconds of sim-time, captured every sample_every) and
# compare each timestep against the matching golden sample. Unlike
# parity/run.sh (single pinned frame, which freezes fluid/feedback sims at
# the seed), this evolves the sim to a developed state -- the meaningful
# parity test for navierStokes & friends. Copied/adapted from the godot
# sibling's parity/run_samples.sh: same structure, same default
# tolerances (this class of fixture is inherently noisier than a single
# pinned frame -- floating-point divergence compounds over hundreds of
# solver steps -- and the godot port already established these as the
# right bar for the SAME solver family; not a per-repo guess).
#
# nm-render owns its own offscreen GL context (QOffscreenSurface), unlike
# the Godot lineage's --headless RenderingDevice restriction, so no window
# positioning / non-headless workaround is needed here.
#
# Goldens + graph must pre-exist in parity/out/ (produced by the reference
# harness):
#   node parity/export-and-render.mjs parity/programs/<name>.dsl parity/out \
#       --size 256 --backend webgl2 --run-seconds 30 --sample-every 5
#
#   bash parity/run_samples.sh <name>
#
# --samples given to nm-render is <totalFrames>:<sampleEvery>, BOTH IN
# FRAMES (see qt/tools/nm-render/render_samples.cpp) -- this script is the
# seconds<->frames adapter: run_seconds*60 : sample_every*60.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:?usage: run_samples.sh <name> [tol] [ssim] [run_seconds] [sample_every] [size]}"
TOL="${2:-10}"
SSIM="${3:-0.998}"
RUN="${4:-30}"
EVERY="${5:-5}"
SIZE="${6:-256}"
NM_RENDER="${NM_RENDER:-$ROOT/qt/build/nm-render}"
PY="$ROOT/parity/.venv/bin/python"
GRAPH="$ROOT/parity/out/$NAME.graph.json"
[ -f "$GRAPH" ] || { echo "missing graph: $GRAPH (run export-and-render.mjs first)"; exit 2; }
[ "$EVERY" -gt 0 ] 2>/dev/null || { echo "sample_every must be a positive integer"; exit 2; }
rm -f "$ROOT/parity/out/$NAME.report.t"*.json

if [ "${SKIP_RENDER:-0}" != "1" ]; then
	rm -f "$ROOT/parity/out/$NAME.candidate.png"
	t=$EVERY
	while [ "$t" -le "$RUN" ]; do
		rm -f "$ROOT/parity/out/$NAME.candidate.t$t.png"
		t=$((t + EVERY))
	done
	TOTAL_FRAMES=$((RUN * 60))
	EVERY_FRAMES=$((EVERY * 60))
	set +e
	render_log=$("$NM_RENDER" --graph "$GRAPH" --size "${SIZE}x${SIZE}" \
		--samples "${TOTAL_FRAMES}:${EVERY_FRAMES}" \
		--out "$ROOT/parity/out/$NAME.candidate.png" 2>&1)
	render_rc=$?
	set -e
	printf '%s\n' "$render_log" | grep -E "RENDERED|ERROR|unimplemented|shader |missing|error" || true
	if [ "$render_rc" -ne 0 ]; then
		echo "[FAIL] $NAME timed renderer exited $render_rc"
		exit "$render_rc"
	fi
fi

pass=0; total=0
t=$EVERY
while [ "$t" -le "$RUN" ]; do
	g="$ROOT/parity/out/$NAME.golden.t$t.png"
	c="$ROOT/parity/out/$NAME.candidate.t$t.png"
	total=$((total + 1))
	if [ -f "$g" ] && [ -f "$c" ]; then
		report="$ROOT/parity/out/$NAME.report.t$t.json"
		rm -f "$report"
			if compare_output=$("$PY" "$ROOT/parity/compare.py" "$g" "$c" --name "${NAME}_t$t" \
				--tolerance "$TOL" --ssim-min "$SSIM" --report "$report" 2>&1); then compare_rc=0; else compare_rc=$?; fi
			r=$(printf '%s\n' "$compare_output" | grep -E "\[PASS\]|\[FAIL\]" | tail -1)
			echo "$r"
			if [ "$compare_rc" -eq 0 ]; then case "$r" in *"[PASS]"*) pass=$((pass + 1)) ;; esac; fi
	else
		echo "[FAIL] ${NAME}_t$t: missing expected $( [ -f "$g" ] && echo candidate || echo golden ) sample"
	fi
	t=$((t + EVERY))
done
echo "=== SAMPLES: $NAME $pass/$total pass (tol=$TOL ssim>=$SSIM, ${RUN}s every ${EVERY}s) ==="
[ "$pass" -eq "$total" ] && [ "$total" -gt 0 ]
