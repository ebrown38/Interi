## With a project pin but no module declaring physicsEngines, is the pinned engine still selected?
- **Task:** Milestone 17, Physics Engine selection
- **Context:** `SelectPhysicsEngine` in engine/sdk/src/ModuleGraph.cpp; plan 17.3 only says "NoDeclaringModules_NoEngine" without a pin.
- **Assumption made:** A pin selects its engine even when nobody declares engines (the project asked for it); it sits at the bottom of the priority list.

## Where does CanAddModule insert the candidate in the project's module order?
- **Task:** Milestone 17, 17.4 Installing and enabling
- **Context:** `CanAddModule` in engine/sdk/include/interi/sdk/ModuleGraph.h; the plan doesn't say top or bottom, and priority affects engine candidate order.
- **Assumption made:** An `insertAt` parameter, defaulting to the top (highest priority) like a freshly enabled module.

## Which base version does an extender's chain use when several are installed?
- **Task:** Milestone 17, 17.3 selection
- **Context:** Extender declares `extends: {id, version-range}`; several versions of the base may be installed.
- **Assumption made:** Try base versions highest to lowest and take the first chain that satisfies every declaring module (and pin range).

## What are the concrete service.Physics* contract ids?
- **Task:** Milestone 17, 17.5/17.7 contracts and scaffolding
- **Context:** The plan names only service.PhysicsQueries (M19.3.1 declares it). A scaffolded PhysicsEngine needs something to provide.
- **Assumption made:** The prefix `service.Physics` is reserved (`sdk::kPhysicsContractPrefix`). `sdk::PhysicsContracts()` (just `service.PhysicsQueries`, a placeholder hook signature) is declared by every ModuleHost so a scaffolded engine has something to provide; M19 defines the real contracts.

## May a PhysicsEngine module have its own `dependencies`?
- **Task:** Milestone 17, 17.3 selection
- **Context:** Dependencies are resolved before the engine is selected, so an engine's dependencies would be skipped.
- **Assumption made:** Rejected at parse time for M17 (a PhysicsEngine declares no dependencies); revisit with M19.

## Where does the HUD (and the engine's other new modules) live: one `interi::hud` module, and is `game` (GameSession) in scope?
- **Task:** Milestone 18, 18.4 HUD / UI and 18.6/18.7 (starter sample, editor hosting modules)
- **Context:** The plan names no module for the HUD and no "game loop" owner. engine/hud and engine/game are new; GameSession is the glue the starter sample's headless validation and 18.7 both need (`SceneRuntimeDesc::world` lets module systems share the scene's World).
- **Assumption made:** `interi::hud` (SHARED, no rendering) and `interi::game` (static) are new engine modules; the HUD draws through ImGui in the editor only, a game brings its own renderer for the draw list.

## What is an editor "render feature" (18.7.2)?
- **Task:** Milestone 18, 18.7.2 editor_ui registration hooks
- **Context:** The plan lists panels, viewport overlays, tools and render features. Modules cannot reach the forward renderer's passes from the editor (no stable interface), and nothing defined "render feature".
- **Assumption made:** a render feature contributes world-space debug geometry (`DebugDraw` lines and boxes) the viewport draws each frame. Real renderer passes can follow when a module needs them.

## How do modules contribute shared `input.*` actions (18.1.1)?
- **Task:** Milestone 18, 18.1 Input
- **Context:** The plan says "shared `input.*` actions contributed by modules" but no manifest section or registry exists for input.
- **Assumption made:** modules ship input config files (`ApplyInputConfig` with `InputMerge::Add`) and the project's or player's config applies on top with `Replace`; no new manifest section. A `module.json` "input" section can follow if wanted.

## Is the volume test for triggers O(volumes x actors) acceptable?
- **Task:** Milestone 18, 18.3 Triggers
- **Context:** `OverlapTracker` compares every volume with every actor each step; fine for the simple games this targets, quadratic for thousands of both.
- **Assumption made:** acceptable for M18; a sort-and-sweep or grid can replace the inner loop later without changing the event order.

## How do editor extension modules get ImGui?
- **Task:** Milestone 18, 18.7.2 editor_ui registration hooks
- **Context:** The extension API is abstract so a module links no editor symbols, but a panel draws with ImGui and a module library has its own ImGui copy; it must adopt the editor's context (`PanelContext`). The installed `Interi` package does not export imgui, and the editor's vcpkg imgui is not a shared library.
- **Assumption made:** M18 ships the API, the context hand-off and a test module that uses only the ImGui-free parts (overlays, tools, render features, and a panel that never calls ImGui). How a third-party module obtains imgui (package export, or editor_ui becoming shared) is left for M20 when the first real editor extension module needs it.

