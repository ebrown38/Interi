# Program Architecture — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. **ARCH-004 and ARCH-005 (rules and docs first).** Write down the intended layer order (an ADR plus `docs/architecture.md`) and add a CMake layering check in warn-only mode. Every later fix can then be proven by the check turning green.
2. **ARCH-001, then ARCH-003.** Introduce an engine-feature registration seam and move anim, nav and AI registration out of `ModuleHost`. The editor fallback then uses the same seam, which removes the duplicated contract set.
3. **ARCH-008 and ARCH-009.** These are small, independent dependency and coupling cleanups. Do them while the ARCH-001 work is in progress.
4. **ARCH-002 and ARCH-007.** Extract a shared runtime composition root and the render bridge before M25 starts. Then slim `EditorApp` onto it.
5. **ARCH-006.** Add an automated ABI-surface guard once ARCH-001 has narrowed what the ABI headers include.

## ARCH-001 — The module host (module ABI layer) hard-codes specific engine features (anim, nav, AI) and owns navigation state
- **Severity / Effort:** High / L
- **Depends on:** ARCH-004 (to keep the new rule enforced)
- **Steps:**
  1. Add a small engine-feature seam below `hotreload`, using Branch by Abstraction. One option is `sdk` or `chain`, since `chain::ChainRegistry` is what features register into, e.g. `interi/chain/EngineLibrary.h`:
     - `struct EngineLibrary { std::string_view name; std::vector<std::string> (*contracts)(); Result<void> (*registerInto)(ChainRegistry&, ServiceTable&); };`
     - plus a value type `EngineLibraries` (a plain list, not a global).
  2. In `anim`, `nav` and (moved out of `sdk`) an `ai` feature, expose `anim::EngineLibrary()`, `nav::EngineLibrary(std::shared_ptr<NavSource>)` and so on, each returning that descriptor.
  3. Change `ModuleHostDesc` to take `std::span<const EngineLibrary>` (plus the services they provide). Replace `ModuleHost.cpp:376-408` with a loop over the descriptors. Keep only `chain::CoreContracts()` and `sdk::PhysicsContracts()` built in, because those are part of the module contract itself.
  4. Move `NavSource` ownership and `NavigationService` from `hotreload` into `nav` or the composition root (`game`, see ARCH-002). Register the service through `ModuleHostDesc::services`, which already exists for this purpose. Remove `Navigation()` and the `nav` include from `ModuleHost.h`.
  5. Drop `interi::anim interi::nav` from `engine/hotreload/CMakeLists.txt:17`. Add them to `game` (which already has `nav`) and to `editor`.
  6. Bump `INTERI_MODULE_ABI_VERSION` (the ModuleHost/registrar surface changes). Rebuild `modules/physics` and `modules/destruction` standalone against the installed engine.
- **Illustrative change (not applied):**
  ```cpp
  // before (ModuleHost.cpp)
  for (std::string& id : anim::AnimationContracts()) m_desc.engineContracts.push_back(std::move(id));
  auto animation = anim::RegisterAnimationLibrary(*m_chains);
  // after
  for (const EngineLibrary& lib : m_desc.engineLibraries)
      for (std::string& id : lib.contracts()) m_moduleSet->DeclareContract(id);
  for (const EngineLibrary& lib : m_desc.engineLibraries)
      INTERI_VERIFY(lib.registerInto(*m_chains, m_desc.services).has_value(), "engine library must register");
  ```
- **Verification:**
  - `grep -rE "#include <interi/(anim|nav)/" engine/hotreload` returns nothing.
  - The ARCH-004 layering check passes with the rule "hotreload must not depend on anim, nav, scene, game".
  - `ModuleHostTest`, `ModuleProjectTest`, `PhysicsEngineHostTests` and the starter sample tests pass under debug, asan and tsan.
  - Both out-of-tree modules build and load.
