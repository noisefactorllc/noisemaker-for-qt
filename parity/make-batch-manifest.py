#!/usr/bin/env python3
"""Build ONE nm-render --batch-manifest JSON for the corpus-wide sweep (task T6).

Adapted from the Godot sibling's make-batch-manifest.py, but the manifest
SHAPE here matches THIS repo's actual candidate: nm-render's own
`runBatchManifest()` (qt/tools/nm-render/main.cpp), which reads a flat JSON
ARRAY of {graph, out, size, time, frames} objects -- not Godot's
{"entries": [...]} object with run_seconds/sample_every fields (this port's
nm-render has no batch-manifest timed-sampling mode; timed fixtures are
rendered separately via parity/run_samples.sh's own `--samples` flag, one
process per timed fixture, see parity/sweep.sh).

Unlike Godot's version, this script does NOT hardcode its own EXCLUDED /
TIMED_SAMPLE_EVERY tables -- sweep.sh is the single source of truth for
fixture classification (timed / chaos / defer / normal). The caller passes
the exact candidate set via --names-file (one program name per line); this
script only turns that list into a manifest, using each name's already-
exported parity/out/<name>.graph.json (written by the golden-minting step
that must run before this script, in the same sweep).
"""

import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--names-file", type=Path, required=True,
                         help="one program name per line (no .dsl suffix); blank/# lines skipped")
    parser.add_argument("--size", type=int, default=256)
    parser.add_argument("--time", type=float, default=0.25)
    parser.add_argument("--frames", type=int, default=8)
    args = parser.parse_args()
    out_dir = args.root / "parity" / "out"
    size_text = f"{args.size}x{args.size}"

    names = [
        line.strip() for line in args.names_file.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]

    entries = []
    missing_graph = []
    for name in names:
        graph_path = out_dir / f"{name}.graph.json"
        if not graph_path.exists():
            missing_graph.append(name)
            continue
        candidate = out_dir / f"{name}.candidate.png"
        candidate.unlink(missing_ok=True)
        entries.append({
            "graph": str(graph_path),
            "out": str(candidate),
            "size": size_text,
            "time": args.time,
            "frames": args.frames,
        })

    args.output.write_text(json.dumps(entries, indent=2) + "\n")
    if missing_graph:
        print(f"[make-batch-manifest] {len(missing_graph)} name(s) skipped (no graph.json): "
              f"{' '.join(missing_graph)}")
    print(len(entries))


if __name__ == "__main__":
    main()