## Do the Jolt module's layers become generic now (19.4)?
- **Task:** Milestone 19, 19.4 Manifest contributions (Layers data table)
- **Context:** `PhysicsLayers.h` hard-codes Static, Structure, Debris, DebrisSmall, Character, Vehicle and Projectile, and the copied layer, world, sync, character and determinism tests (and the golden hash) depend on them. Making layers table-driven changes the golden hash inputs and must move together with Destruction's own rows (M20.3).
- **Assumption made:** M19 copies the layer enum faithfully so the golden hash is unchanged. The module reads the `Layers` and `PhysicsMaterials` tables (see the 19.4 entry below); a fully table-driven layer set lands with M20.3.

## Which engine file does the TSan suppression mechanism change (19.2.5)?
- **Task:** Milestone 19, 19.2.5 sanitizer suppressions
- **Context:** The engine's own `interi::physics_testing` stays until M20.5, and `destruction_render` tests need Jolt + lavapipe suppressions in one list.
- **Assumption made:** `VulkanTestSupport.cpp` now takes any extra suppression from an `INTERI_EXTRA_TSAN_SUPPRESSIONS` string definition (set by `interi::physics_testing` and by the module's helper), so the engine names no Jolt text; a test in `interi_destruction_render_gpu_tests` checks both sets are present under TSan.

## Static vs shared physics library and the Windows shadow-copy open item
- **Task:** Milestone 19, library layout
- **Context:** M13 left "Windows shadow copies vs link-time imports" open (solve before M19). A module linked against another module's DLL won't bind to its shadow-renamed copy. The Jolt module's code (world, character, Jolt itself) must not be duplicated per consumer, or a process gets two `JoltRuntime`s.
- **Assumption made:** the physics code is one shared library (`interi_jolt_physics`, a normal dependency found by the loader, not shadow-copied) and the `PhysicsEngine` entry library (`com_interi_physics_jolt`, shadow-copied) only registers hooks, events and Values, reaching the shared library's `JoltEngine` singleton. Destruction (M20) links the shared library; it is not hot reloaded. Only Linux was exercised; the Windows case (Jolt statics duplicated in consumers linking `Jolt::Jolt` statically) is untested until CI has the module, which it cannot (the module is gitignored).

## What does OnOverlap mean for the Jolt engine?
- **Task:** Milestone 19, 19.4 Events
- **Context:** The plan lists OnContact, OnOverlap, ApplyImpulse, Raycast; the scene runtime already raises `trigger.Overlap` for physics-free volumes.
- **Assumption made:** `OnContact` is raised by `RaiseContactTriggers` from the step's contact list; `OnOverlap` is raised by `SensorSystem` for entities with a `SensorComponent` (sphere overlap queries, begin events only), not by Jolt sensor bodies.

## How does the Jolt module contribute "rigid body and character controller components" (19.4)?
- **Task:** Milestone 19, 19.4 Manifest contributions (Physics rules)
- **Context:** `ModuleRegistrar` has no reflection registration, so a module cannot register reflection itself.
- **Assumption made:** the physics components now have `kComponentName`s (`com.interi.physics.RigidBody`, `.Collider`, `.Body`) and the module's library offers `RegisterPhysicsReflection(ReflectionRegistry&)` (rigid body numeric fields; motion type and layer are layer-table enums and are not reflected) for a host such as the editor to call; the character controller is the `CharacterComponent` driven by `CharacterSync`, and `SensorComponent`/`SensorSystem` wire `OnOverlap`; `hook.physics.*` has BodyBudget, ContactImpulseScale, CharacterMove and OverlapFilter. Body budget: the `bodyBudget` Value plus `hook.physics.BodyBudget` feed `MakeWorldDesc` (`maxBodies`), and `PhysicsSync` creates no body past it. `PhysicsMaterials` rows apply through `ApplyPhysicsMaterial`; `Layers` rows are table-driven: a row named like a built-in layer overrides its collisions, any other row defines a new layer (`PhysicsWorldDesc::extraLayers`, up to 32 layers in all). The built-in seven remain the defaults (golden hash unchanged); M20.3 moves Destruction's layers into rows and may then drop the built-ins.

## Should the Destruction Module's Health and Armor be the shared ids `attr.Health` / `attr.Armor`?
- **Task:** Milestone 20, 20.3 Contributions (Character: `attr.Health`, `attr.Armor`)
- **Context:** Shared ids (no `:`) must be declared by the engine (`ModuleHostDesc::engineContracts`) or an Extension's `contracts`; neither declares `attr.*`. `modules/destruction/module.json` therefore declares them as local attributes (`com.interi.destruction:Health`, `:Armor`) in an attribute set `Vitals` and an archetype `Survivor`.
- **Assumption made:** local ids for now. Making them shared needs a contract owner (an Extension, or engine contracts); other modules can still name them by full id or override them.

## How does a destructible or a spawn point appear in a scene once the engine has no level bridge?
- **Task:** Milestone 20, 20.1.5 Editor extensions (LevelBridge)
- **Context:** Module components must be trivially copyable, and the old `DestructiblePlacementComponent` held a `std::string`. The editor extension's `LevelBridge` (`modules/destruction/libs/destruction_editor`) imports a level as plain scene objects instead.
- **Assumption made:** a destructible is an object with the engine's `MeshComponent` whose asset is the cooked `.idest` (the editor does not draw such a mesh; the module's render feature does), and a spawn point is an object named `Spawn <name>`. `LevelToScene` converts a level file to scene v2, replacing the editor's legacy-level import; File > Export level is gone (play and replays export from the open scene).

