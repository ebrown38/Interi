# Performance Optimization — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

**Constraints for every step below (from CLAUDE.md):**
- Iteration stays in stable-id order. Use sorted vectors or ordered maps, and never let `unordered_map` iteration order reach results.
- Results stay bit-identical at 1, 4 and 32 threads (`HashWorld` comparisons, `GameSession` 0/1/4/16 worker tests).
- Any change to chain evaluation, ordering, the core library or Random changes `ChainDeterminism.MatchesCrossPlatformGoldenHash`. Allocation-only changes, such as reusing buffers across steps without changing order, leave it unchanged. Re-run `interi_chain_stress_tests` to prove it.
- No `rcp`/`rsqrt` in anything that feeds simulation.
- Run the presets debug, release, asan, tsan and tidy before committing.
- Run `impact` on every symbol before editing it, as CLAUDE.md requires.

## Recommended order
1. PERF-004, PERF-008, PERF-009: instrumentation, benchmarks and an optimized configuration come first. Every other fix needs a before and after number, and measuring with asserts on gives misleading numbers.
2. PERF-001, PERF-007: the per-step hierarchy and change-tick fixes touch the same code (`SceneRuntime::Step`, `PropagateTransforms`), so do them together.
3. PERF-002: nav query pooling, a cheap win with no behavior change. Do the optional re-plan policy afterwards, as a separate behavior change.
4. PERF-003, PERF-012: editor stalls, isolated to `tools/editor` and `editor_ui`.
5. PERF-005, PERF-006: chain executor and trigger allocations. Re-check the golden hash.
6. PERF-010, PERF-011: low-cost cleanups.

## PERF-001 — Every fixed step rebuilds `std::map`-based indexes of all scene objects three to four times
- **Severity / Effort:** High / L
- **Depends on:** PERF-004 (zones to measure), PERF-008 (step benchmark)
- **Steps:**
  1. Add a `SceneRuntime`/`GameSession` step benchmark first (see PERF-008) with 1k, 10k and 50k objects, including a few parent chains, and record a baseline.
  2. Change `HierarchyIndex` storage from `std::map<ObjectId, Node>` with a per-node `std::vector children` to flat, id-sorted arrays:
     - `std::vector<ObjectId> ids`, sorted.
     - `std::vector<Node>` in parallel, with `firstChild`/`childCount` into one shared `children` array (CSR layout).
     - Lookup by `std::ranges::lower_bound` on `ids`.

     Iteration order stays ascending by id, which preserves determinism.
  3. Build `PreOrder()` once per index into a member vector, with one explicit stack and no per-root `Subtree` copies (Hierarchy.cpp:10-39).
  4. Replace the parent-chain loop walk (Hierarchy.cpp:81-97) with a single coloured DFS: white, grey and black marks over the flat arrays. This keeps the documented rule that the lowest id in a loop is cut first. Test it with the existing hierarchy loop tests.
  5. In `SceneRuntime::Step`, build the index once and reuse it:
     - Pass `before` into a `PropagateTransforms(world, const HierarchyIndex&)` overload, rebuilding only if the step changed structure. `CommandBuffers::Playback` and `SpawnPrefabs` can create, destroy or reparent; track a "structure changed" flag, or compare a World structural version counter if one exists.
     - Replace both `std::map<ObjectId, Transform> worlds` with a `std::vector<Transform>` indexed by the node's position in the flat index.
  6. In `ChainExecutor::Step`, build the `entities` map (Executor.cpp:580) lazily, only when `effects` is non-empty, or receive it from the caller's index. Keep it as a sorted vector of `(id, Entity)` pairs.
  7. In `GameSession::StepNavigation` (GameSession.cpp:106), reuse the runtime's index, for example through `SceneRuntime::Hierarchy()`, instead of building another one.
  8. Keep the scratch vectors as `SceneRuntime` members (`clear()`, not reallocation) so steady-state steps do not allocate.
