#!/usr/bin/env bash
# parity/run.sh <name> [tolerance] [ssim_min] [size] [time]
#
# Render the Qt candidate for a Tier-1 program and compare it against the
# existing golden. Goldens + graph JSON are produced by the reference harness
# (parity/export-and-render.mjs) into parity/out/.
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
# Portable parity definitions are test fixtures. Give nm-render a private copy
# of its data root with their shaders, so they are never installed as effects.
PORTABLE="$ROOT/parity/portable/$NAME.portable.json"
if [ -f "$PORTABLE" ]; then
	DATA_ROOT="$(mktemp -d)"
	trap 'rm -rf "$DATA_ROOT"' EXIT
	cp -R "$ROOT/qt/noisemaker/." "$DATA_ROOT/"
	node - "$PORTABLE" "$DATA_ROOT" <<'NODE'
const fs = require('node:fs')
const path = require('node:path')
const definition = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'))
const valid = value => typeof value === 'string' && /^[A-Za-z][A-Za-z0-9]*$/.test(value)
if (!valid(definition.namespace) || !valid(definition.func)) throw Error('invalid portable fixture name')
for (const [program, shaders] of Object.entries(definition.shaders)) {
  if (!valid(program)) throw Error('invalid portable program name')
  const dir = path.join(process.argv[3], 'shaders', 'effects', definition.namespace, definition.func)
  fs.mkdirSync(dir, { recursive: true })
  fs.writeFileSync(path.join(dir, program + '.frag'), shaders.glsl)
  if (shaders.vertex) fs.writeFileSync(path.join(dir, program + '.vert'), shaders.vertex)
}
NODE
else
	DATA_ROOT="${NOISEMAKER_QT_DATA_ROOT:-}"
fi
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
# NM_REFERENCE_OVERLAYS=1 also passes each asyncInit overlay the reference
# uploaded (<name>.node_<N>_<texture>.png), which replaces the overlay the port
# would trace, so only the shader passes are graded. By default the port's own
# trace is graded: the reference draws its overlays on Chromium's software
# canvas on every host, which the port reproduces byte for byte
# (parity/check_async_overlay.mjs).
if [ "${NM_REFERENCE_OVERLAYS:-0}" = "1" ]; then
	for texture in "$ROOT/parity/out/$NAME".node_*.png; do
		[ -f "$texture" ] || continue
		texture_id="${texture##*/}"
		texture_id="${texture_id#"$NAME".}"
		texture_id="${texture_id%.png}"
		[[ "$texture_id" =~ ^node_[0-9]+_[A-Za-z][A-Za-z0-9]*$ ]] || continue
		EXTERNAL_ARGS+=(--external-texture "$texture_id=$texture")
	done
fi

[ -f "$GRAPH" ] || { echo "missing graph: $GRAPH (run: node parity/export-and-render.mjs parity/programs/$NAME.dsl parity/out)"; exit 2; }
[ -f "$GOLD" ]  || { echo "missing golden: $GOLD (run the reference harness)"; exit 2; }

if [ "${SKIP_RENDER:-0}" != "1" ]; then
	rm -f "$CAND"
	set +e
	render_log=$(NOISEMAKER_QT_DATA_ROOT="$DATA_ROOT" "$NM_RENDER" --graph "$GRAPH" --size "${SIZE}x${SIZE}" --time "$TIME" --frames 8 --out "$CAND" ${MESH_ARGS[@]+"${MESH_ARGS[@]}"} ${EXTERNAL_ARGS[@]+"${EXTERNAL_ARGS[@]}"} 2>&1)
	render_rc=$?
	set -e
	printf '%s\n' "$render_log" | grep -E "RENDERED|ERROR|unimplemented|shader |missing|error" || true
	[ "$render_rc" -eq 0 ] || { echo "FAIL: nm-render exited $render_rc for $NAME"; exit 1; }
fi
[ -f "$CAND" ] || { echo "FAIL: nm-render produced no candidate for $NAME"; exit 1; }

"$PY" "$ROOT/parity/compare.py" "$GOLD" "$CAND" \
	--name "$NAME" --tolerance "$TOL" --ssim-min "$SSIM" \
	--report "$ROOT/parity/out/$NAME.report.json"
