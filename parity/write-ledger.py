#!/usr/bin/env python3
"""Build a policy-accurate Qt parity ledger from one sweep.

Adapted from the Godot sibling's write-ledger.py: same row schema
(program/effect/mode/golden/candidate/verdict/max_abs_diff/mean_abs_diff/ssim/
passed/skipped/policy{...}), same TIMED/AUTO report-vs-policy re-validation
(never trust a shell [PASS] line on its own -- re-derive the verdict from the
actual report.json evidence). Two additions specific to this task:

  - DEFER is accepted as a third "skipped, not a failure" verdict alongside
    SKIP/CHAOS (task-T6-brief.md's external-input/3D stage-classification;
    see parity/sweep.sh's header for why this corpus currently has zero
    DEFER rows).
  - A universe-completeness check: the set of programs in --results MUST
    equal exactly the set of parity/programs/*.dsl files on disk (PORTING-
    GUIDE.md rule 5's "never weaken a gate" extends to the ledger itself --
    a silently-dropped program is a hidden FAIL, not a passed sweep).
"""

import argparse
import json
import re
from pathlib import Path


STRICT_TOLERANCE = 2.001
STRICT_SSIM_MIN = 0.98


def explicit_effect_arguments(source, effect):
    marker = f".{effect}("
    start = source.rfind(marker)
    if start < 0:
        return ""
    start += len(marker)
    depth = 0
    quote = None
    escaped = False
    chars = []
    for char in source[start:]:
        if quote:
            chars.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == quote:
                quote = None
            continue
        if char in {'"', "'"}:
            quote = char
            chars.append(char)
        elif char == "(":
            depth += 1
            chars.append(char)
        elif char == ")":
            if depth == 0:
                break
            depth -= 1
            chars.append(char)
        else:
            chars.append(char)
    return re.sub(r"\s+", " ", "".join(chars)).strip()