- **Illustrative change (not applied):**
  ```cpp
  // Hierarchy.h (sketch)
  struct HierarchyIndex {
      std::vector<ObjectId> ids;          // ascending
      std::vector<Entity>   entities;     // parallel to ids
      std::vector<std::uint32_t> parent;  // index into ids, kNone for roots
      std::vector<std::uint32_t> childBegin, childCount; // CSR into children
      std::vector<std::uint32_t> children; // ascending id within each range
      std::vector<std::uint32_t> preOrder; // built once
  };
  ```
- **Verification:**
  - `ctest -L unit -R 'Hierarchy|SceneRuntime|GameSession|Prefab|SceneSave'` passes.
  - `interi_chain_stress_tests` and the `HashWorld` 1/4/32-thread comparisons are unchanged.
  - The new step benchmark (release and shipping config, PERF-009) shows fewer allocations per step and lower time per object.
  - A Tracy capture with the PERF-004 zones shows `BuildHierarchy` once per step.
- **Risks / trade-offs:**
  - `HierarchyIndex` is public API, used by the editor (`BuildScene`, `EditorScene`, prefabs). Run `impact` on `HierarchyIndex`/`BuildHierarchy` and migrate the callers, or keep a compatibility accessor.
  - Reusing the index across playback is only valid if structural changes are detected; get this wrong and the result is stale parents.

## PERF-002 — Each moving nav agent allocates a Detour query with a 2048-node pool and runs a full A* every step
- **Severity / Effort:** High / M
- **Depends on:** PERF-008 (nav step benchmark)
- **Steps:**
  1. Stop allocating per call while keeping thread safety, which is why CLAUDE.md documents one query per call. Preferred: keep a small mutex-protected free list (pool) of initialized `dtNavMeshQuery` objects inside `NavMesh`, handing one out per call (RAII lease) and returning it afterwards. The pool is owned by the mesh and destroyed with it, so no cached query can outlive or be confused with a destroyed mesh. A `thread_local` cache is acceptable only if its key is a process-unique lifetime token (e.g. a `NavMesh` id from a global `std::atomic<std::uint64_t>` counter, never reused), not the `dtNavMesh*` address plus a resettable generation: a destroyed mesh's address can be reused by a new mesh, and a stale query would then point at freed memory. Even with the token, thread-local slots keep a dead mesh's query alive until that thread's next call or exit, so the pool is simpler.

     Either way, `FindPath` results are unchanged.
  2. Optional behavior change, to be agreed with the maintainer because it changes trajectories: store each agent's straight path and corridor on the component or in a side table, and re-plan only when the destination changes, the agent leaves its corridor, the mesh generation changes, or every N steps. `dtPathCorridor` (`movePosition`/`optimizePathTopology`) is Detour's incremental tool for this. Document the new rule in `docs/animation-navigation-localization.md`.
  3. If agent counts grow, evaluate agents in parallel with `ParallelFor` over the id-sorted `walkers`, writing results into per-index slots and merging in id order. This keeps determinism, since `FindPath` is const and thread-safe with per-call leased queries.
- **Illustrative change (not applied):**
  ```cpp
  // NavMesh.cpp (sketch): the pool lives in NavMesh and dies with it.
  class QueryPool {
      std::mutex m_mutex;
      std::vector<std::unique_ptr<dtNavMeshQuery, QueryDeleter>> m_free;
  public:
      Lease Acquire(const dtNavMesh& mesh); // pop or alloc + init(&mesh, 2048); Lease returns it on destruction
  };
  std::unique_ptr<QueryPool> m_queries; // member of NavMesh; heap-held so NavMesh stays movable, moves with m_mesh
  ```
- **Verification:**
  - The `NavAgentTests` and `GameSession` navigation tests pass unchanged for step 1.
  - A `BM_StepNavAgents` benchmark with 1/16/128 agents shows the allocation cost gone (allocation counters via `TrackedAllocator`, or heaptrack).
  - A Tracy zone around `StepNavAgents` shows the per-step cost.
  - For step 2, add tests that pin when re-planning happens.
