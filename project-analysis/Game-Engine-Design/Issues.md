# Game-Engine-Design — Issues

**Project:** Interi (repository root), custom C++23 modular engine
**Analyzed:** 2026-10-19
**Scope:** First-party engine code under `engine/` (runtime, game, scene, ecs, chain, nav, anim, assets, asset_import, render, rhi, rhi_vulkan, core allocators, hotreload, editor), `tools/editor` (the only shipped executable that drives a frame loop), `samples/modules/starter`. Read with the GitNexus graph first, then targeted reads of about 25 files. `modules/` (physics, destruction), `external/`, `build/` and third-party code were out of scope.
**Summary:** 12 issues — Critical: 0 · High: 1 · Medium: 7 · Low: 3 · Info: 1

Interi's core is strong. It has a deterministic integer-nanosecond fixed-step clock with hitch clamping, an archetype ECS with generational handles and deterministic command playback, a job system, a backend-neutral RHI with timeline-based deferred destruction, a frame graph with transient aliasing, and a clean include DAG between engine libraries. The findings below are about **composition and lifecycle**: the pieces exist, but no runtime loop ties them together, there is no single source of truth for the step, and the asset path from cooked data to the GPU stops in the editor.

## Findings

### ENG-001 — No runtime game loop: nothing composes simulation, rendering, audio, animation and input outside the editor
- **Severity:** High
- **Confidence:** High
- **Category:** Main loop / subsystem composition
- **Location:** `engine/game/include/interi/game/GameSession.h:8`, `engine/game/src/GameSession.cpp:90-95`, `engine/chain/include/interi/chain/Executor.h:268`, `engine/chain/src/Executor.cpp:572`, `engine/anim/include/interi/anim/Animator.h:119`, `engine/runtime/include/interi/runtime/FramePipeline.h:20-45`, `tools/` (only `editor`, `module_check`, `bench_compare`, `license_check`, `new_module`, `platform_audit`)
- **Evidence:** `GameSession` is only a simulation step: "The caller owns time (runtime/FixedStepClock.h) and input (InputMapper)" (GameSession.h:8). `Step()` runs module systems, the scene and navigation (GameSession.cpp:90-95). It has no window, renderer, audio or HUD, and it does no render extraction. Apart from tests, its only users are header files (`git grep GameSession` finds engine/game and SceneRuntime.h). There is no player executable in `tools/` or `samples/`. The consequences are visible in the code:
  - `world.PlaySound` produces `ChainExecutor::Sounds()` "for the host" (Executor.h:268). `Executor.cpp:572` clears them at the next step. Nothing outside `tests/chain/WorldEventsTests.cpp` reads them, so chain sounds never reach `AudioEngine`.
  - `StepAnimator` (Animator.h:119) is called only from `tests/anim/AnimatorTests.cpp`. CLAUDE.md says "Animation and localization are not stepped/owned by `GameSession`: a game drives them."
  - `FramePipeline` (the sim and render-prep overlap) and `Interpolate`/`Alpha()` have no production users (see ENG-003).
  - The renderer is driven only by `tools/editor/EditorRenderer.cpp`, which also does the ECS-to-`RenderObject` extraction (`EditorApp.cpp:937-975`).
- **Impact:** The milestone plan's M25.1.1 says "The runtime player is the same engine without the editor". Today, though, every piece of host glue (clock to steps, input to `RaiseInputAction`, render extraction, sound and spawn-request consumption, animator stepping, frame pacing) lives only in `tools/editor` or is missing. Each game would have to rewrite it, and the planned export work has no engine-level loop to build on.