- **Risks / trade-offs:**
  - This is an ABI bump.
  - `GameSession`, `EditorModules` and every test that constructs a `ModuleHost` must pass the feature list. Mitigate with a `game::DefaultEngineLibraries()` helper.
  - A static "self-registering" plugin list was considered and rejected: it reintroduces global state and conflicts with the "keep globals out of public headers" rule.

## ARCH-002 — No reusable runtime composition root: game, play-in-editor and the renderer bridge are assembled separately and none of them drives anim or audio
- **Severity / Effort:** High / XL
- **Depends on:** ARCH-001 (feature seam), ARCH-009 (single step owner)
- **Steps:**
  1. Define the frame/step pipeline once in `engine/game`, using the Strangler Fig pattern: a `GameLoop` (or extend `GameSession`) that owns the `FixedStepClock`. Each fixed step runs, in a documented order:
     - module systems
     - `SceneRuntime::Step`
     - navigation agents
     - `StepAnimator` over `AnimatorComponent`s
     - drain `ChainExecutor::Sounds()` into an optional `audio::AudioEngine` sink
     
     Then it does render prep through `FramePipeline<Snapshot>`.
  2. Make each stage optional by injection (an `IAudioSink*`, an `IRenderSink*`), so headless tests keep running with nothing attached.
  3. Extract the scene-to-`ForwardRenderer` part of `tools/editor/EditorRenderer.cpp` into a library, e.g. `engine/scene_render` (static, depends on `scene` and `render`). The editor keeps only viewport, ImGui and overlay code.
  4. Make `ScenePlaySession` a thin wrapper over the same `GameLoop`, constructed on the editor's `EditorModules` host. Play-in-editor then runs module systems, navigation and animation exactly like the game. This also closes the "module systems don't run in the editor" gap.
  5. Build the M25 player (`tools/player`) as `GameLoop` + `scene_render` + `platform_sdl` + `rhi_vulkan` only. It has no editor dependency.
  6. Add a parity test: the starter sample run N steps through `GameSession` and through `ScenePlaySession` produces identical `HashWorld` and Values hashes.
- **Illustrative change (not applied):**
  ```cpp
  void GameLoop::FixedStep()
  {
      m_host.RunSystems(m_jobs, m_scene->Commands());
      m_scene->Step();
      StepNavigation(m_stepSeconds);
      anim::StepAnimator(m_world, m_animations, m_stepSeconds);
      if (m_audio) { m_audio->Play(m_scene->Executor().Sounds()); }
  }
  ```
- **Verification:**
  - The parity test passes at 1/4/16 workers.
  - Starter sample tests pass unchanged.
  - A chain using `world.PlaySound` produces a voice in an `Offline` `AudioEngine` in a test.
  - `level_editor` no longer contains a `ForwardRenderer` scene submission (grep).
  - The player target links no `interi::editor*`.
- **Risks / trade-offs:**
  - Large change across editor and game, so do it in phases (render bridge extraction first, since it has no behaviour change).
  - The step order becomes a contract. Pin it in a test and CLAUDE.md.
  - Determinism golden hashes are unaffected as long as chain evaluation does not change.

## ARCH-003 — The "engine contract set" and engine chain libraries are assembled in two places that have already diverged
- **Severity / Effort:** Medium / S (M if done before ARCH-001)
- **Depends on:** ARCH-001 (preferably)
- **Steps:**
  1. Short term: add a single function, e.g. `hotreload::EngineContracts()` or `game::DefaultEngineLibraries()`, returning the complete list (core, anim, nav, AI, physics). Use it in both `ModuleHost.cpp:376-395` and `EditorApp.cpp:94-106,136-147`.
  2. Better: when no project is open, have `EditorApp` construct an empty `ModuleHost` (or `EditorModules` with an empty project) and always use its `Chains()`. Then delete `m_chainModules` and `m_chains` from `EditorApp.h:209-210`.
- **Verification:**
  - A headless test asserts that the editor's fallback registry declares exactly the contracts a `ModuleHost` declares (compare `ModuleSet` contract lists).
  - `grep -n "AnimationContracts" tools/editor` is empty.