## Do editor render features need particles?
- **Task:** Milestone 20, 20.2.1 render-feature hook ("particle and decal access")
- **Context:** The editor never drew GPU particles (no `GpuParticleSystem` in `EditorRenderer`), so there was nothing to move. The hook (`RenderFeatureDef::render`) gives features `RenderObject`s and `Decal`s plus uploads into the editor's `MeshPool`.
- **Assumption made:** decals and objects only. A feature that wants particles needs the editor to own a `GpuParticleSystem`; that can be added to `RenderFeatureOutput` without changing existing features.

## How does an editor extension module get Dear ImGui?
- **Task:** Milestone 20, 20.2.2 (open question from M18, "How do editor extension modules get ImGui?")
- **Context:** The installed package does not export ImGui and the editor's ImGui is static.
- **Assumption made:** the Destruction Module links its own ImGui (vcpkg `imgui`, same baseline as the engine, docking-experimental feature) into its entry library and adopts the editor's context (`PanelContext`). Its panel code is the old `FracturePanel`/`StructurePanel` contents, drawn between the editor's `Begin`/`End`. The ImGui used by a module and by the editor must stay the same version.

## Where do the sandbox's weapon cooldown and weapon selection live?
- **Task:** Milestone 20, 20.4 Sandbox as a project ("hard-coded sandbox gameplay and bindings become Values and Chains")
- **Context:** Bindings were already data (`data/gameplay/input.json`). What was hard-coded is what an action does: `modules/destruction/projects/sandbox/content/chains/weapons.chain.json` now maps `weapon.swing|charge|detonate|rocket` input actions to the module's `Swing`/`PlaceCharge`/`Detonate`/`FireRocket` events, reading the aim from the Values `aimOrigin`/`aimDirection`.
- **Assumption made:** the sandbox's C++ keeps the weapon cooldown, the 1/2/3 weapon selection and the truck/character code; only the weapon effects went through chains. The sandbox uses the module host directly (not `GameSession`, which is not in the installed package). The project is staged into the build tree (`<build>/sandbox_project`), because a project's module folders must lie inside it.

## How do the Destruction and Jolt modules build in the engine tree after the engine drops Jolt?
- **Task:** Milestone 20, 20.5 Engine removal
- **Context:** In-tree builds of `modules/*` need `find_package(Jolt)`, and the engine's vcpkg manifest no longer lists it after M20.5.
- **Assumption made:** the root CMake skips a module whose dependencies are missing (`modules/physics`, `modules/destruction` return early when Jolt is not found); the supported path is the standalone build against an installed engine (`cmake --install`, then `find_package(Interi COMPONENTS Vulkan)`), with the module's own `vcpkg.json` and the overlay port in `modules/physics/ports`. Destruction's standalone build adds the sibling `../physics` folder (`INTERI_PHYSICS_MODULE_DIR`), because the Jolt module has no install rules.

## M21 (asset import) is not on this branch; build M22 without it?
- **Task:** Milestone 22 (Chain editor)
- **Context:** The branch is based on M20; M21 lives on autoflow/003-goal-implement-milestone-21-8367f0 and is unmerged. M22 depends nominally on M21 but needs nothing from it.
- **Assumption made:** M22 is implemented on this base without M21 (no asset browser v2 use).

