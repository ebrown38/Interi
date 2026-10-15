# SPDX-License-Identifier: MIT
"""Compares Google Benchmark JSON results against a stored baseline.

Uses the median aggregate of each benchmark when repetitions were run (plain results otherwise), and compares
real_time plus every counter whose name ends in `_ms`. A value above baseline * (1 + threshold) is a regression.

Exit codes: 0 = no regression (or --report-only), 1 = regressions, 2 = bad input.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


def _load(path: Path) -> dict[str, dict]:
    """Benchmark name -> result entry (median aggregates preferred)."""
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SystemExit(f"error: cannot read {path}: {error}") from error
    plain: dict[str, dict] = {}
    medians: dict[str, dict] = {}
    for entry in data.get("benchmarks", []):
        name = entry.get("run_name", entry["name"])
        if entry.get("run_type") == "aggregate":
            if entry.get("aggregate_name") == "median":
                medians[name] = entry
        else:
            plain.setdefault(name, entry)
    plain.update(medians)
    return plain


def _metrics(entry: dict) -> dict[str, float]:
    metrics = {"real_time": float(entry["real_time"])}
    for key, value in entry.items():
        if key.endswith("_ms") and isinstance(value, (int, float)):
            metrics[key] = float(value)
    return metrics


def compare(baseline: dict[str, dict], current: dict[str, dict], threshold: float) -> tuple[list[str], int]:
    lines = []
    regressions = 0
    for name in sorted(current):
        if name not in baseline:
            lines.append(f"  new        {name}")
            continue
        base = _metrics(baseline[name])
        now = _metrics(current[name])
        for metric, value in now.items():
            reference = base.get(metric)
            if reference is None or reference <= 0.0:
                continue
            ratio = value / reference
            regressed = ratio > 1.0 + threshold
            regressions += regressed
            mark = "REGRESSED" if regressed else "ok"
            lines.append(f"  {mark:<10} {name} {metric}: {reference:.4g} -> {value:.4g} ({(ratio - 1.0) * 100:+.1f}%)")
    for name in sorted(set(baseline) - set(current)):
        lines.append(f"  missing    {name}")
    return lines, regressions


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("current", type=Path)
    parser.add_argument("--threshold", type=float, default=0.10, help="allowed slowdown fraction (default 0.10)")
    parser.add_argument("--report-only", action="store_true", help="always exit 0 (noise not measured yet)")
    args = parser.parse_args()

    lines, regressions = compare(_load(args.baseline), _load(args.current), args.threshold)
    print(f"{args.current} vs {args.baseline} (threshold {args.threshold * 100:.0f}%)")
    print("\n".join(lines))
    print(f"{regressions} regression(s)")
    return 0 if args.report_only or regressions == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