### ENG-002 — Fixed-step duration has three independent definitions, and module systems never receive it
- **Severity:** Medium
- **Confidence:** High
- **Category:** Timing
- **Location:** `engine/runtime/include/interi/runtime/FixedStepClock.h:13`, `engine/chain/include/interi/chain/Executor.h:142`, `engine/game/src/GameSession.cpp:100`, `engine/ecs/include/interi/ecs/Scheduler.h:24-41`, `engine/hotreload/include/interi/hotreload/ModuleApi.h:92`
- **Evidence:** `FixedStepConfig::step{16'666'667}` (FixedStepClock.h:13) and `ExecutorDesc::stepNanoseconds = 16'666'667; ///< The fixed step (FixedStepClock's).` (Executor.h:142) are separate literals. A comment is the only thing that links them. `GameSession::StepNavigation` computes its dt from a default-constructed `chain::ExecutorDesc{}.stepNanoseconds` (GameSession.cpp:100), not from the `SceneRuntimeDesc::executor` the scene was created with. `SystemContext` exposes world, jobs, ticks and command buffers but no step time (Scheduler.h:24-41). Modules register systems only through `AddSystem(name, access, Scheduler::SystemFunction)` (ModuleApi.h:92), so a module system cannot learn the step length. `impact(FixedStepClock)` returned UNKNOWN with 0 callers. A text search confirmed the one production user, `ScenePlay.h:61`.
- **Impact:** The three values agree today, so this is a drift risk, not a live bug. If anyone configures a 30 Hz or 120 Hz step on the scene's executor, chains, navigation and module systems will silently run at different rates. Module authors have to hard-code 1/60 s, so a module's gameplay rate is coupled to an engine default it can't see.

### ENG-003 — No render interpolation: `Alpha()` and `Interpolate()` are never used and no previous state is kept
- **Severity:** Medium
- **Confidence:** High
- **Category:** Timing / frame pacing
- **Location:** `engine/runtime/include/interi/runtime/FixedStepClock.h:35-38`, `engine/runtime/include/interi/runtime/Interpolation.h:13`, `engine/editor/src/ScenePlay.cpp:24-35`
- **Evidence:** `git grep "\.Alpha()\|Interpolate("` over engine/tools/samples finds only the definition in Interpolation.h:13. `ScenePlaySession::Advance` runs `m_clock.Advance(elapsed)` steps and discards the remainder fraction (ScenePlay.cpp:30-34). No component or snapshot holds the previous step's `Transform`.
- **Impact:** At display rates that are not multiples of 60 Hz (50, 75, 144, VRR), a fixed 60 Hz simulation that is presented without interpolation makes moving objects judder: the number of steps per frame alternates. The "Fix Your Timestep" design is half done. The accumulator exists, but rendering ignores it.

### ENG-004 — The scene step rebuilds the full hierarchy into node-based maps several times per step, with no dirty tracking
- **Severity:** Medium
- **Confidence:** Medium (cost inferred from reading the code, not measured)
- **Category:** World representation / scene graph
- **Location:** `engine/scene/src/SceneRuntime.cpp:164-194`, `engine/scene/src/Hierarchy.cpp:56-110`, `engine/scene/src/Hierarchy.cpp:132-152`, `engine/scene/include/interi/scene/Hierarchy.h:24`, `engine/game/src/GameSession.cpp:106`, `engine/nav/src/NavAgents.cpp:75`
- **Evidence:** Each `SceneRuntime::Step` does the following:
  1. `BuildHierarchy(m_world)` (line 164) builds a `std::map<ObjectId, Node>`, with a `std::vector` of children per node, and does a random `world.Get<ParentComponent>` per node.
  2. It copies every world `Transform` into a `std::map<ObjectId, Transform>` (165-172).
  3. After the executor runs, it walks `PreOrder()`, which allocates a vector per root subtree (Hierarchy.cpp:10-38).
  4. `PropagateTransforms` (line 194) rebuilds the whole hierarchy again and builds another `std::map` of world transforms (Hierarchy.cpp:132-152).

  `GameSession::StepNavigation` builds a third index whenever an agent moved (GameSession.cpp:106). `StepNavAgents` re-plans a full `FindPath` for every agent every step (NavAgents.cpp:75). All of this is serial and uses the default heap.
- **Impact:** The per-step cost is O(n log n) in scene objects, plus many heap allocations, even when nothing moved. This puts a ceiling on scene size at 60 Hz and adds allocator churn on the simulation thread. The same work is repeated in up to three places each step.

