# Performance Optimization — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi)
**Analyzed:** 2026-10-19
**Scope:** First-party C++ under `engine/` (scene, chain, ecs, jobs, nav, render, rhi_vulkan, audio, hud, game, core profiling), `tools/editor/` (editor frame loop and renderer), `benchmarks/`, `tools/bench_compare/`, `CMakePresets.json`, `cmake/`, `.github/workflows/ci.yml`. Static reading only. The work follows the per-step simulation path (`GameSession::Step` → `SceneRuntime::Step` → `ChainExecutor::Step` → hierarchy, triggers and nav), the per-frame render path (`EditorRenderer::Render` → `FrameGraph` → `ForwardRenderer`), the audio mix thread and the job system. Skipped: `modules/` (separate projects, not indexed), `external/`, `build*/`.
**Summary:** 14 issues — Critical: 0 · High: 4 · Medium: 5 · Low: 3 · Info: 2

Determinism note that applies to every finding: the engine requires id-ordered iteration and bit-identical results at 1, 4 and 32 threads (`ChainDeterminism.MatchesCrossPlatformGoldenHash`, `HashWorld` comparisons). The remediations in Remediation.md are constrained by this.

## Findings

### PERF-001 — Every fixed step rebuilds `std::map`-based indexes of all scene objects three to four times
- **Severity:** High
- **Confidence:** High (behavior); Medium (magnitude, not measured)
- **Category:** CPU / allocation in the per-step hot path
- **Location:** `engine/scene/src/SceneRuntime.cpp:161-218`, `engine/scene/src/Hierarchy.cpp:10-39`, `engine/scene/src/Hierarchy.cpp:56-111`, `engine/scene/src/Hierarchy.cpp:132-156`, `engine/scene/include/interi/scene/Hierarchy.h:14-24`, `engine/chain/src/Executor.cpp:580-590`, `engine/game/src/GameSession.cpp:89-106`
- **Evidence:** `SceneRuntime::Step` does the following on every fixed step:
  - `const HierarchyIndex before = BuildHierarchy(m_world);` (line 164), plus a `std::map<ObjectId, Transform> worlds` of every object (line 165).
  - `ChainExecutor::Step` builds `std::map<std::uint64_t, Entity> entities` from every `StableIdComponent` (Executor.cpp:580). It does this unconditionally, even when no effect runs.
  - `PropagateTransforms(m_world)` (line 194) calls `BuildHierarchy` again (Hierarchy.cpp:134) and fills a second `std::map<ObjectId, Transform>` (line 135).
  - `GameSession::StepNavigation` builds another hierarchy whenever any agent moved (GameSession.cpp:106).

  `HierarchyIndex` is `std::map<ObjectId, Node>`, and each `Node` owns a `std::vector<ObjectId> children` (Hierarchy.h:20-23). That is one tree node plus one children vector per object, per rebuild. `PreOrder()` calls `Subtree(root)`, which allocates a result vector and a stack vector per root and then copies them (Hierarchy.cpp:14-18, 24-36), and both `Step` and `PropagateTransforms` call it. `BuildHierarchy` also walks each object's parent chain to detect loops (Hierarchy.cpp:81-97). That walk is O(N × depth), with an O(log N) `map::at` per hop.
- **Impact:** This is the core game loop. Per-step cost is roughly 4 × O(N log N) node-allocating map builds, plus O(N × depth) loop checks, plus about 2N `std::map` inserts. That happens even when nothing moved and no chain fired. Cost grows with total object count, not with activity. On a scene with thousands of objects this is likely to dominate the step before any gameplay runs, and the allocator churn pollutes the cache for everything after it. GitNexus `context` on `SceneRuntime::Step` shows it calls `BuildHierarchy`, `PropagateTransforms`, `ScanTriggers`, `SpawnPrefabs` and `PreOrder`; 15 further call sites were dropped by receiver typing, so the real caller set is larger.