- **Risks / trade-offs:** Constructing a `ModuleHost` needs a `World`, and the editor already has a scratch one. This path fails on a static engine (`ModuleHost::Supported()` false), so keep the function-based list as the fallback there.

## ARCH-004 — Dependency rules are not declared or enforced; all module dependencies are PUBLIC and several direct dependencies are undeclared
- **Severity / Effort:** Medium / M
- **Depends on:** ARCH-005 (the rule text)
- **Steps:**
  1. Give each module a layer number in `interi_add_module`, e.g. `LAYER 0` for core and `LAYER 9` for editor_ui, following the to-be diagram in Goal.md.
  2. In `cmake/InteriModule.cmake`, record the layer as a target property. After all modules are added (a deferred call at the end of the root `CMakeLists.txt`), fail configuration when any `interi::*` dependency has a higher or equal layer outside an explicit allowlist.
  3. Add a CTest `license`-style test, e.g. label `unit`, `ArchLayering_IncludesMatchDeps`. It scans `engine/<m>/{include,src}` for `#include <interi/X/` and fails when X is neither `m` nor in the module's declared `DEPS`/`PRIVATE_DEPS`. This covers the include direction that link rules cannot see.
  4. Fix the undeclared dependencies it reports:
     - `render`, `rhi_vulkan`, `platform_sdl` → core
     - `hotreload` → jobs
     - `editor_ui` → assets, chain, ecs, scene, values
     
     Remove unused declared ones (`anim` → ecs, values) if they are truly unused.
  5. Split `DEPS` into public (used in public headers) and `PRIVATE_DEPS` (used only in `src/`), so the transitive closure shrinks.
- **Illustrative change (not applied):**
  ```cmake
  interi_add_module(render SHARED LAYER 3 ... DEPS interi::rhi interi::core)
  # end of root CMakeLists.txt
  interi_check_layers()   # FATAL_ERROR: "hotreload (L6) must not depend on anim (L5) ..."  (if anim is above it)
  ```
- **Verification:**
  - Configure fails if a test branch adds `#include <interi/hotreload/...>` to `engine/scene`.
  - CI runs the layering test on Linux and Windows.
- **Risks / trade-offs:**
  - Moving dependencies to PRIVATE can break out-of-tree consumers that relied on transitive includes. Run the `Package_*` integration tests and build both modules standalone.
  - Start the check in warn-only mode, then make it fatal after ARCH-001.

## ARCH-005 — No human-facing architecture overview, layer diagram or ADRs; decision records are discarded; agent docs have drifted
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Create `docs/architecture.md` containing:
     - the module/layer diagram (mermaid, from Goal.md)
     - the dependency rules
     - state ownership (who owns `World`, `JobSystem`, `ModuleHost`, registries, `SaveRegistry`, `NavSource`)
     - the threading model summary
     - the composition roots (`game`, `level_editor`, the M25 player)
  2. Create `docs/adr/` with a template, and back-fill ADRs for the decisions that are currently only in CLAUDE.md or gitignored `findings.md`:
     - shared engine and toolchain fingerprint
     - physics as a module (Physics Engine rule)
     - `ContributionRegistry` identity rule
     - deterministic chains with no trigonometry
     - bindless RHI
     - `FileWatcher` polling
  3. Change the milestone process (CLAUDE.md "Starting a milestone"): any decision in `findings.md` that outlives the milestone becomes an ADR in the phase commit.
  4. Fix CLAUDE.md:54 to list the 17 shared modules, or better, reference a generated list. Make `AGENTS.md` a pointer to CLAUDE.md, or generate it from CLAUDE.md, so they cannot diverge.
  5. Add a short "Architecture" section to README.md linking to `docs/architecture.md`.
- **Verification:**
  - `docs/architecture.md` and at least 6 ADRs exist.
  - A check (script or CTest) confirms that every `interi_add_module(... SHARED` name appears in the shared list in CLAUDE.md, and that every engine module appears in `docs/architecture.md`.
