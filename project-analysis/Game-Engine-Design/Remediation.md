# Game-Engine-Design — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. ENG-002, ENG-005: one step-time source and one declared step pipeline. These are cheap and are the foundation the player loop builds on.
2. ENG-001, ENG-003: the engine-level runtime loop (`interi::app`/player) with interpolation, built on the shared step. This unblocks M25.1.
3. ENG-004: make the scene step incremental before content scales.
4. ENG-007, ENG-006, ENG-008: runtime asset manager, a cook stage into runtime formats, and a mesh pool that frees. The player needs these to load real content.
5. ENG-009, ENG-010, ENG-011: budgets (with M25.3), decoupling anim and nav from chain, and cvars plus a crash handler.

## ENG-001 — No runtime game loop: nothing composes simulation, rendering, audio, animation and input outside the editor
- **Severity / Effort:** High / XL
- **Depends on:** ENG-002, ENG-005 (shared step); ENG-003 (interpolation); ENG-007 (asset loading for render extraction)
- **Steps:**
  1. Add an engine library (e.g. `engine/app`, `interi::app`, shared) that owns the frame loop: `platform.PollEvents` → `InputMapper` → `GameSession::RaiseInputAction` → `FixedStepClock::Advance` → N × `GameSession::Step()` (collect per-step outputs after each step) → render extraction → `ForwardRenderer` → present. Keep it free of editor and ImGui includes.
  2. After **each** fixed step, drain `Scene().Executor().Sounds()` into `audio::AudioEngine` (cue → clip lookup through the resource manager, attached entity → `Spatializer` position). The vector is cleared on the next step (Executor.cpp:572), so per-frame polling would lose sounds from earlier substeps.
  3. Add an engine-owned animation system: per step, `StepAnimator` for every `AnimatorComponent` using the `AnimationLibrary` from `LoadModuleAnimations`, writing poses for render extraction.
  4. Move ECS-to-`RenderObject` extraction out of `tools/editor/EditorApp.cpp:937-975` into the app library (a `RenderExtract(World&, alpha) -> RenderSnapshot`), and use it from both the editor viewport and the player.
  5. Optionally run extraction and simulation through `FramePipeline<RenderSnapshot>` (it already exists and is TSan-tested).
  6. Build a `interi_player` executable (`tools/player`) that opens `GameSession` from a project folder. This is M25.1.1.
- **Illustrative change (not applied):**
  ```cpp
  // engine/app/src/GameLoop.cpp (sketch)
  void GameLoop::Frame(std::chrono::nanoseconds elapsed)
  {
      PumpInput();                                   // InputMapper -> session.RaiseInputAction
      for (std::uint32_t n = m_clock.Advance(elapsed); n > 0; --n)
      {
          m_previous = CaptureTransforms(m_session->World());
          m_session->Step();
          m_audio.Play(m_session->Scene().Executor().Sounds());
      }
      m_renderer.Draw(RenderExtract(m_session->World(), m_previous, m_clock.Alpha()));
  }
  ```
- **Verification:** A headless integration test (`interi_app_tests`, label `integration`) runs the starter project for 600 frames with the Null audio backend and lavapipe. It asserts that coins are collected, that every `world.PlaySound` reached the audio engine (count matches the chain trace), and that a golden frame matches. `interi_player --project samples/modules/starter/templates/coins --frames 120 --screenshot out.ppm` exits 0.
- **Risks / trade-offs:** The extraction API becomes a contract between modules' render features and the player. Reuse `IEditorExtensions` render-feature semantics, or define a runtime equivalent, rather than inventing a third path.

## ENG-002 — Fixed-step duration has three independent definitions, and module systems never receive it
- **Severity / Effort:** Medium / M
- **Depends on:** None (bumps `INTERI_MODULE_ABI_VERSION` if `SystemContext` changes)
- **Steps:**
  1. Make `FixedStepConfig` (or a new `StepTime { std::int64_t stepNs; std::uint64_t index; }`) the single source. Derive `ExecutorDesc::stepNanoseconds` from it in `SceneRuntime::Create` instead of keeping a separate default literal.
  2. In `GameSession::StepNavigation` (GameSession.cpp:100), read the step from `m_scene->Executor()` (expose `StepNanoseconds()`), not from `chain::ExecutorDesc{}`.
  3. Add `StepTime` to `SystemContext` (`ctx.StepSeconds()`, `ctx.StepIndex()`) and pass it through `Scheduler::Run` and `ModuleHost::RunSystems`. Bump the module ABI version.
  4. Add a test that configures a 30 Hz step and checks that chains (`Delay`), nav movement and a test module system all observe 1/30 s.
- **Illustrative change (not applied):**
  ```cpp
  // before: const float kStepSeconds = static_cast<float>(chain::ExecutorDesc{}.stepNanoseconds) * 1e-9f;
  // after:
  const float stepSeconds = static_cast<float>(m_scene->Executor().StepNanoseconds()) * 1e-9f;
  ```
- **Verification:** The new `GameSession.CustomStep_AllSubsystemsAgree` test passes. `git grep "16'666'667"` finds the literal in exactly one place.
- **Risks / trade-offs:** Changing `SystemContext` is a module ABI break. Batch it with other ABI changes.