## Which canvas library for the chain editor?
- **Task:** 22.1.1 node canvas
- **Context:** The plan says imgui-node-editor or imnodes after a license check; the engine uses imgui 1.92 with the new texture API.
- **Assumption made:** The canvas (pan, zoom, minimap, links) is drawn directly on ImDrawList like DrawHudFrame, avoiding a dependency, license-policy and notices changes.

## What does "trusted" key on, and are projects that already list modules confirmed?
- **Task:** Milestone 23, 23.4 Trust UI
- **Context:** The plan says the panel "shows the author and license and asks for confirmation the first time a module is enabled." `engine/editor/ModuleManager.h`, `docs/module-manager.md`.
- **Assumption made:** Trust is keyed by id + author + license (a changed author or license asks again) and stored in `trusted_modules.json` beside the editor preferences. Only enabling asks (Module Manager and the Project menu's Add module, covering implicit Extensions and a newly selected Physics Engine); opening a project that already lists modules does not, and the docs say so.

## Where does the module docs generator live, and does it cover modules that aren't loaded?
- **Task:** Milestone 23, 23.5 Module docs generator ("from a manifest")
- **Context:** Events and hooks are registered by a module's code, so they exist only in a loaded host's registries; Values and Tables are in module.json.
- **Assumption made:** `hotreload::GenerateModuleDocs` takes the manifest plus optional registries, and `interi_module_check --docs <dir>` loads the modules itself so command-line docs include Events and hooks. The editor's Docs tab covers modules loaded in the open project. A module that can't load only gets its manifest, Values and Tables.

## Which module libraries does the editor reload in the hot-reload loop?
- **Task:** Milestone 23, 23.3.2 Hot-reload loop
- **Context:** `EditorApp::RunModuleLoop` reloads each loaded module that has a library, in project manifest order (`Reload` also reopens dependents), rather than only the one that was rebuilt, because the watcher only reports that some library changed.
- **Assumption made:** Reloading all is correct but redundant for large projects; a per-library watcher could narrow it.

## M21 (importers, skeletal data) is not on this branch; where do skeletons and clips come from?
- **Task:** Milestone 24, 24.1.1 skeletal playback "for the M21 clips"
- **Context:** `git log` on this base goes M20, M22, M23; M21 lives unmerged on `autoflow/003-goal-implement-milestone-21-8367f0`. `engine/anim` therefore defines its own minimal `Skeleton`/`Clip` JSON assets (`Animation.h`).
- **Assumption made:** An importer (M21's glTF/FBX skins) is expected to produce these two formats later; nothing in M24 depends on M21 code. Tests use procedural clips.

## Which animation runtime: ozz-animation or in-house?
- **Task:** Milestone 24, 24.1.1
- **Context:** Plan says "decided by license and size".
- **Assumption made:** In-house (about 400 lines, no dependency or notices change, nlerp instead of slerp so results are bit-identical across platforms). ozz can replace `SampleClip` later behind the same API.

## What is "Management" in "computer-player spawning hooks for Management"?
- **Task:** Milestone 24, 24.3 AI section support
- **Context:** The plan mentions a Management game/module that isn't defined anywhere in the repo.
- **Assumption made:** A generic engine hook `ai.SpawnComputerPlayer` (`sdk/Ai.h`) that any game module provides; the engine only collects `behaviours` and `computerPlayers` from manifests (`AiCatalog`) and calls the hook per requested player.

## Is "animation states per archetype" about values-module character archetypes or ECS archetypes?
- **Task:** Milestone 24, 24.1.2
- **Context:** `animationSets` in module.json list `archetypes` ids.
- **Assumption made:** Character archetype ids of the values module (`<module>:<name>`); `AnimationLibrary::BindArchetype` maps them to a set and `MakeAnimator` creates the component. Nothing attaches the component automatically at spawn yet: the game does it.

## Localization of HUD text: how are Values and translations separated?
- **Task:** Milestone 24, 24.4
- **Context:** HUD text uses `{valueId}`; ids contain `:`.
- **Assumption made:** See the localization phase: `{@key}` references a string-table key (keys have no `:`/`|` conflict because `@` starts the token).

## Where does a project keep its cooked navmesh, and who adds NavAgentComponent / AnimatorComponent to entities?
- **Task:** Milestone 24, 24.2 / 24.1.2 wiring into `GameSession`
- **Context:** The plan gives no project-level format for the cooked `.inav`, and no spawn-time hook that attaches the components.
- **Assumption made:** `GameDesc::navMesh` defaults to `content/navmesh.inav`; the game (or a module) adds `NavAgentComponent` / `AnimatorComponent` itself. Scene-file reflection and editor UI for both components, and a navmesh bake step in the editor/cook, are not done in M24.