### ENG-005 — Play-in-editor and GameSession use different step compositions, and navigation runs after propagation and trigger scanning
- **Severity:** Medium
- **Confidence:** High
- **Category:** Main loop / phase ordering
- **Location:** `engine/game/src/GameSession.cpp:90-95`, `engine/editor/src/ScenePlay.cpp:24-41`, `engine/scene/src/SceneRuntime.cpp:194-217`, `engine/nav/src/NavAgents.cpp:84`
- **Evidence:** `GameSession::Step` = `Host().RunSystems` → `m_scene->Step()` → `StepNavigation()`. `ScenePlaySession::Advance` and `StepOnce` call only `m_runtime->Step()`, so no module systems or navigation run. CLAUDE.md documents this as "module systems don't run in the editor yet". No shared step function exists for both hosts to call. Inside the game step, `SceneRuntime::Step` propagates transforms (line 194) and scans triggers (205-217) before `StepNavAgents` writes the world `Transform` directly (NavAgents.cpp:84). GameSession then writes the local transform.
- **Impact:** What designers test in the editor isn't what ships: navigation agents don't move and module systems don't tick during play-in-editor. In the game, children of a moving agent and the agent's trigger overlaps lag one step behind its position. The phase order (input → systems → chains → propagate → triggers → nav) is implicit in two hosts instead of declared once.

### ENG-006 — The asset pipeline produces data the runtime cannot consume: two skeleton formats, and no UV or skinning path in the renderer
- **Severity:** Medium
- **Confidence:** High
- **Category:** Resources and assets
- **Location:** `engine/assets/include/interi/assets/Skeleton.h:4,48`, `engine/anim/include/interi/anim/Animation.h:5,39`, `engine/render/include/interi/render/Mesh.h:13-18`, `engine/render/include/interi/render/ForwardRenderer.h:33-35`
- **Evidence:** The importer cooks `.iskel`/`.ianim`/`.iskin` (`interi::assets::Skeleton`, Skeleton.h:48), while the animation runtime defines its own `interi::anim::Skeleton` read from `.skeleton.json`/`.clip.json` (Animation.h:39). CLAUDE.md says "nothing converts these importer outputs to them yet". The render `Vertex` is `{position, normal}` with a `static_assert` on a 24-byte stride (Mesh.h:13-18), and materials are mapped triplanar because "Meshes have no UVs" (ForwardRenderer.h:33-35). Imported UVs, tangents, skins and texture slots therefore have no consumer.
- **Impact:** Two parallel type families for the same concept, with no conversion step, mean imported animated characters can't be played or drawn. Imported textured models render without their UV mapping. The cooked formats are a dead end until a cook stage targets the runtime formats.

### ENG-007 — No runtime resource manager: path-string references, synchronous loads, and no streaming or GUID resolution
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Resources and assets
- **Location:** `engine/values/include/interi/values/Value.h:38,65`, `engine/scene/include/interi/scene/Mesh.h:80-86`, `engine/platform/include/interi/platform/AsyncFileReader.h`, `engine/asset_import/include/interi/asset_import/AssetDatabase.h:77`
- **Evidence:** Scene components reference assets as `values::AssetRef`, "A virtual path" (Value.h:38). `MeshComponent::asset` is one of these (Mesh.h:83). The importer assigns stable GUIDs (`AssetDatabase`), but those live only in editor-side `.meta` files and `asset_import`, which is editor-only. A cypher query for engine classes named `*Cache`/`*Loader`/`*AssetManager`/`*Streamer` finds only the editor's `ThumbnailCache`. `AsyncFileReader` has no users outside `engine/platform` (git grep). `GameSession::Open` reads the nav mesh synchronously (GameSession.cpp:49-55).
- **Impact:** Moving or renaming a source asset breaks every scene reference, even though GUIDs exist. Nothing deduplicates or reference-counts loaded meshes and textures at run time, and nothing loads in the background. Large levels will hitch on load, and the planned export has no asset table to package.

### ENG-008 — The MeshPool is append-only, and editor uploads stall the GPU per mesh
- **Severity:** Medium
- **Confidence:** High
- **Category:** Rendering architecture / GPU resource lifetime
- **Location:** `engine/render/include/interi/render/MeshPool.h:33-56`, `tools/editor/EditorRenderer.cpp:36-53`, `tools/editor/EditorRenderer.cpp:150-209`
- **Evidence:** The `MeshPool` header says "Meshes are appended and never freed" (MeshPool.h:33-34). The only recovery is `Clear()`, which requires the GPU to be idle. `EditorRenderer::AddMesh` expands each triangle to 3 unshared vertices (lines 173-183). It uploads through `UploadNow`, which creates a 4 MiB `UploadQueue` and a command pool, submits, and calls `WaitForValue` for every mesh (36-53). On `OutOfMemory` it waits for the whole GPU, clears the pool and re-uploads (150-164, 202-206).
- **Impact:** Every mesh added during editing costs a full CPU-GPU round trip, and a full pool costs a GPU drain plus re-upload of everything visible. There is no sub-allocation or free list, so a game streaming levels in and out would grow the pool until it overflows. De-indexing triples vertex memory compared with the welded `TriangleMesh`.