def effect_metadata(root, program):
    dsl = root / "parity" / "programs" / f"{program}.dsl"
    effect = None
    source = ""
    if dsl.exists():
        source = dsl.read_text()
        calls = [name for name in re.findall(r"\.([A-Za-z][A-Za-z0-9_]*)\(", source) if name not in {"write", "render"}]
        if calls:
            effect = calls[-1]
    mode = explicit_effect_arguments(source, effect) if effect else ""
    if not mode and effect and program.startswith(effect + "_"):
        mode = program[len(effect) + 1 :]
    mode = mode or "default"
    return (f"filter/{effect}" if effect else None), mode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--results", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output if args.output.is_absolute() else args.root / args.output
    rows = []

    for line in args.results.read_text().splitlines():
        if not line:
            continue
        program, requested, tolerance, ssim_min, reason = line.split("\t", 4)
        policy_tolerance = float(tolerance)
        policy_ssim_min = float(ssim_min)
        report_path = args.root / "parity" / "out" / f"{program}.report.json"
        report = json.loads(report_path.read_text()) if report_path.exists() else {}
        verdict = requested
        golden = f"parity/out/{program}.golden.png"
        candidate = f"parity/out/{program}.candidate.png"
        samples = None
        if requested == "TIMED":
            timed_reports = sorted(
                (args.root / "parity" / "out").glob(f"{program}.report.t*.json"),
                key=lambda path: int(re.search(r"\.t(\d+)\.json$", path.name).group(1)),
            )
            samples = []
            for timed_report_path in timed_reports:
                second = int(re.search(r"\.t(\d+)\.json$", timed_report_path.name).group(1))
                timed_report = json.loads(timed_report_path.read_text())
                golden_path = f"parity/out/{program}.golden.t{second}.png"
                candidate_path = f"parity/out/{program}.candidate.t{second}.png"
                max_diff = timed_report.get("max_abs_diff")
                sample_ssim = timed_report.get("ssim")
                try:
                    policy_matches = (
                        timed_report.get("name") == f"{program}_t{second}"
                        and float(timed_report.get("tolerance")) == policy_tolerance
                        and float(timed_report.get("ssim_min")) == policy_ssim_min
                    )
                    within_policy = (
                        float(max_diff) <= policy_tolerance
                        and float(sample_ssim) >= policy_ssim_min
                    )
                except (TypeError, ValueError):
                    policy_matches = False
                    within_policy = False
                samples.append({
                    "time_seconds": second,
                    "golden": golden_path,
                    "candidate": candidate_path,
                    "max_abs_diff": max_diff,
                    "mean_abs_diff": timed_report.get("mean_abs_diff"),
                    "ssim": sample_ssim,
                    "passed": bool(timed_report.get("passed")) and policy_matches and within_policy,
                    "policy_matches": policy_matches,
                })
            evidence_complete = bool(samples) and all(
                sample["passed"]
                and (args.root / sample["golden"]).exists()
                and (args.root / sample["candidate"]).exists()
                for sample in samples
            )
            if not evidence_complete:
                verdict = "FAIL"
                reason = "timed sample evidence is missing or contains a failed comparison"
            golden = [sample["golden"] for sample in samples]
            candidate = [sample["candidate"] for sample in samples]
            if evidence_complete:
                report = {
                    "max_abs_diff": max(sample["max_abs_diff"] for sample in samples),
                    "mean_abs_diff": max(sample["mean_abs_diff"] for sample in samples),
                    "ssim": min(sample["ssim"] for sample in samples),
                }
                if report["max_abs_diff"] <= STRICT_TOLERANCE and report["ssim"] >= STRICT_SSIM_MIN:
                    verdict = "PASS"
                else:
                    verdict = "NEAR"
        if requested == "AUTO":
            try:
                report_valid = (
                    report.get("name") == program
                    and bool(report.get("passed"))
                    and float(report.get("tolerance")) == policy_tolerance
                    and float(report.get("ssim_min")) == policy_ssim_min
                    and float(report.get("max_abs_diff")) <= policy_tolerance
                    and float(report.get("ssim")) >= policy_ssim_min
                )
            except (TypeError, ValueError):
                report_valid = False
            if not report_valid:
                verdict = "FAIL"
                reason = "shell status was not backed by a complete passing comparison report"
            elif report.get("max_abs_diff", float("inf")) <= STRICT_TOLERANCE and report.get("ssim", 0) >= STRICT_SSIM_MIN:
                verdict = "PASS"
            else:
                verdict = "NEAR"
        effect, mode = effect_metadata(args.root, program)
        rows.append({
            "program": program,
            "effect": effect,
            "mode": mode,
            "golden": golden,
            "candidate": candidate,
            "verdict": verdict,
            "max_abs_diff": report.get("max_abs_diff"),
            "mean_abs_diff": report.get("mean_abs_diff"),
            "ssim": report.get("ssim"),
            "passed": verdict in {"PASS", "NEAR"},
            "skipped": verdict in {"SKIP", "CHAOS", "DEFER"},
            "policy": {
                "tolerance": policy_tolerance,
                "ssim_min": policy_ssim_min,
                "strict_tolerance": STRICT_TOLERANCE,
                "strict_ssim_min": STRICT_SSIM_MIN,
                "reason": reason,
            },
        })
        if samples is not None:
            rows[-1]["samples"] = samples

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(sorted(rows, key=lambda row: row["program"]), indent=2) + "\n")

    exit_code = 0
    if any(row["verdict"] == "FAIL" for row in rows):
        exit_code = 1

    programs_dir = args.root / "parity" / "programs"
    if programs_dir.is_dir():
        universe = {path.stem for path in programs_dir.glob("*.dsl")}
        covered = {row["program"] for row in rows}
        missing = sorted(universe - covered)
        extra = sorted(covered - universe)
        if missing or extra:
            if missing:
                print(f"[write-ledger] UNIVERSE MISMATCH: {len(missing)} program(s) on disk have no "
                      f"ledger row: {' '.join(missing)}")
            if extra:
                print(f"[write-ledger] UNIVERSE MISMATCH: {len(extra)} ledger row(s) have no matching "
                      f".dsl on disk: {' '.join(extra)}")
            exit_code = 1

    if exit_code:
        raise SystemExit(exit_code)


if __name__ == "__main__":
    main()
