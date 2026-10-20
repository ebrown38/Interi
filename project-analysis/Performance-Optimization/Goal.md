# Performance Optimization — Goal State

## Vision
Interi's fixed step and render frame have documented budgets, for example 16.6 ms per frame at 60 Hz or 8.3 ms at 120 Hz, plus a fixed-step budget derived from measured baselines. Both are visible in a Tracy capture with named CPU zones for every engine system and GPU timings per frame-graph pass.

In steady state, the per-step path (`GameSession::Step` → modules → `SceneRuntime::Step` → chains → hierarchy, triggers and nav) does no heap allocation and no whole-world map rebuilds, and its cost scales with activity rather than total object count. The editor never stalls the GPU per mesh or per frame.

Benchmarks for every hot system run in an optimized, assert-free shipping configuration, and CI catches allocation and time regressions. All of this holds while results stay deterministic and bit-identical at any thread count.

## Acceptance criteria
- [ ] AC-1: `SceneRuntime::Step` builds at most one hierarchy index per step, and none when the step made no structural change. `HierarchyIndex` lookups and pre-order traversal do not allocate per node. A benchmark at 10k objects reports zero heap allocations per steady-state step from hierarchy and transform propagation. (resolves PERF-001)
- [ ] AC-2: `ChainExecutor::Step` builds its stable-id → entity lookup only when effects run, and `GameSession::StepNavigation` reuses the runtime's index instead of calling `BuildHierarchy`. (resolves PERF-001)
- [ ] AC-3: `NavMesh::FindPath` performs no `dtAllocNavMeshQuery`/`init` per call in steady state, and the nav tests pass unchanged. Any re-plan policy change is documented in `docs/animation-navigation-localization.md` and pinned by tests. (resolves PERF-002)
- [ ] AC-4: An editor run with 300 or more distinct primitive meshes (`level_editor --video-driver offscreen --frames 120`) performs zero mesh uploads and zero `WaitForValue` calls in frames 2-120. Mesh uploads are batched on the timeline, not submitted and waited per mesh. (resolves PERF-003)
- [ ] AC-5: A `linux-clang-profile` capture of the starter sample shows named zones for `GameSession::Step`, `SceneRuntime::Step`, `ChainExecutor::Step`, `PropagateTransforms`, `ScanTriggers`, `StepNavAgents`, `CommandBuffers::Playback`, `FrameGraph::Compile`/`Execute`, `EditorRenderer::Render` and the audio mix. It also shows a frame mark every frame without opting in, named worker and audio threads, and GPU timings per frame-graph pass. (resolves PERF-004)
- [ ] AC-6: A frame budget and a fixed-step budget are written down in `docs/` with the measured baseline they came from. (resolves PERF-004, PERF-008)
- [ ] AC-7: `ChainExecutor::Step` reuses its activation, outcome and firing buffers across steps. `AddTriggerFirings` visits only nodes matching the raised trigger. `ChainDeterminism.MatchesCrossPlatformGoldenHash` is unchanged. (resolves PERF-005)
- [ ] AC-8: `ScanTriggers` visits only entities with a trigger component, reads `Transform` from the chunk, and copies no tag string per volume per step. Overlap results and event order are identical to the brute-force oracle in tests. (resolves PERF-006)
- [ ] AC-9: After two `SceneRuntime::Step`s with no chain activity, `Changed<Transform>` and `Changed<LocalTransformComponent>` match zero chunks for the second step, and a unit test pins this. (resolves PERF-007)
- [ ] AC-10: The `benchmarks/` folder covers scene hierarchy and propagation, `SceneRuntime::Step`, `ChainExecutor::Step`, trigger scan and overlap, nav agents and HUD `BuildFrame`, each reporting allocations per iteration. A CI job runs them in an optimized configuration and compares them with `tools/bench_compare/compare_benchmarks.py` against a committed baseline. Allocation regressions fail the job. (resolves PERF-008, PERF-013)
- [ ] AC-11: `linux-clang-shipping` and `windows-msvc-shipping` presets exist with `CMAKE_BUILD_TYPE=Release`, `INTERI_SHIPPING` defined and IPO/LTO enabled, and CI builds at least one of them. (resolves PERF-009)
- [ ] AC-12: `FrameGraph::Compile`/`Execute` and `CommandBuffers::Playback` reuse member scratch storage, and `BM_FrameGraph_DeclareCompileExecute` reports fewer allocations than the baseline. (resolves PERF-010)
- [ ] AC-13: The Vulkan device creates pipelines through a persisted `VkPipelineCache`. (resolves PERF-011)
- [ ] AC-14: ImGui texture uploads do not call `WaitForValue` on the render thread. (resolves PERF-012)
- [ ] AC-15: The audio thread still neither allocates nor locks in `Mix`, `ParallelFor*` keeps per-chunk local accumulation, and the forward renderer keeps persistent mapped per-frame scene buffers and frame-graph transient reuse. (preserves PERF-014)

## Guardrails to keep it that way
- CI benchmark job with committed baselines and a strict allocation-count gate (`compare_benchmarks.py`). Timing starts in report-only mode until runner noise is understood.
- A rule in `CLAUDE.md`: per-step and per-frame code must not allocate in steady state. Use member scratch buffers, sorted flat arrays (not `std::map`) for id indexes, and a `Get` before `GetMut` whenever the write is conditional.
- An `INTERI_ZONE` is required on every new system, step or render pass (review checklist item). A `profile` preset build runs in CI so instrumentation keeps compiling.
- Every new `IDevice` feature, such as timestamps, comes with `MockDevice` and `NullDevice` support in the same change (existing rule).
- Determinism tests (`HashWorld` 1/4/32 threads, the chain golden hash, `GameSession` 0/1/4/16 workers) stay mandatory for any performance change on the simulation path.

## Out of scope
- GPU shader-level optimization (HLSL cost, overdraw, cluster sizes). This needs GPU captures, which static analysis can't provide.
- Performance of `modules/physics` (Jolt) and `modules/destruction`. They are separate projects and not analyzed here.
- Offline tooling throughput (asset import `ModelProcess`, cook times, Python tools), except where it blocks the editor frame.
- Console (PS5/Xbox) I/O and submission paths, which live in private repositories.
- Micro-optimizing SIMD math kernels, which already have scalar/SIMD parity tests and benchmarks.