### PERF-002 — Each moving nav agent allocates a Detour query with a 2048-node pool and runs a full A* every step
- **Severity:** High
- **Confidence:** High (behavior); Medium (magnitude)
- **Category:** Algorithmic cost / allocation per entity per step
- **Location:** `engine/nav/src/NavAgents.cpp:40-91`, `engine/nav/src/NavMesh.cpp:351-356`, `engine/game/src/GameSession.cpp:97-106`
- **Evidence:** For every agent in state `Moving`, the loop calls `auto path = mesh->FindPath(transform->position, agent->destination);` (NavAgents.cpp:75). `FindPath` starts with `std::unique_ptr<dtNavMeshQuery, QueryDeleter> query(dtAllocNavMeshQuery()); ... query->init(m_mesh.get(), 2048)` (NavMesh.cpp:353-354). `init` allocates a node pool, its hash table and an open list for 2048 nodes, and they are freed at the end of the call. It then runs `findNearestPoly` twice, `findPath` and `findStraightPath`. All of this runs serially on the step thread. CLAUDE.md documents both "re-plans each step" and "one `dtNavMeshQuery` per call so queries are thread-safe" as intended.
- **Impact:** Cost is agents × (heap allocation and free of a 2048-node pool + full A* + string pull) on every fixed step, even when the agent's destination and corridor have not changed. At 60 Hz with dozens of agents this adds up to thousands of full path searches and node-pool allocations per second on the main simulation thread. These are documented trade-offs, but the allocation part is not needed for thread safety (see Remediation).

### PERF-003 — Editor mesh cache thrashes above 256 distinct meshes, and every mesh upload is a synchronous GPU round trip
- **Severity:** High
- **Confidence:** Medium (derived from code; not reproduced)
- **Category:** Editor frame hitch / CPU-GPU sync / redundant I/O
- **Location:** `tools/editor/EditorApp.cpp:886-931`, `tools/editor/EditorApp.cpp:934-979`, `tools/editor/EditorRenderer.cpp:39-53`, `tools/editor/EditorRenderer.cpp:150-211`
- **Evidence:**
  - `BuildScene()` runs every frame (EditorApp.cpp:1633) and calls `ObjectMesh` for every object.
  - `ObjectMesh` builds a `std::format` key with ten fields for every primitive on every frame (line 892) and looks it up in a `std::map<std::string, ...>`.
  - When a key misses and the cache already holds more than 256 entries, the whole cache is dropped: `if (m_meshCache.size() > 256) { m_meshCache.clear(); }` (lines 925-927).
  - The regenerated mesh is a new `shared_ptr`. `EditorRenderer::AddMesh` keys `sceneMeshes` by raw pointer (`state.sceneMeshes.contains(mesh.get())`, line 169), so the regenerated mesh counts as a new mesh. It is re-expanded and re-uploaded through `UploadNow` (line 201).
  - For `.imesh` assets a cache miss also re-reads the file from disk through `scene::LoadMeshAsset` (line 913).
  - `UploadNow` creates a 4 MiB `UploadQueue` and a command pool, submits, and then calls `device.WaitForValue(value)` (lines 41-50). That is a full CPU-GPU stall for each mesh.
  - `sceneMeshes` keeps the old entries alive, so the `MeshPool` fills up and `ResetMeshPool` runs. It waits for the GPU to go idle and re-uploads (lines 150-163).
- **Impact:**
  - Steady state: a level with more than 256 distinct primitive parameter sets or mesh assets goes through clear, regenerate and re-upload every frame. That means many GPU stalls, staging-buffer creation, primitive regeneration and disk reads per frame, plus periodic pool resets, so the editor viewport stutters or crawls.
  - Below the threshold: opening a scene with N meshes still costs N serialized GPU round trips at load.

### PERF-004 — Frame and step hot paths have no profiling instrumentation; there are no GPU timings and no default frame marks
- **Severity:** High
- **Confidence:** High
- **Category:** Profiling readiness
- **Location:** `engine/core/include/interi/core/Profiling.h:19-43`, `engine/jobs/src/JobSystem.cpp:303`, `engine/jobs/src/TaskGraph.cpp:53`, `tools/editor/EditorApp.cpp:537-540`, `engine/editor/include/interi/editor/EditorServices.h:30`
- **Evidence:**
  - `grep -rn INTERI_ZONE engine tools` finds zones in exactly two places, `JobSystem.cpp:303` and `TaskGraph.cpp:53`.
  - Nothing is instrumented in `SceneRuntime::Step`, `ChainExecutor::Step`, `BuildHierarchy`/`PropagateTransforms`, `ScanTriggers`, `StepNavAgents`, `CommandBuffers::Playback`, `Scheduler`, `FrameGraph::Compile`/`Execute`, `ForwardRenderer::AddPasses`, `EditorRenderer::Render`, `GameSession::Step`, the audio mix callback or the asset cook.
  - `INTERI_FRAME_MARK()` appears only behind an editor preference that defaults to `false` (`profilerFrameMarks = false`).
  - `GameSession` has no frame mark.
  - No `INTERI_PROFILE_THREAD_NAME` is used for workers or the audio thread.
  - The RHI has no timestamp queries (`grep Timestamp|QueryPool` in `engine/rhi*`/`engine/render` finds nothing) and no command-list debug labels.
  - There is no documented frame or step budget.