### ENG-009 — Custom allocators and memory tags exist but are unused outside `engine/core`, so there are no subsystem budgets
- **Severity:** Low
- **Confidence:** High
- **Category:** Memory
- **Location:** `engine/core` (FrameAllocators, LinearAllocator, TrackedAllocator, MemoryTag); per-step users such as `engine/scene/src/Hierarchy.cpp:56-152` and `engine/scene/src/SceneRuntime.cpp:165`
- **Evidence:** `git grep -l "FrameAllocators\|TrackedAllocator\|LinearAllocator"` over engine/tools outside core returns nothing, and so does `git grep MemoryTag`. Per-step scratch data (hierarchy maps, transform snapshots) uses `std::map`/`std::vector` on the global heap. Budgets are planned for M25.3.
- **Impact:** No subsystem reports its memory use, and per-step scratch memory isn't reset per frame. Consoles (a stated target) need hard budgets, so this has to be retrofitted before M25.3.

### ENG-010 — Animation and navigation libraries depend on the Chain Reaction scripting layer
- **Severity:** Low
- **Confidence:** High
- **Category:** Architecture and layering
- **Location:** `engine/anim`, `engine/nav`, `engine/hud` (include scan); for example `engine/nav/src/NavAgents.cpp:43` (`chain::StableIdComponent`) and `engine/nav/src/NavAgents.cpp:95-104` (`RaiseNavTriggers(chain::ChainExecutor&)`)
- **Evidence:** An include scan of `<interi/<lib>/` gives: anim → {chain, core, platform, sdk}; nav → {chain, core, ecs, sdk}; hud → {chain, loc, values, ...}. The low-level runtime systems take their entity identity (`StableIdComponent`) and event raising from the scripting library.
- **Impact:** Navigation and animation can't be used without linking the visual-scripting runtime, and a change to `chain` recompiles and re-links them. Stable identity and the bindings are scene or gameplay concerns that could sit above both libraries.

### ENG-011 — No console variables and no crash handling or minidumps
- **Severity:** Low
- **Confidence:** High
- **Category:** Tooling
- **Location:** `engine/core/include/interi/core/Log.h:21-45` (levels and categories exist); `engine/`, `tools/` (absence)
- **Evidence:** A case-insensitive `git grep` for `cvar|ConsoleVariable|minidump|SetUnhandledExceptionFilter|sigaction` over engine and tools finds nothing. Logging has levels and categories (Log.h:21-45), and Tracy markers exist.
- **Impact:** Runtime tuning (step rate, worker count, render toggles) means editing code or command-line flags. Field crashes in a shipped player would leave no dump to diagnose.

