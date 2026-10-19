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
- **Assumption made:** `OnContact` is raised by `RaiseContactTriggers` from the step's contact list; `OnOverlap` is declared (same payload) for a game to raise from `OverlapSphere` results, since Jolt sensors are not wired. 

## How does the Jolt module contribute "rigid body and character controller components" (19.4)?
- **Task:** Milestone 19, 19.4 Manifest contributions (Physics rules)
- **Context:** `ModuleRegistrar` has no reflection registration, so a module cannot register reflection itself.
- **Assumption made:** the physics components now have `kComponentName`s (`com.interi.physics.RigidBody`, `.Collider`, `.Body`) and the module's library offers `RegisterPhysicsReflection(ReflectionRegistry&)` (rigid body numeric fields; motion type and layer are layer-table enums and are not reflected) for a host such as the editor to call; the character controller is a library class, not a component. Body budget: the `bodyBudget` Value plus `hook.physics.BodyBudget` feed `MakeWorldDesc` (`maxBodies`), and `PhysicsSync` creates no body past it. `PhysicsMaterials` rows apply through `ApplyPhysicsMaterial`; `Layers` rows override the built-in collision matrix for rows named like a built-in layer (`PhysicsWorldDesc::layerRules`); new layer names (and Destruction's rows) need a table-driven layer enum, which M20.3 does.
