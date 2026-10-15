# Destruction performance baselines (M7.6)

Google Benchmark JSON from the `linux-clang-release` preset, median of repetitions (5 for `destruction`, 3 for
`destruction_runtime`), recorded 2026-10-15 on the development machine (32 logical CPUs; benchmarks use 0 or 7
workers, the console proxy). Refresh with:

```sh
build/linux-clang-release/benchmarks/destruction/interi_destruction_benchmarks --benchmark_filter='BM_Stress|BM_Label' \
  --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
  --benchmark_out=benchmarks/baselines/linux-clang-release/destruction.json --benchmark_out_format=json
build/linux-clang-release/benchmarks/destruction_runtime/interi_destruction_runtime_benchmarks \
  --benchmark_repetitions=3 --benchmark_report_aggregates_only=true \
  --benchmark_out=benchmarks/baselines/linux-clang-release/destruction_runtime.json --benchmark_out_format=json
```

Compare a run with `tools/bench_compare/compare_benchmarks.py <baseline.json> <current.json> [--threshold 0.10]`.

## Measured (7 workers)

| What | Result |
|---|---|
| Cold stress solve, 1k / 10k / 50k bonds | 1.4 ms / 28 ms / 251 ms (65 / 173 / 350 CG iterations) |
| One CG iteration | 16-60 ns per bond (~60 M bond-iterations/s at 10k-50k bonds) |
| Island labelling, 50k bonds | 0.09 ms |
| Collapse, 1836 chunks / 3275 bonds | worst frame: destruction 1.7 ms, physics 0.9 ms |

A cold solve of a large structure does not fit one frame: `DestructionSettings::stressWorkBudget` (150k
bond-iterations, ~3 ms) spreads it over frames, warm-started.

## Gates (proposed, not enforced)

From the 16.6 ms frame: destruction work (stress + islands + pieces) <= 4 ms per frame, physics step <= 5 ms. The
nightly CI `benchmarks` job only reports against these baselines: they come from a development machine, not the CI
runner, and local run-to-run variation already reaches 13% (coefficient of variation) on some benchmarks. Lock the
gates and a regression threshold after reviewing runner results.

The first runner run (2026-10-15, `workflow_dispatch`) was 20-90% slower than these baselines, most where the 7
workers oversubscribe the runner's 4 vCPUs (e.g. 50k-bond cold solve at 7 workers: 251 -> 355 ms; worst collapse
frame: destruction 1.7 -> 2.1 ms, physics 0.9 -> 1.5 ms). To gate on CI, take the runner's `destruction-benchmarks`
artifact from a few nights as the baseline instead.
