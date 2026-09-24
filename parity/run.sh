#!/usr/bin/env bash
# parity/run.sh <name> [tolerance] [ssim_min] [size] [time]
#
# Render the Qt candidate for a Tier-1 program and compare it against the
# existing golden. Goldens + graph JSON are produced by the reference harness
# (parity/export-and-render.mjs) into parity/out/ — see parity/README.md.
#
# nm-render owns its own offscreen GL context (QOffscreenSurface), unlike the
# Godot lineage's --headless RenderingDevice restriction, so no window
# positioning / non-headless workaround is needed here.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
NAME="${1:?usage: run.sh <name> [tol] [ssim_min] [size] [time]}"
# Default 2.001 (not 2): compare.py computes max-abs-diff as float ((x-y)/255*255),
# so a TRUE 8-bit diff of 2 reports ~2.0000035 and would false-fail a strict "<=2".
# 2.001 accepts true diffs <=2 and still rejects >=3.
TOL="${2:-2.001}"
SSIM="${3:-0.98}"
SIZE="${4:-256}"
# Matches export-and-render.mjs's own default paused-time (0.25) — the golden
# and candidate MUST be captured at the same normalized time.
TIME="${5:-0.25}"
NM_RENDER="${NM_RENDER:-$ROOT/qt/build/nm-render}"
PY="$ROOT/parity/.venv/bin/python"
# A venv created by a native Windows Python keeps its interpreter in Scripts/.
if [ ! -e "$PY" ] && [ -e "$ROOT/parity/.venv/Scripts/python.exe" ]; then
	PY="$ROOT/parity/.venv/Scripts/python.exe"
fi
GRAPH="$ROOT/parity/out/$NAME.graph.json"
GOLD="$ROOT/parity/out/$NAME.golden.png"
CAND="$ROOT/parity/out/$NAME.candidate.png"
# Optional mesh0 input: the fixture's sidecar OBJ (the golden harness loads
# the same file; see meshPlan in export-and-render.mjs).
MESH="$ROOT/parity/programs/$NAME.obj"
MESH_ARGS=()
[ -f "$MESH" ] && MESH_ARGS=(--mesh "$MESH")
# Host pixels the reference sampled for each external texture id
# (<name>.<texId>.png, written by the golden minter).
EXTERNAL_ARGS=()
for texture in "$ROOT/parity/out/$NAME".*_step_*.png; do
	[ -f "$texture" ] || continue
	texture_id="${texture##*/}"
	texture_id="${texture_id#"$NAME".}"
	texture_id="${texture_id%.png}"
	[[ "$texture_id" =~ ^[A-Za-z][A-Za-z0-9]*_step_[0-9]+$ ]] || continue
	EXTERNAL_ARGS+=(--external-texture "$texture_id=$texture")
done

[ -f "$GRAPH" ] || { echo "missing graph: $GRAPH (run: node parity/export-and-render.mjs parity/programs/$NAME.dsl parity/out)"; exit 2; }
[ -f "$GOLD" ]  || { echo "missing golden: $GOLD (run the reference harness)"; exit 2; }

if [ "${SKIP_RENDER:-0}" != "1" ]; then
	rm -f "$CAND"
	set +e
	render_log=$("$NM_RENDER" --graph "$GRAPH" --size "${SIZE}x${SIZE}" --time "$TIME" --frames 8 --out "$CAND" ${MESH_ARGS[@]+"${MESH_ARGS[@]}"} ${EXTERNAL_ARGS[@]+"${EXTERNAL_ARGS[@]}"} 2>&1)
	render_rc=$?
	set -e
	printf '%s\n' "$render_log" | grep -E "RENDERED|ERROR|unimplemented|shader |missing|error" || true
	[ "$render_rc" -eq 0 ] || { echo "FAIL: nm-render exited $render_rc for $NAME"; exit 1; }
fi
[ -f "$CAND" ] || { echo "FAIL: nm-render produced no candidate for $NAME"; exit 1; }

"$PY" "$ROOT/parity/compare.py" "$GOLD" "$CAND" \
	--name "$NAME" --tolerance "$TOL" --ssim-min "$SSIM" \
	--report "$ROOT/parity/out/$NAME.report.json"