## ENG-003 — No render interpolation: `Alpha()` and `Interpolate()` are never used and no previous state is kept
- **Severity / Effort:** Medium / M
- **Depends on:** ENG-001 (the render extraction point)
- **Steps:**
  1. Before each fixed step, capture the previous world `Transform` of renderable entities. Use a `PreviousTransform` engine component written by the step pipeline, or a snapshot array keyed by stable id.
  2. In render extraction, emit `Interpolate(previous, current, static_cast<float>(clock.Alpha()))` for each object. Teleports (spawn, restart, load) snap: reset `previous = current` when an entity is created or the scene restarts.
  3. Apply the same in the editor's play viewport via `ScenePlaySession` (expose `Alpha()`).
- **Verification:** A unit test steps a moving object at 60 Hz, renders at 144 Hz with a fake clock, and asserts that extracted positions increase monotonically with near-constant deltas. The test runs headless with no GPU, comparing extracted transforms.
- **Risks / trade-offs:** Interpolation adds one step of visual latency. Keep it switchable (a cvar, see ENG-011) for debugging.

## ENG-004 — The scene step rebuilds the full hierarchy into node-based maps several times per step, with no dirty tracking
- **Severity / Effort:** Medium / L
- **Depends on:** ENG-005 (one step pipeline means one place to maintain the index)
- **Steps:**
  1. Keep a persistent `HierarchyIndex` in `SceneRuntime`, using sorted flat vectors (`std::vector<Node>` indexed by dense slot plus a `HashMap<ObjectId, slot>`) instead of `std::map`. Update it from structural commands (spawn, destroy, reparent), not by rescanning.
  2. Use `Changed<LocalTransformComponent>` and `Changed<ParentComponent>` (the ECS already has Bevy-style ticks) to propagate only dirty subtrees. Keep id order for determinism.
  3. Replace the `std::map<ObjectId, Transform> worlds` snapshot (SceneRuntime.cpp:165) with a per-step scratch vector from a frame allocator (ENG-009).
  4. Pass the maintained index to `GameSession::StepNavigation` instead of calling `BuildHierarchy` again.
  5. In `StepNavAgents`, cache the path per agent and re-plan only when the destination changes, the mesh is swapped, or the agent deviates. Keep the stable-id order.
  6. Add a benchmark (`benchmarks/scene`) for `SceneRuntime::Step` with 1k/10k/50k objects and 0% and 1% moving.
- **Verification:** The benchmark shows `Step` cost proportional to moved objects for the 1% case. The existing determinism tests (1/4/32 workers, `ChainDeterminism.MatchesCrossPlatformGoldenHash`) stay green.
- **Risks / trade-offs:** An incremental index has to handle the existing broken-parent and loop rules exactly. Keep `BuildHierarchy` as the reference and add a debug-build cross-check against it.

## ENG-005 — Play-in-editor and GameSession use different step compositions, and navigation runs after propagation and trigger scanning
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Define one step function (e.g. `game::StepWorld(StepContext&)`) with an explicit, documented phase list: module systems → chains (executor) → navigation → adopt and propagate transforms → spawns → trigger scan.
  2. Make both `GameSession::Step` and `ScenePlaySession::Advance`/`StepOnce` call it. The editor passes its `EditorModules` host (systems may be optional behind a flag until module systems are supported in the editor).
  3. Move `StepNavAgents` before `PropagateTransforms` and `ScanTriggers`, writing `LocalTransformComponent` (as GameSession already does) instead of the world `Transform`, so propagation places children and triggers see the final position.
- **Verification:** A test with a nav agent that has a child and a trigger volume on its path: the child's world position and the overlap begin occur in the same step as the agent's move. A play-in-editor test with a nav agent shows it moving.
- **Risks / trade-offs:** Reordering nav changes chain-visible timing (`nav.Arrived` step). Re-baseline any affected golden hash deliberately.

## ENG-006 — The asset pipeline produces data the runtime cannot consume: two skeleton formats, and no UV or skinning path in the renderer
- **Severity / Effort:** Medium / L
- **Depends on:** ENG-007
- **Steps:**
  1. Pick one runtime skeleton and clip representation. Either add a cook step in `AssetDatabase` that writes `anim`'s formats from `.iskel`/`.ianim`, or have `engine/anim` read the cooked binary `assets::Skeleton` directly and retire the JSON as an authoring-only format. Prefer cooked binary at run time.
  2. Add a second vertex stream to the render path (UV and tangent, and for skinned meshes, joint indices and weights in a separate buffer, still vertex-pulled), keeping the 24-byte position and normal stream so goldens for untextured meshes stay unchanged.
  3. Add GPU skinning: a compute pre-pass writes skinned positions into a transient buffer from the frame graph, fed by `ComputeSkinMatrices`.
- **Verification:** An integration test imports the glTF cube fixture with UVs and a skinned fixture, cooks, loads at run time, renders a golden image with a UV-mapped texture, and plays a clip with the skinned pose.
- **Risks / trade-offs:** The `static_assert(sizeof(Vertex) == 24)` and `ForwardCommon.hlsli` layouts change together. Use separate streams to avoid touching existing goldens.

