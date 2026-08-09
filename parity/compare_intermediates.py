#!/usr/bin/env python3
"""Per-(frame, surface) intermediate-state bisection for the Qt parity harness.

Round-3 tool (task-T5, physarum agent-state parity investigation). Compares
the raw GPU-buffer dumps written by:
  - parity/export-and-render.mjs's NM_DUMP_INTERMEDIATES=1 mode (golden side,
    parity/out/introspect/golden/), and
  - qt/noisemaker/runtime/backend.cpp's matching temporary NM_DUMP_INTERMEDIATES
    probe (candidate side, wherever NM_DUMP_INTERMEDIATES points nm-render at)
frame by frame, surface by surface, and reports the FIRST (frame, surface)
where they diverge beyond tolerance -- localizing a defect to one pass
execution on one frame, rather than "the final blended image differs."

Dump filename convention (shared by both dumpers):
  <fixture>.frame<N>.<bareSurfaceId>.w<W>.h<H>.<dtype>.bin
where dtype is `f32` (raw IEEE754 float32, RGBA-interleaved, GL row order --
row 0 = GL's bottom row, NOT flipped) or `u8` (raw uint8, same layout, for
rgba8-format surfaces where reading as float is a GL_INVALID_OPERATION).

Usage:
  python compare_intermediates.py <fixture> <goldenDir> <candidateDir> \
      [--frames 8] [--tolerance 0.0005]

Exit code 0 = every dumped (frame, surface) pair matches within tolerance
(or one side has no dump for a given pair, which is reported but not fatal --
see --require-surfaces), 1 = a real divergence was found, 2 = bad input.
"""

import argparse
import re
import sys
from pathlib import Path

import numpy as np

FILENAME_RE = re.compile(r"^(?P<fixture>.+)\.frame(?P<frame>\d+)\.(?P<surface>.+)\.w(?P<w>\d+)\.h(?P<h>\d+)\.(?P<dtype>f32|u8)\.bin$")


def discover_surfaces(dump_dir: Path, fixture: str, frame: int):
    """Returns {surfaceName: (path, width, height, dtype)} for one (fixture, frame)."""
    out = {}
    for p in dump_dir.glob(f"{fixture}.frame{frame}.*.bin"):
        m = FILENAME_RE.match(p.name)
        if not m or m.group("fixture") != fixture or int(m.group("frame")) != frame:
            continue
        out[m.group("surface")] = (p, int(m.group("w")), int(m.group("h")), m.group("dtype"))
    return out


def load_buffer(path: Path, width: int, height: int, dtype: str) -> np.ndarray:
    raw = np.fromfile(path, dtype=np.float32 if dtype == "f32" else np.uint8)
    expected = width * height * 4
    if raw.size != expected:
        raise ValueError(f"{path}: expected {expected} {dtype} elements, got {raw.size}")
    return raw.reshape(height, width, 4)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("fixture")
    parser.add_argument("golden_dir", type=Path)
    parser.add_argument("candidate_dir", type=Path)
    parser.add_argument("--frames", type=int, default=8, help="frame indices 0..N-1 to compare (default 8)")
    parser.add_argument("--tolerance", type=float, default=0.0005,
                         help="max-abs-diff allowed for f32 surfaces (normalized units); "
                              "u8 surfaces use ceil(tolerance*255) as an integer-count tolerance")
    args = parser.parse_args()

    first_divergence = None
    any_compared = False

    for frame in range(args.frames):
        golden_surfaces = discover_surfaces(args.golden_dir, args.fixture, frame)
        candidate_surfaces = discover_surfaces(args.candidate_dir, args.fixture, frame)
        names = sorted(set(golden_surfaces) | set(candidate_surfaces))
        for name in names:
            if name not in golden_surfaces:
                print(f"frame{frame} {name}: no golden dump (skipped)")
                continue
            if name not in candidate_surfaces:
                print(f"frame{frame} {name}: no candidate dump (skipped)")
                continue
            gpath, gw, gh, gdtype = golden_surfaces[name]
            cpath, cw, ch, cdtype = candidate_surfaces[name]
            if (gw, gh, gdtype) != (cw, ch, cdtype):
                print(f"frame{frame} {name}: SHAPE/DTYPE MISMATCH golden={gw}x{gh} {gdtype} "
                      f"vs candidate={cw}x{ch} {cdtype}")
                if first_divergence is None:
                    first_divergence = (frame, name, "shape/dtype mismatch")
                continue
            g = load_buffer(gpath, gw, gh, gdtype).astype(np.float64)
            c = load_buffer(cpath, cw, ch, cdtype).astype(np.float64)
            diff = np.abs(g - c)
            max_diff = float(diff.max())
            mean_diff = float(diff.mean())
            tol = args.tolerance * 255.0 if gdtype == "u8" else args.tolerance
            any_compared = True
            status = "OK" if max_diff <= tol else "DIVERGE"
            print(f"frame{frame} {name} [{gdtype} {gw}x{gh}]: max-abs-diff={max_diff:.6g} "
                  f"mean-abs-diff={mean_diff:.6g} tol={tol:.6g} {status}")
            if status == "DIVERGE" and first_divergence is None:
                # Magnitude profile: where in the surface does it diverge, and
                # by how much, channel by channel.
                per_channel_max = diff.reshape(-1, 4).max(axis=0)
                worst_flat = int(np.argmax(diff.reshape(-1, 4).max(axis=1)))
                worst_y, worst_x = divmod(worst_flat, gw)
                first_divergence = (
                    frame, name,
                    f"max-abs-diff={max_diff:.6g} at pixel ({worst_x},{worst_y}), "
                    f"per-channel max-abs-diff={per_channel_max.tolist()}"
                )

    if first_divergence is not None:
        frame, name, detail = first_divergence
        print(f"\nFIRST DIVERGENCE: frame{frame} {name}: {detail}")
        return 1

    if not any_compared:
        print("\nNo comparable (frame, surface) pairs found -- check fixture name and dump dirs.")
        return 2

    print("\nNo divergence found in any dumped (frame, surface) pair.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