- **Risks / trade-offs:** Docs rot without the check, so tie the module list to CMake.

## ARCH-006 — Module ABI surface is wide and transitive, but ABI compatibility relies on a hand-bumped version number
- **Severity / Effort:** Medium / M
- **Depends on:** ARCH-001 (narrows the surface first)
- **Steps:**
  1. Add a generated layout fingerprint to the toolchain fingerprint: a constexpr hash of `sizeof`/`alignof` of the types the registrar exposes (`ModuleContext`, `ModuleRegistrar`, `ModuleApi`, `values::DataRegistry`, `chain::ChainRegistry`, `SaveRegistry`, `sdk::HookRegistry`, `Scheduler`). The loader then rejects a mismatched library even when the manual version was not bumped.
  2. Alternatively or additionally, add a CI test that keeps a committed list of the ABI-surface headers' hashes (`tests/hotreload/abi_surface.txt`). It fails when one changes while `INTERI_MODULE_ABI_VERSION` did not, prompting the bump (`INTERI_UPDATE_GOLDEN=1` to refresh).
  3. Shrink `ModuleApi.h` includes by forward-declaring registries where only references are used.
- **Verification:**
  - Changing a member of `DataRegistry` without bumping the version makes `ModuleHostTest` reject a stale test module (new negative test, like the existing "wrong toolchain" variant 7).
- **Risks / trade-offs:** `sizeof`-based hashing misses same-size reorderings. The header-hash test catches those but over-triggers on comment edits, so you may want to strip comments before hashing.

## ARCH-007 — `EditorApp` is an untested god object that holds the editor's composition logic inside the executable
- **Severity / Effort:** Medium / L
- **Depends on:** ARCH-002, ARCH-003
- **Steps:**
  1. Move non-UI orchestration (project open/close, module host lifetime, `ActiveData`/`ActiveChains`, trust gating, play start/stop) into a headless `editor::EditorSession` class in `interi::editor`, with tests.
  2. Leave `EditorApp` as UI wiring: windows, dock layout, input routing and frame drawing.
  3. Register a CTest `integration` test that runs `level_editor --video-driver offscreen --frames 120` on lavapipe (`INTERI_REQUIRE_GPU` jobs only), plus `--project-file samples/... --play 1`.
- **Verification:**
  - `EditorApp.cpp` shrinks substantially (target < 800 lines).
  - `EditorSession` tests cover fallback registry selection and failed module load.
  - CI runs the smoke test.
- **Risks / trade-offs:** The GPU smoke test adds CI time and needs lavapipe, which CI already uses for golden tests.

## ARCH-008 — `FileWatcher` lives in `hotreload`, so the editor-only `asset_import` links the whole module host
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Move `FileWatcher.h/.cpp` and `FileWatcherTests.cpp` to `engine/platform` (namespace `interi`), keeping a forwarding alias `hotreload::FileWatcher` for one milestone.
  2. Change `engine/asset_import/CMakeLists.txt:27` to drop `interi::hotreload`.
  3. Optionally move `ModuleDocs` to `editor` or `tools/module_check`.
- **Verification:** `grep -rn "interi/hotreload" engine/asset_import` is empty, and the asset import tests pass.
- **Risks / trade-offs:** Out-of-tree modules using `hotreload::FileWatcher` need the alias during migration.

## ARCH-009 — `GameSession` derives the navigation step from a default-constructed `ExecutorDesc` instead of the running executor's step
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add the step to `GameDesc`, pass it into `SceneRuntimeDesc::executor.stepNanoseconds`, and read it back from there in `StepNavigation`. Alternatively, expose `SceneRuntime::StepNanoseconds()`.
- **Verification:** A test opens a `GameSession` with a 1/30 s step and checks an agent's displacement per step.
- **Risks / trade-offs:** None of note.

ARCH-010 is Info and needs no remediation beyond tracking the listed gaps in the milestone plan.
