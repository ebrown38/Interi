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

## Debris capacity (M8.4, 7 workers)

Recorded 2026-10-15 in `destruction_runtime.json` (same command as above), with unrelated background load on the
machine (load average ~10), so they lean pessimistic.

| What | Result |
|---|---|
| Debris bodies falling and piling, 500 / 1k / 2k / 4k | physics step mean 0.32 / 0.56 / 1.1 / 2.9 ms, worst frame 0.45 / 0.81 / 1.5 / 5.8 ms; `DebrisSystem::Update` 0.03-0.10 ms |
| Particles (CPU update), 10k / 50k / 100k | 0.20 / 1.0 / 2.1 ms per Update (~20 ns each) |
| Churn at a 1k cap, 0 / 50 / 200 new pieces per frame | physics step 0.50 / 0.48 / 0.51 ms (body creation does not show); Update + adoption 0.04 / 0.22 / 0.72 ms |

Proposed caps (`DebrisSettings` defaults; review before locking): `maxBodies` 2048, at which the worst frame is ~1.5 ms
and leaves the rest of the 5 ms physics gate to structures and characters. `maxParticles` 16384 (~0.35 ms on the CPU).
The 4k-body worst frame (5.8 ms) breaks the physics gate on its own. Body churn costs Jolt almost nothing, so bodies
are not recycled. Adoption (ECS entity changes) is ~3.5 us per piece. GPU particle counts are measured in M9, when
particles are rendered.

## Destruction rendering and GPU particles (M9)

`destruction_render.json` (same flags as above, `interi_destruction_render_benchmarks`), recorded 2026-10-16:

| What | Result |
|---|---|
| `ChunkRenderSet::Cover`, 2048 leaves (64 regions x 32) | intact: 11 us (1 mesh); every region lost a leaf: 21 us (2048 meshes) |
| `SpawnBreakParticles`, concrete, 10 / 100 / 1000 bond breaks | 1.9 us / 19 us / 0.21 ms (10 particles per break, ~20 ns each) |

GPU particle capacity (the GPU count M8.4 deferred), `interi_render_gpu_benchmarks` (not stored: it measures the
machine's GPU, and CI only has lavapipe). One frame of simulate + draw at 1280 x 720, every particle alive and on
screen, submitted and waited for (so it includes ~0.25 ms of submission and fence latency), on an AMD Radeon
RX 9070 XT (RADV):

| Particles | 16k | 64k | 256k |
|---|---|---|---|
| Frame | 0.28 ms | 0.34 ms | 0.75 ms |

`ParticleSettings::capacity` defaults to 65536; a console-class GPU has room for several times that, so the cap is
set by the CPU side (`BreakEffects` spawning) and by overdraw of large dust particles close to the camera, which this
benchmark does not stress. Review together with the debris caps before locking.