- **Risks / trade-offs:**
  - `thread_local` caches outlive a mesh if the generation check is wrong. A hot-reloaded module that swaps `NavSource` must bump the generation.
  - Step 2 is a deliberate behavior change.

## PERF-003 — Editor mesh cache thrashes above 256 distinct meshes, and every mesh upload is a synchronous GPU round trip
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Replace the "clear everything above 256" policy (EditorApp.cpp:925-927) with LRU or mark-and-sweep eviction:
     - Stamp each cache entry with the frame it was last used.
     - After `BuildScene`, evict only entries not used this frame, and only when over budget.
     - Never evict an entry used in the current frame.
  2. Key the renderer's `sceneMeshes` (EditorRenderer.cpp:81) by the same content key as `m_meshCache`, not by `TriangleMesh*`, so a regenerated but identical mesh does not re-upload. Alternatively, drop `sceneMeshes` entries when their `shared_ptr` is the last owner (`use_count() == 1`) at frame end.
  3. Batch uploads: give `EditorRenderer` one persistent `UploadQueue` and record mesh uploads into the frame's command list, or one batch list per frame, retired on the timeline. Do not call `UploadNow` per mesh. `UploadQueue` already tracks retirement (`uploads.Submitted(value)`).
  4. Avoid the per-object `std::format` key: cache it per entity with the `PrimitiveComponent` change tick (using `Changed<PrimitiveComponent>` semantics), or hash the parameter struct directly with XXH3.
  5. Make `ResetMeshPool` the rare case: size the pool from the scene, or grow it.
- **Verification:**
  - Add an editor headless test or benchmark that builds a scene with 300 or more distinct box sizes and runs 120 frames (`level_editor --video-driver offscreen --frames 120`). Count `AddMesh` uploads and `WaitForValue` calls: after the first frame, steady-state frames should do zero uploads.
  - A Tracy capture (PERF-004) shows no `WaitForValue` inside `Render`.
- **Risks / trade-offs:**
  - Batched uploads need correct barriers between the upload copy and the forward pass. The frame graph can import the mesh pool buffers with a `CopyDst` → `ShaderRead` transition.
  - The cache must still stay bounded during parameter drags, which was the original reason for the clear.

## PERF-004 — Frame and step hot paths have no profiling instrumentation; there are no GPU timings and no default frame marks
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Add `INTERI_ZONE()` (or `INTERI_ZONE_NAMED`) to:
     - `GameSession::Step` and `SceneRuntime::Step`.
     - `ChainExecutor::Step`, with one sub-zone per wave plus "apply effects".
     - `BuildHierarchy`, `PropagateTransforms`, `ScanTriggers`, `OverlapTracker::Update`, `StepNavAgents`, `NavMesh::FindPath`.
     - `CommandBuffers::Playback`, `Scheduler::Run` (or equivalent), `ModuleHost::RunSystems`.
     - `FrameGraph::Compile`/`Execute`, with a zone per pass name through `INTERI_ZONE_TEXT`.
     - `ForwardRenderer::AddPasses`, `EditorRenderer::Render`/`AddMesh`, `ImGuiRenderer::AddPass`.
     - `AudioEngine::Impl::Mix`.
     - `AssetDatabase` cook.
  2. Always emit `INTERI_FRAME_MARK()` once per frame in profile builds. It is free when no profiler is attached, because it is on-demand. Remove the `profilerFrameMarks` gate or default it to true. Also emit a frame mark from the game loop that drives `GameSession`, and mark the fixed step with `FrameMarkNamed("Step")`.
  3. Name threads with `INTERI_PROFILE_THREAD_NAME`: job workers ("Worker N"), the logger flush thread and the audio callback thread.
  4. GPU timing:
     - Add `IDevice` timestamp-query support (`WriteTimestamp(list, pool, index)` plus readback N frames later), with a `MOCK_METHOD` and a `NullDevice` override in the same change, per the CLAUDE.md rule.
     - Have the `FrameGraph` wrap each pass in begin/end timestamps and Vulkan debug labels (`vkCmdBeginDebugUtilsLabelEXT`) for RenderDoc.
     - Alternatively, use TracyVulkan (`TracyVkZone`) in the Vulkan backend under `INTERI_PROFILING`.
  5. Plot key counts with `INTERI_PROFILE_PLOT`: activations per step, objects, draw count and transient heap size.
  6. Document a frame budget (for example 16.6 ms at 60 Hz) and a fixed-step budget in `docs/` once baseline numbers exist.