## ENG-007 — No runtime resource manager: path-string references, synchronous loads, and no streaming or GUID resolution
- **Severity / Effort:** Medium / XL
- **Depends on:** None (prerequisite for ENG-001 and ENG-006)
- **Steps:**
  1. Add a runtime `interi::assets::ResourceManager`: `Handle<T>` (generational, like `SlotMap`) keyed by `Guid`, reference counts, states Unloaded/Loading/Ready/Failed, loading through `AsyncFileReader` on Low-priority jobs, and GPU upload through `rhi::UploadQueue` with retirement through `rhi::ResourceManager`.
  2. Have the cook (`AssetDatabase`) emit an asset table (`guid → cooked path, kind, dependencies`) that the player package ships.
  3. Allow `AssetRef` to hold a GUID (a new `Value` encoding, or a sibling `AssetGuid` type appended to `ValueType`), with path fallback for hand-written scenes. The editor writes GUIDs.
  4. Load the nav mesh and scene meshes through it, not ad hoc in `GameSession::Open`.
- **Verification:** A test renames a source model in a scratch project, re-imports, and checks that the scene still resolves its meshes by GUID. A test loads 100 meshes asynchronously while stepping, and the step never blocks on IO (`WaitFor` is not called from jobs).
- **Risks / trade-offs:** This is a large API surface. Start with meshes and textures only. Hot reload of cooked assets should reuse `AssetWatcher`.

## ENG-008 — The MeshPool is append-only, and editor uploads stall the GPU per mesh
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Give `MeshPool` a sub-allocator for vertex and index ranges (a TLSF or free-list over the buffers; `TlsfAllocator` exists in core) with `Remove(MeshRange)` that retires the range at `LastSubmittedValue() + 1`, like `ResourceManager`.
  2. In `EditorRenderer`, batch all new meshes for a frame into the frame's command list and `UploadQueue` (one per frame in flight) instead of `UploadNow` per mesh. Remove the per-mesh `WaitForValue`.
  3. Upload the welded `TriangleMesh` with indices. Split vertices only where normals differ (the importer already welds), instead of 3 vertices per triangle.
- **Verification:** A unit test (MockDevice) adds and removes meshes repeatedly and checks that the pool never reports `OutOfMemory` while the live size stays below capacity. An editor smoke run with 1,000 distinct meshes completes with zero `WaitForValue` calls during mesh adds (count via `NullDevice`/mock).
- **Risks / trade-offs:** Fragmentation. Measure it and allow an occasional compaction during a load screen.

## ENG-009 — Custom allocators and memory tags exist but are unused outside `engine/core`, so there are no subsystem budgets
- **Severity / Effort:** Low / L
- **Depends on:** ENG-004 (scratch data moves to frame allocators); aligns with plan M25.3
- **Steps:**
  1. Give `World`, `ChainExecutor`, `SceneRuntime`, `MeshPool` (CPU side) and audio a `TrackedAllocator` with their own `MemoryTag`.
  2. Route per-step scratch memory (hierarchy, snapshots, wave activations) through `FrameAllocators`, reset at the start of each fixed step.
  3. Expose per-tag statistics in the editor console and Tracy plots, and define budgets with a policy hook (M25.3).
- **Verification:** A test reports non-zero bytes for each tag after running the starter project. A stress run shows no global-heap allocations inside `SceneRuntime::Step` in steady state (count via a counting `IAllocator`).
- **Risks / trade-offs:** Threading `IAllocator*` through std containers adds API noise. Use `StlAllocator<T>` consistently.

## ENG-010 — Animation and navigation libraries depend on the Chain Reaction scripting layer
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Move `StableIdComponent` (and `EntityRef` resolution) to a lower library (`ecs` or a small `interi::identity`). Keep a `chain` alias for source compatibility.
  2. Split `anim`/`nav` into the core runtime (sampling, path-finding, stepping) and a `*_chain` binding target that registers `anim.Play`, `nav.MoveTo` and `RaiseNavTriggers`.
- **Verification:** The include scan shows `engine/anim` and `engine/nav` core targets without `<interi/chain/`. CMake target dependencies show no `interi::chain` link for them.
- **Risks / trade-offs:** Changing `StableIdComponent`'s `kComponentName` would break saved scenes. Keep the name and move only the header.

## ENG-011 — No console variables and no crash handling or minidumps
- **Severity / Effort:** Low / M
- **Depends on:** ENG-001 (the player hosts them)
- **Steps:**
  1. Add a small cvar registry in core (name, `values::Value`, flags, change callback), with values set from the command line, a config file and the editor console panel.
  2. Install a crash handler in the player and the editor: `SetUnhandledExceptionFilter` plus `MiniDumpWriteDump` on Windows, and `sigaction` on Linux writing a backtrace, then flush the `Logger`.
- **Verification:** A test sets `r.interpolate=0` from the command line and observes it. A deliberate-crash test in a subprocess produces a dump or backtrace file.
- **Risks / trade-offs:** Signal handlers must be async-signal-safe. Keep the Linux handler minimal.