- **Impact:** The `*-profile` presets exist, but a Tracy capture shows only job execution blocks, with no names for the work inside them and no GPU pass timings. None of the other findings in this report can be confirmed or ranked with a profiler as the code stands, and regressions in the step or frame stay invisible.

### PERF-005 — `ChainExecutor::Step` allocates per activation and scans every trigger node per raised trigger
- **Severity:** Medium
- **Confidence:** High (behavior); Medium (magnitude)
- **Category:** Allocation in hot loop / algorithmic cost
- **Location:** `engine/chain/src/Executor.cpp:361-404`, `engine/chain/src/Executor.cpp:408-480`, `engine/chain/src/Executor.cpp:568-827`, `engine/chain/include/interi/chain/Event.h:102`
- **Evidence:**
  - Every wave allocates `std::vector<Activation>`, `std::vector<Outcome> outcomes(activations.size())` and `std::vector<Firing> next` (lines 614, 685, 727).
  - Each `Outcome` holds three heap vectors (`inputs`, `fired`, `effects`; lines 601-608).
  - Each `Effect` is a `std::move_only_function` (Event.h:102), and each `Firing` carries its own `std::vector<Value> payload`.
  - Every Tick node builds a fresh payload vector on every step (lines 452-456).
  - The entity map from PERF-001 is built unconditionally (line 580).
  - `AddTriggerFirings` (lines 361-404), called for each raised trigger including every overlap begin and end, loops over all instances and all their nodes and compares `node.event != trigger` as strings. The cost is O(raised × instances × nodes).
- **Impact:** Allocator traffic scales with activations and runs on the worker threads inside `ParallelFor`, where it contends on the global allocator. Trigger dispatch is quadratic-ish in the number of chains when many overlaps or value changes are raised in a step. Reusing buffers does not change evaluation order or results, so it does not affect the golden hash. An index for trigger lookup must keep the instance and node iteration order.

### PERF-006 — Trigger scan does redundant lookups, string work and an all-pairs overlap test every step
- **Severity:** Medium
- **Confidence:** High
- **Category:** CPU / data access / algorithmic complexity
- **Location:** `engine/scene/src/Triggers.cpp:32-78`, `engine/runtime/src/TriggerVolumes.cpp:39-96`, `engine/scene/src/SceneRuntime.cpp:206-217`, `engine/scene/include/interi/scene/Triggers.h:23-31`
- **Evidence:**
  - `ScanTriggers` iterates every entity with `StableIdComponent + Transform`, which is every scene object, not only volumes or actors.
  - For each one it calls `world.Get<Transform>`, `Get<TriggerVolumeComponent>` and `Get<TriggerActorComponent>` (lines 43, 44, 56), although `Transform` is already in the query's chunk.
  - Per volume it compares `volume->shape.name == "Sphere"` as a string (line 49) and copies the `std::string tag` (line 54).
  - It then sorts an index and copies everything into a second `TriggerScan` (lines 66-76).
  - `OverlapTracker::Update` sorts pointer arrays again and tests every volume against every actor (TriggerVolumes.cpp:61-70). This O(V × A) cost is documented in CLAUDE.md.
  - `SceneRuntime::Step` builds a `std::string tag` per event (line 210).
- **Impact:** This is a per-step cost proportional to all objects, plus V × A overlap tests. It is fine for the starter sample but scales poorly for levels with many pickups and actors. The fix must keep the sorted (volume, actor) output order.

### PERF-007 — `GetMut` stamps change ticks for every object every step, which defeats `Changed<Transform>` and `Changed<LocalTransformComponent>`
- **Severity:** Medium
- **Confidence:** High (behavior); Low (current impact: no in-tree consumer found)
- **Category:** ECS change detection / wasted downstream work
- **Location:** `engine/ecs/include/interi/ecs/World.h:281-290`, `engine/scene/src/Hierarchy.cpp:143-149`, `engine/scene/src/SceneRuntime.cpp:182-188`
- **Evidence:** `World::GetMut` calls `MarkChanged(*record, type)` before returning (World.h:289).
  - `PropagateTransforms` calls `world.GetMut<Transform>(node.entity)` for every node and only then compares `!(*transform == worldTransform)` (Hierarchy.cpp:143-145).
  - `SceneRuntime::Step` calls `m_world.GetMut<LocalTransformComponent>(node.entity)` for every node before testing whether the transform moved (SceneRuntime.cpp:184-186).
  - `grep "Changed<Transform>|Changed<LocalTransformComponent>"` over `engine tools samples tests` finds no consumer today.
