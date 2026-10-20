# Game-Engine-Design — Goal State

## Vision
Interi ships a game with the same engine as the editor, minus the editor. An engine-owned loop turns wall time into fixed steps from a single step-time source. It runs one declared step pipeline (module systems, chains, navigation, transform propagation, triggers) shared by the player and play-in-editor, and presents interpolated state. Chain sound requests reach the audio engine and animators are stepped by the engine. Assets are referenced by GUID, loaded asynchronously through a reference-counted runtime resource manager, and cooked into formats the runtime actually consumes (UVs, skins, skeletons). GPU geometry can be freed. The scene step is incremental and allocation-free in steady state, memory is tracked per subsystem, and the clean library DAG is preserved.

## Acceptance criteria
- [ ] AC-1: An `interi_player` executable (or an `interi::app` library plus launcher) runs the starter project headless for 600 frames. An integration test asserts gameplay progress, a golden frame, and that the number of `world.PlaySound` requests equals the number of plays received by `AudioEngine` (resolves ENG-001)
- [ ] AC-2: `AnimatorComponent`s are stepped by engine code during `GameSession`/player steps, verified by a test that plays a clip through a chain `anim.Play` (resolves ENG-001)
- [ ] AC-3: `git grep "16'666'667" -- engine` finds exactly one definition. `SystemContext` exposes the step time. A test with a 30 Hz step shows chains, navigation and a module system all using 1/30 s (resolves ENG-002)
- [ ] AC-4: Render extraction uses `Interpolate(previous, current, Alpha())`. A headless test at a simulated 144 Hz display over 60 Hz simulation shows monotonic, near-uniform extracted positions (resolves ENG-003)
- [ ] AC-5: A `SceneRuntime::Step` benchmark at 10k objects with 1% moving runs at least 5× faster than the 100%-moving case, and the steady-state step performs zero global-heap allocations (counting allocator). Determinism tests at 1/4/32 workers stay green (resolves ENG-004, ENG-009)
- [ ] AC-6: `GameSession::Step` and `ScenePlaySession` call one shared step function. A test shows a nav agent's child and the agent's trigger overlap updating in the same step as the agent's move, and a nav agent moving in play-in-editor (resolves ENG-005)
- [ ] AC-7: An imported skinned glTF plays a clip at run time without hand-written `.skeleton.json`, and an imported UV-mapped texture renders with its UVs in a golden test. The engine has one runtime skeleton type (resolves ENG-006)
- [ ] AC-8: Scene mesh references survive renaming the source asset (GUID resolution test). Meshes and textures load through a runtime resource manager on Low-priority jobs, with no IO wait inside the fixed step (resolves ENG-007)
- [ ] AC-9: `MeshPool` supports `Remove`. Repeated add/remove cycles never return `OutOfMemory` below capacity, and editor mesh adds issue no per-mesh `WaitForValue` (resolves ENG-008)
- [ ] AC-10: Each major subsystem (ECS world, chains, scene, render CPU side, audio) reports bytes under its own `MemoryTag`, visible in the editor console or Tracy (resolves ENG-009)
- [ ] AC-11: The `engine/anim` and `engine/nav` core targets neither include `<interi/chain/...>` nor link `interi::chain`. Chain bindings live in separate targets (resolves ENG-010)
- [ ] AC-12: A cvar can be set from the command line and the console, and a forced crash in the player produces a minidump (Windows) or backtrace file (Linux) (resolves ENG-011)

## Guardrails to keep it that way
- A CMake or CTest include-layering check (extend `tools/platform_audit` or add a script) encodes the allowed `engine/<lib>` include graph: core → jobs → ecs/rhi → render, with no low-to-high includes, and render never including ecs, scene or editor. This preserves the clean DAG observed in ENG-012 and enforces AC-11.
- The player integration test (AC-1) runs in CI on lavapipe with the Null audio backend, so the loop composition can't silently rot.
- A `SceneRuntime::Step` benchmark is registered with the existing `--benchmark_dry_run` smoke test, and regressions are compared with `tools/bench_compare`.
- Review checklist item: any new per-step subsystem must be added to the shared step function (AC-6) and must take step time from `StepTime`, never from a literal.
- `ChainDeterminism.MatchesCrossPlatformGoldenHash` and the 1/4/32-worker comparisons stay mandatory for every step-order change.

## Out of scope
- Physics fixed-step integration (lives in `modules/physics`, not reviewed here).
- Console platform backends (private repositories).
- Netcode and lockstep multiplayer (designed for, not planned).
- Renderer feature work beyond what the cooked asset formats require (e.g. deferred shading, GI).