- **Verification:**
  - Build `linux-clang-profile`, run `level_editor --project-file samples/modules/starter/templates/coins --play 1` with the Tracy profiler attached, and confirm named zones for every system listed above plus per-pass GPU zones.
  - A test confirms that the `INTERI_ZONE` macros compile to nothing in non-profile builds (existing behavior).
- **Risks / trade-offs:**
  - Zones in very tight loops add overhead even when on-demand. Keep zones at system or pass granularity, not per entity.
  - Timestamp queries need a readback ring sized to frames in flight, so they don't cause a sync stall.

## PERF-005 — `ChainExecutor::Step` allocates per activation and scans every trigger node per raised trigger
- **Severity / Effort:** Medium / M
- **Depends on:** PERF-004, PERF-008
- **Steps:**
  1. Make `activations`, `outcomes`, `next`, `effects` and `firings` executor members and `clear()` them each step and wave instead of reallocating. Make `Outcome` reuse its inner vectors: clear them rather than value-initialising new `Outcome`s.
  2. Build a per-instance trigger index at `Recompile()`:
     - Map the trigger event id (interned, or a `StringId`) to the list of node indices, in ascending node order.
     - In `AddTriggerFirings`, iterate instances in id order as today, and only that instance's matching nodes.

     The iteration order and `m_sequence` assignment stay identical, so the golden hash is unaffected.
  3. Pre-build constant Tick and Timer payload shapes once, outside the per-node loop.
  4. Build `entities` lazily (see PERF-001 step 6).
  5. Optional: give `Effect` a small-buffer allocator, or move common effects (SetValue, Move) to a POD variant to avoid `move_only_function` heap captures. Keep application order.
- **Verification:**
  - `interi_chain_stress_tests` passes, including `ChainDeterminism.MatchesCrossPlatformGoldenHash` with an unchanged hash.
  - Add a `BM_ChainExecutorStep` benchmark (100 chains, Tick plus Overlap triggers) and compare allocations per step and time with `tools/bench_compare/compare_benchmarks.py`.
- **Risks / trade-offs:**
  - Member scratch buffers keep peak memory after a burst step. Shrink them occasionally if needed.
  - The trigger index must be rebuilt whenever programs recompile (`m_dirty`).

## PERF-006 — Trigger scan does redundant lookups, string work and an all-pairs overlap test every step
- **Severity / Effort:** Medium / M
- **Depends on:** PERF-008
- **Steps:**
  1. Query volumes and actors in two separate queries: `With<StableIdComponent, Transform, TriggerVolumeComponent>` and `With<StableIdComponent, Transform, TriggerActorComponent>`. Read `Transform` from the chunk view (`view.Read<Transform>()`) instead of calling `world.Get`. This visits only relevant entities.
  2. Resolve `shape.name` to `VolumeShape` once. Either cache it on the component at load and edit time, or compare against an interned value. Store a tag index or `StringId` in `TriggerVolume` and copy the `std::string` only when an event is raised.
  3. Sort once: emit `(id, index)` pairs and sort them, without building and copying a second `TriggerScan`. `OverlapTracker::Update` can then skip its own re-sort when the input is already sorted (assert in debug).
  4. When V × A becomes significant, add a broadphase. A uniform grid or sort-and-sweep on x over volume AABBs works. The candidate pairs must be emitted, or sorted, in `(volume id, actor id)` order so that `now` stays sorted and the merge walk is unchanged.