- **Impact:** Every chunk's `Transform` and `LocalTransformComponent` columns count as changed on every step. Any module system, render sync or save diff that filters on `Changed<Transform>` will process the whole world every step, so the Bevy-style change filter is useless for the most common component. It is latent today, but module systems (the destruction and physics modules, which are not indexed) are the likely consumers.

### PERF-008 — Benchmarks do not cover the per-step paths, and CI never compares results
- **Severity:** Medium
- **Confidence:** High
- **Category:** Performance regression guardrails
- **Location:** `benchmarks/` (core, ecs, jobs, render, audio only), `cmake/InteriModule.cmake:105-111`, `.github/workflows/ci.yml:66-70`, `tools/bench_compare/compare_benchmarks.py:1-8`
- **Evidence:**
  - There are 33 `BENCHMARK(` registrations across `benchmarks/core`, `ecs`, `jobs`, `render` and `audio`.
  - Nothing benchmarks `SceneRuntime::Step`, `ChainExecutor::Step`, `BuildHierarchy`/`PropagateTransforms`, `ScanTriggers`/`OverlapTracker`, `StepNavAgents`, `GameSession::Step`, `BuildFrame` (HUD) or the editor's `BuildScene`.
  - CI runs benchmark executables only as `--benchmark_dry_run` smoke tests (`InteriModule.cmake:111`).
  - `compare_benchmarks.py` exists, but no baseline JSON is committed (`git ls-files | grep -i baseline` is empty) and no workflow calls it.
- **Impact:** The costliest paths in this report have no measurement, and a regression in any of them, such as an extra hierarchy rebuild, cannot fail CI.

### PERF-009 — There is no optimized shipping configuration: "release" is RelWithDebInfo with asserts on, and there is no IPO/LTO
- **Severity:** Medium
- **Confidence:** High
- **Category:** Build optimization settings
- **Location:** `CMakePresets.json:27-28`, `CMakePresets.json:38-39`, `CMakePresets.json:55-58`, `engine/core/include/interi/core/Config.h:6-14`, `CMakeLists.txt:20-24`
- **Evidence:**
  - `linux-clang-release`, `windows-msvc-release` and both `*-profile` presets set `CMAKE_BUILD_TYPE` to `RelWithDebInfo`.
  - `INTERI_ENABLE_ASSERTS` is 1 unless `INTERI_SHIPPING` is defined (Config.h:10-14), and no preset or CMake option defines `INTERI_SHIPPING` (grep over `CMakeLists.txt`/`cmake/` finds nothing).
  - `INTERPROCEDURAL_OPTIMIZATION`, `-flto` and `/GL`/`/LTCG` are not used anywhere in `CMakeLists.txt`, `cmake/` or `CMakePresets.json`.
  - CLAUDE.md tells contributors to measure benchmarks with the release preset.
- **Impact:** Benchmarks and Tracy captures measure code with asserts on, including `INTERI_ASSERT` inside hot loops such as `ComponentAt` and `OverlapTracker::Update`, and with the StringId name table. They don't show what a shipped game would cost. The shared-library engine also loses cross-module inlining, and with no LTO nothing recovers it in the static parts. Since PS5/Xbox are targets, the shipping configuration needs to exist on PC as well.

### PERF-010 — Per-frame scratch containers are rebuilt instead of reused (FrameGraph, CommandBuffers playback)
- **Severity:** Low
- **Confidence:** High
- **Category:** Per-frame allocation churn
- **Location:** `engine/render/include/interi/render/FrameGraph.h:124`, `engine/render/include/interi/render/FrameGraph.h:149-179`, `engine/render/src/FrameGraph.cpp:222-293`, `engine/render/src/FrameGraph.cpp:363`, `engine/render/src/FrameGraph.cpp:424-460`, `engine/ecs/src/CommandBuffer.cpp:93-113`
- **Evidence:**
  - The frame graph is rebuilt every frame. Each pass stores a `std::string name`, a `std::vector<Access>` and a `std::function` execute callback (FrameGraph.h:177-179), and each resource stores a `std::string name`.
  - `Compile` and `Execute` allocate several temporaries per frame: `std::vector<std::vector<std::int64_t>> producers`, the worklist, the order, `busy` vectors per resource, `transients`, `taken`, `states`, and the barrier vectors.
  - `CommandBuffers::Playback` builds a fresh `std::vector<Entry> entries` on each call (CommandBuffer.cpp:100).