### ENG-012 — Library layering is a clean DAG (positive observation)
- **Severity:** Info
- **Confidence:** Medium (from an include scan; the graph could not confirm it)
- **Category:** Architecture and layering
- **Location:** `engine/*/` include directives
- **Evidence:** The include scan gives core → ∅; jobs → core; ecs → core, jobs; rhi → core, jobs; render → core, rhi, shaders (no ecs or scene); runtime → core, jobs, platform; scene → chain, ecs, runtime, values... Graph queries found no edges from low layers (core, jobs, ecs, rhi, render, platform) or mid layers (scene, chain, runtime, values, sdk, anim, nav, assets, audio, hud) into editor, game, hotreload, asset_import, rhi_vulkan or platform_sdl, but IMPORTS edges for `<interi/...>` includes don't resolve in this index (see limitations). The RHI is backend-neutral (`rhi` never includes Vulkan).
- **Impact:** None. Keep it this way (see the Goal.md guardrails).

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | Community by symbolCount LIMIT 30 | All top 30 are third-party (SDL Joystick/Hidapi, Tracy, Manifold, ImGui docking) or test clusters |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 vcpkg buildtree flows (SDL_AppIterate, X11_CreateWindow...) |
| 4 | cypher | Process ENTRY_POINT_OF filtered to engine/ and tools/ | 0 rows (no first-party entry points recorded) |
| 5 | query | "fixed step clock main loop game session step" | No processes; definitions: ScenePlaySession (+Advance, m_clock), GameSessionTest, StarterSampleTest (~59 s, BM25 slow) |
| 6 | query | "frame graph compile execute transient aliasing barriers" | FrameGraph::Compile, TransientHeapSize, CreateTransients, Execute (FrameGraph.cpp:361-522) |
| 7 | query | "resource manager deferred destroy frames in flight" | rhi::ResourceManager (Defer/Collect/Destroy), IDevice::DestroyBuffer/DestroyHeap |
| 8 | context | ScenePlaySession | Ambiguous (class and ctor), 2 candidates |
| 9 | context | ResourceManager (Class, rhi) | 13 methods; held by ForwardRenderer, FrameGraph, MeshPool, GpuParticleSystem, DecalRenderer, ImGuiRenderer, RenderFeatureContext; accessed by EditorRenderer::Render |
| 10 | cypher | cross-module IMPORTS with `split()` | Error: function SPLIT does not exist |
| 11 | cypher | low layers (core/jobs/ecs/rhi/render/platform) IMPORTS high layers | 0 rows |
| 12 | context | uid Class ScenePlaySession | 1 caller (ScenePlaySession::Start); owned by EditorApp::m_scenePlay; holds m_clock, m_runtime |
| 13 | cypher | `(f:Method OR f:Function)` Step/Advance/Frame names | Parse error (label disjunction unsupported) |
| 14 | impact | FixedStepClock upstream (summaryOnly) | Ambiguous; risk UNKNOWN, 0 callers resolved. A text search confirmed the use in ScenePlay.h:61 |
| 15 | cypher | Methods Step/Advance/Frame/Run in game/runtime/tools/editor | GameSession::Step (GameSession.cpp:89), FixedStepClock::Advance, FramePipeline, EditorApp::Frame (EditorApp.cpp:1485), RunModuleLoop |
| 16 | cypher | Classes *Cache/*AssetManager/*Loader/*Streamer/*Registry in engine/ | Registries only, plus editor ThumbnailCache; no runtime asset manager |
| 17 | query | "load cooked mesh texture material at runtime for rendering" | Only CookModel process (AssetDatabase), editor AssetPanels, EditorRenderer::AddMesh |
| 18 | cypher | mid layers (scene/chain/runtime/values/sdk/anim/nav/assets/audio/hud) IMPORTS editor/game/hotreload/asset_import/tools/backends | 0 rows |
| 19 | check | cycles | 3 cycles in 2 components, all third-party (manifold wasm TS, SDL cocoa headers); first-party not confirmable (see limitations) |
| 20 | cypher | engine/scene/ IMPORTS any engine/ file | 0 rows: first-party `<interi/...>` include edges are not resolved |

## Analysis limitations
- IMPORTS edges for first-party `#include <interi/...>` directives don't resolve in this index (log #20), so the layering cyphers (#11, #18) and `check` (#19) can't prove the absence of first-party layering inversions or cycles. Layering (ENG-010, ENG-012) comes from a `git grep` include scan per engine library instead.
- The BM25 full-text index is degraded: `query` calls took about 59 s and returned mostly third-party definitions. Clusters and processes are dominated by vcpkg buildtrees, and there are no first-party processes. Discovery used filtered cypher plus targeted reads.
- `modules/` (physics, destruction, which hold the reference editor extension and the physics step) isn't indexed and wasn't reviewed. Physics fixed-step integration is therefore not assessed.
- All performance statements (ENG-004, ENG-008) come from reading the code. No profiling, benchmarks or builds were run (analysis is read-only).
- The editor viewport's behaviour during play (what it draws relative to the simulation step) was not traced beyond `EditorApp::Frame`.

## Cross-discipline notes
- Performance: per-step `std::map` hierarchy rebuilds and per-agent re-planning (`engine/scene/src/SceneRuntime.cpp:164-194`, `engine/nav/src/NavAgents.cpp:75`) are worth benchmarking at 10k objects.
- Graphics/Rendering: there is no UV or skinning vertex path (`engine/render/include/interi/render/Mesh.h:13-18`), and editor uploads are synchronous (`tools/editor/EditorRenderer.cpp:36-53`).
- Audio: chain `SoundRequest`s are never consumed by any host (`engine/chain/include/interi/chain/Executor.h:268`).
- Platform/Release: there is no player executable or packaging yet (M25.1); there is no crash handler (ENG-011).