- **Verification:**
  - The `TriggerVolumes`, `Triggers` and `SceneRuntime` overlap tests pass.
  - The 1/4/32-thread scene hashes are unchanged.
  - A `BM_ScanTriggers` benchmark (10k objects, 200 volumes, 50 actors) shows the drop.
- **Risks / trade-offs:** The broadphase adds code. Keep the brute-force path as a test oracle.

## PERF-007 — `GetMut` stamps change ticks for every object every step, which defeats `Changed<Transform>` and `Changed<LocalTransformComponent>`
- **Severity / Effort:** Medium / S
- **Depends on:** PERF-001 (same functions)
- **Steps:**
  1. In `PropagateTransforms` (Hierarchy.cpp:143-149), read with `world.Get<Transform>` first and call `GetMut`, or `Add` for a missing component, only when the value differs.
  2. In `SceneRuntime::Step` (SceneRuntime.cpp:182-188), move `GetMut<LocalTransformComponent>` after the "did it move" test, and use `Get` for the check.
  3. Optionally add a `World::SetIfChanged<T>(entity, value)` helper that compares before stamping. This is a cross-discipline item for the ECS design.
- **Illustrative change (not applied):**
  ```cpp
  const auto* current = world.Get<Transform>(node.entity);
  if (current == nullptr)            { (void)world.Add(node.entity, worldTransform); }
  else if (!(*current == worldTransform)) { *world.GetMut<Transform>(node.entity) = worldTransform; }
  ```
- **Verification:** Add a unit test: two `SceneRuntime::Step`s with no chain activity, followed by a `Query().Changed<Transform>()` over the second step's run, match zero chunks. The existing scene tests must still pass.
- **Risks / trade-offs:** Code that relied on "Changed matches everything after a step" was relying on the bug. CLAUDE.md documents that a reload marks everything changed once, and that behavior is separate and unaffected.

## PERF-008 — Benchmarks do not cover the per-step paths, and CI never compares results
- **Severity / Effort:** Medium / M
- **Depends on:** PERF-009 (measure in an optimized configuration)
- **Steps:**
  1. Add `benchmarks/scene/` and `benchmarks/chain/` (plus `nav`) with `interi_add_benchmark` or the existing helper, so that the `--benchmark_dry_run` smoke test is registered automatically. Cover:
     - `BuildHierarchy` and `PropagateTransforms` at N = 1k/10k.
     - `SceneRuntime::Step` on a generated scene.
     - `ChainExecutor::Step` with many Tick chains.
     - `ScanTriggers` plus `OverlapTracker::Update`.
     - `StepNavAgents` with M agents.
     - `hud::BuildFrame`.
  2. Report allocations per iteration as a counter, using `TrackedAllocator` or a global `operator new` counter in the benchmark binary, alongside time.
  3. Add a non-blocking CI job, or a scheduled workflow on a fixed runner, that runs the release benchmarks with `--benchmark_repetitions=5 --benchmark_out=...json` and calls `tools/bench_compare/compare_benchmarks.py` against a committed baseline (for example `benchmarks/baselines/linux-clang-release.json`). Start in `--report-only` mode, because shared CI runners are noisy, then gate allocation counters, which are deterministic, strictly.
- **Verification:**
  - `ctest -L benchmark` lists the new dry-run tests.
  - The CI job uploads the JSON and the comparison report.
  - Deliberately adding an extra `BuildHierarchy` call makes the allocation-count gate fail.
- **Risks / trade-offs:** Timing gates on shared runners produce false positives. Gate on allocation counts and use wide time thresholds.