- **Impact:** This is small at today's pass counts of roughly 10-20, but it is steady allocator traffic on the render thread and the step thread. Transient GPU resources are already reused across frames (FrameGraph.cpp:372-385), which is the expensive part; this finding concerns only the CPU-side scratch.

### PERF-011 — Pipelines are created with no `VkPipelineCache`
- **Severity:** Low
- **Confidence:** High
- **Category:** Startup / PSO compilation
- **Location:** `engine/rhi_vulkan/src/VulkanDevice.cpp:1442`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1574`
- **Evidence:** `vkCreateComputePipelines(state.device, VK_NULL_HANDLE, 1, ...)` and `vkCreateGraphicsPipelines(state.device, VK_NULL_HANDLE, 1, ...)`. No pipeline cache is created, persisted or reused anywhere in `engine/rhi_vulkan/src`.
- **Impact:** Driver shader compilation repeats on every launch. SPIR-V is precompiled by DXC at build time and pipelines are created at renderer creation, not mid-frame, so the cost is startup time rather than in-game hitching. It will grow as more pipelines are added, for example by module render features.

### PERF-012 — ImGui atlas and texture uploads stall the frame on the GPU
- **Severity:** Low
- **Confidence:** High
- **Category:** CPU-GPU sync
- **Location:** `engine/editor_ui/src/ImGuiRenderer.cpp:155-172`
- **Evidence:** Each texture request creates an `UploadQueue` with at least 4 MiB of staging and a new command pool, submits, and then calls `m_device.WaitForValue(value)` before the frame continues. CLAUDE.md states that the atlas is "re-uploaded synchronously when it changes".
- **Impact:** This causes a visible editor hitch when fonts or textures change, such as on DPI changes or new glyph ranges. It is rare, so Low.

### PERF-013 — HUD layout formats strings every frame
- **Severity:** Info
- **Confidence:** Medium
- **Category:** Per-frame allocation (observation)
- **Location:** `engine/hud/src/HudLayout.cpp:68-149`
- **Evidence:** `DisplayText` and `FormatHudText` build `std::string`s with `std::format` (lines 85, 97, 105, 149) for every bound text widget on every `BuildFrame`.
- **Impact:** This is negligible at typical HUD sizes. It is listed so that a future benchmark (PERF-008) includes `BuildFrame`. No action is needed unless a measurement shows otherwise.

### PERF-014 — Already sound: audio mixer, job system, GPU-driven forward path
- **Severity:** Info
- **Confidence:** High
- **Category:** Verified good practice (preserve)
- **Location:** `engine/audio/src/Mixer.cpp:53-93`, `engine/audio/src/AudioEngine.cpp:74`, `engine/audio/src/AudioEngine.cpp:121-149`, `engine/jobs/include/interi/jobs/ParallelAlgorithms.h:31-71`, `engine/render/src/ForwardRenderer.cpp:202-305`, `engine/render/src/FrameGraph.cpp:372-385`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1236-1256`
- **Evidence:**
  - Audio: the audio thread uses fixed voice slots and a pre-reserved `mixed` vector (`mixed.reserve(d.maxVoices)`), with SPSC queues in both directions and no allocation or locks in `Mix`.
  - Jobs: `ParallelForChunks` uses a shared atomic cursor, with helpers capped at the thread count and the caller taking part. `ParallelReduce` accumulates locally, so there is no false sharing on partials.
  - Forward renderer: it maps scene data persistently, one slot per frame in flight, and draws with `DrawIndexedIndirectCount` over a single `MeshPool`.
  - Frame graph: transient memory is aliased and placed resources are reused across frames. Memory requirements are queried without creating objects (`vkGetDevice*MemoryRequirements`).
