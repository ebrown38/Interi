# Animation, navigation, localization and AI (M24)

Four small libraries that games reach through chains and `module.json`. None of them changes the module ABI.

## Animation (`engine/anim`, `interi::anim`)

- **Assets.** `.skeleton.json` (joints parents first, local bind pose) and `.clip.json` (per-joint position, rotation and scale keys). The M21 importers are expected to write these; tests build them in code.
- **Playback.** `SampleClip` (linear for position and scale, nlerp for rotation: no trigonometry, bit-identical everywhere), `BlendPoses`, `ComputeModelPose` and `ComputeSkinMatrices` (model pose times the inverse bind pose).
- **States.** An `AnimationSet` is a skeleton, clips and an `.animstates.json` graph (states, `*` transitions, triggers, crossfade seconds). Sets are bound to character archetype ids (`AnimationLibrary::BindArchetype`); `MakeAnimator` returns the `AnimatorComponent` for an entity of that archetype. A module ships sets in `animationSets`; `LoadModuleAnimations` reads them.
- **Chains.** `anim.Play(target, state, blend)` and `anim.Trigger(target, trigger)` only record a request on the `AnimatorComponent`; the game calls `StepAnimator(library, animator, dt, pose)` on the fixed step, which applies the request and returns the local pose. The last request of a step wins.

## Navigation (`engine/nav`, `interi::nav`)

- **Bake and query.** `NavMesh::Bake(vertices, indices, settings)` voxelizes with Recast; `FindPath` and `NearestPoint` use Detour with a query object per call, so any number of threads may query one mesh. Recast and Detour (zlib) are private dependencies. Cook a mesh to `.inav` (`Write`/`Read`, payload hash) and ship that: the float math in Recast is reproducible on one toolchain, not across platforms.
- **Agents.** `NavAgentComponent` (destination, speed, state). `StepNavAgents(world, mesh, dt)` walks every moving agent along its straight path, re-planning each step, in stable-id order; `RaiseNavTriggers` raises `nav.Arrived` / `nav.Failed` for the chains.
- **Chains.** `nav.FindPath(from, to)` -> `Found {distance, next}` | `NotFound`, `nav.MoveTo(target, destination, speed)`, `nav.Stop(target)`. They read the mesh from `ModuleHost::Navigation()` (a `NavSource`); set it between steps.

## AI behaviours (`engine/sdk/Ai.h`)

`module.json` declares `behaviours` (name, description, optional `chain`) and `computerPlayers` (name, behaviour). `AiCatalog::Build(graph)` lists them with full ids (`<module>:<name>`) and rejects duplicates and unknown behaviours. The engine spawns nothing itself: a game module provides the hook `ai.SpawnComputerPlayer` (`SpawnComputerPlayerSignature`: takes a `ComputerPlayerRequest`, returns the stable id of what it created), and `SpawnComputerPlayers(catalog, hooks, playerIds)` calls it once per player with slots 0, 1, 2....

## Localization (`engine/loc`, `interi::loc`)

- **Tables.** `.strings.json`: `{"version": 1, "language": "fr", "strings": {"menu.play": "Jouer"}}`. A module lists them in `localization`; `LoadModuleStrings` adds them as that module's tables. Tables of one language merge and the one added last wins per key, so a higher module or the project retranslates a lower one. `RemoveProvider` drops a module's text when it unloads.
- **Lookup.** The current language, then the fallback (default `en`), then the key itself. `MissingKeys(language)` is the translation to-do list.
- **Switching.** `SetLanguage(code)`, or `ApplyLanguageAction(localization, "language.set:fr")` from a menu button's action (the game saves the choice; the engine does not).
- **HUD.** `{@key}` in widget text (and button labels) is the translation, whose own `{valueId}` placeholders are filled from Values. A `{@key}` inside a translation is not expanded again. `BuildFrame(..., localization)` and `HudController::SetLocalization` take the `Localization`; without one the key is shown. `hud::LocalizationKeys(document)` lists the keys a document uses; `BoundValues` ignores them. The HUD file format is unchanged (still version 1).