## PERF-009 — There is no optimized shipping configuration: "release" is RelWithDebInfo with asserts on, and there is no IPO/LTO
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add an `INTERI_SHIPPING` CMake option that defines `INTERI_SHIPPING` on all engine targets and goes into `interi::build_settings`, since it is ABI-relevant through the toolchain fingerprint (`Toolchain.h`).
  2. Add presets `linux-clang-shipping` and `windows-msvc-shipping` (`CMAKE_BUILD_TYPE=Release`, `INTERI_SHIPPING=ON`, `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON`). Check that LTO works with the shared-library engine and vcpkg static dependencies. ThinLTO on Clang (`-flto=thin`) is the low-risk choice.
  3. Measure benchmarks in both `release` (asserts on, for development) and `shipping`, and document which numbers count as budgets.
  4. Optionally evaluate PGO with Clang (`-fprofile-instr-generate` → `-fprofile-instr-use`) using the starter sample's headless run as the training workload.
- **Verification:**
  - `cmake --preset linux-clang-shipping && cmake --build --preset linux-clang-shipping` succeeds.
  - Tests that depend on asserts `GTEST_SKIP` as CLAUDE.md requires.
  - Benchmark comparison between release and shipping shows the assert overhead.
- **Risks / trade-offs:**
  - LTO lengthens link time.
  - Shipping builds disable the StringId name table and asserts, so a shipping CI build is needed to catch code that only compiles with them on.

## PERF-010 — Per-frame scratch containers are rebuilt instead of reused (FrameGraph, CommandBuffers playback)
- **Severity / Effort:** Low / S
- **Depends on:** PERF-004 (confirm it shows up first)
- **Steps:**
  1. Make the `Compile`/`Execute` temporaries `FrameGraph` members that are `clear()`ed per frame: `producers` flattened into one vector plus offsets, `worklist`, `order`, `busy`, `transients`, `taken`, `states`, and the barrier vectors.
  2. Store pass and resource names as `std::string_view` into a per-frame arena, or as `StringId`. They are mostly literals.
  3. Consider a small-buffer callable for pass execute lambdas instead of `std::function`.
  4. Make `CommandBuffers::Playback`'s `entries` a member scratch vector.
- **Verification:** The `FrameGraph` and `CommandBuffer` unit tests pass. `BM_FrameGraph*` (`benchmarks/render/RenderBenchmarks.cpp`) shows fewer allocations.
- **Risks / trade-offs:** String views into caller-owned names must outlive the frame; literals do.

## PERF-011 — Pipelines are created with no `VkPipelineCache`
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Create one `VkPipelineCache` per device. Load it from a per-user cache file keyed by vendor, device and driver UUID (`VkPhysicalDeviceProperties::pipelineCacheUUID`), and pass it to `vkCreate*Pipelines`.
  2. Save it on device destruction. On Windows, Xbox and PS5 use the platform equivalents in their backends.
- **Verification:** Time renderer creation (Tracy zone, PERF-004) on a cold start and on a second start. The second start should be faster on real drivers; lavapipe may show little difference.
- **Risks / trade-offs:** Reading a corrupt cache file must fall back cleanly. Vulkan validates the header, but the loader must handle I/O errors.

## PERF-012 — ImGui atlas and texture uploads stall the frame on the GPU
- **Severity / Effort:** Low / S
- **Depends on:** PERF-003 (shared persistent upload queue)
- **Steps:** Record ImGui texture uploads into the frame's command list through a persistent `UploadQueue`, and defer destroying the old texture through `ResourceManager`, which retires it on the timeline. This removes `WaitForValue` from `ImGuiRenderer.cpp:170`.
- **Verification:** Change the font size at run time while Tracy is attached: no `WaitForValue` on the render thread, and no frame spike.
- **Risks / trade-offs:** ImGui must not sample the new texture before the copy. The frame graph or an explicit barrier must order it.