- **Impact:** None. These are the patterns the rest of the engine should follow.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 552,120 edges, 4,973 communities, 614 processes; indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are all third-party (SDL Joystick/Hidapi/Test, Tracy Profiler, Manifold, ImGui docking); no first-party cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 SDL example and internal flows (SDL_AppIterate → EnterLock, …); none first-party |
| 4 | cypher | Process where `entryPointId CONTAINS ':engine/'` | Only 5 first-party flows, all `AssetDatabase::Impl::CookModel → …` (offline cook); no per-frame or per-step process detected |
| 5 | query | "per-frame render loop ForwardRenderer frame graph execute" | No processes; definitions: `ForwardRenderer`, `EditorRenderer::Render` (EditorRenderer.cpp:249-393), `ImGuiRenderer::AddPass`; about 59 s wall (BM25 broken) |
| 6 | query | "scene runtime step chain executor waves" | No processes; definitions: `SceneRuntime`, `ScenePlaySession`, `SceneRuntime::m_executor`, executor test fixtures |
| 7 | query | "allocation in hot loop vector push_back command buffer playback" | `CommandBuffers::Playback` (CommandBuffer.cpp:93-145), `CommandBuffer`, `GenerateMips`; 2 Facade processes (editor DuplicateSelection) |
| 8 | context | `Playback` (engine/ecs/src/CommandBuffer.cpp) | 0 incoming resolved; outgoing accesses Clear, DestroyEntity, Entry.key/sequence/slot. The real caller is at Executor.cpp:826 (found by reading) |
| 9 | cypher | `Step` Method/Function OR query | Parser error (`m:Method OR m:Function` unsupported); reissued as #10 |
| 10 | cypher | Methods named Step/Render/Execute/Compile/Playback/BuildFrame/Mix under `engine/` | 25 rows: `SceneRuntime::Step` 160-217, `ChainExecutor::Step` 568-827, `FrameGraph::Compile` 174 / `Execute` 441, `Mixer::Mix` 53, `GameSession::Step` 89, … |
| 11 | impact | `CommandBuffers` (Class, upstream, summaryOnly) | risk UNKNOWN, 0 callers resolved (index gap, not evidence of no use; used by Executor.cpp:826 and SceneRuntime) |
| 12 | context | `Method:engine/scene/src/SceneRuntime.cpp:Step#0` | Outgoing calls: BuildHierarchy, PropagateTransforms, SpawnPrefabs, ScanTriggers, PreOrder, LocalTransformComponent::From; epistemic lower-bound (15 call sites dropped for receiver typing) |
| 13 | cypher | `Step` methods in SceneRuntime.cpp / Executor.cpp | uids for `SceneRuntime::Step#0` and `ChainExecutor::Step#3` (used for #12) |

## Analysis limitations
- No profiler, build, benchmark or test runs were possible (read-only analysis). Every cost estimate comes from reading the code and is marked Medium where the magnitude is inferred. Remediation.md says how to measure each one.
- The GitNexus BM25 index failed to build, so `query` calls were slow (about 59 s) and relied only on vector search. Clusters and processes are dominated by third-party sources under `external/vcpkg/buildtrees`, and no first-party per-frame or per-step process was detected. Hot paths were located by cypher name lookups and by reading the code.
- `modules/` (Jolt physics engine, destruction module) is not indexed and was not analyzed. Those modules are the likely heaviest per-step consumers (see PERF-007).
- These areas were checked only lightly or not at all: `engine/asset_import` (`ModelProcess`, offline editor cooking), editor thumbnails, `Logger`, the SIMD math kernels (they have their own benchmarks), `engine/anim`, `engine/loc`, and HLSL shader cost (GPU-side cost needs a GPU capture).
- No GPU timing data exists, and none can be gathered statically (see PERF-004).

## Cross-discipline notes
- Architecture / ECS: `World::GetMut` stamps change ticks unconditionally (`engine/ecs/include/interi/ecs/World.h:281-290`). A "write only if different" API, or a documented rule to compare before calling `GetMut`, belongs in the ECS design.
- Refactoring: `HierarchyIndex` (`engine/scene/include/interi/scene/Hierarchy.h:14-24`) is rebuilt from scratch by four callers per step. A persistent, incrementally maintained index would change ownership in `SceneRuntime`, `GameSession` and the editor.
- Code-Quality: the comment "the renderer drops what it no longer sees" at `tools/editor/EditorApp.cpp:927` does not match `EditorRenderer`, which only drops meshes on `ResetMeshPool` (`tools/editor/EditorRenderer.cpp:150-154`).
- CI/Build: a shipping preset (`INTERI_SHIPPING`) does not exist, so shipping-only compile paths (asserts off, profiling off) are never built or tested in CI (`CMakePresets.json`).
