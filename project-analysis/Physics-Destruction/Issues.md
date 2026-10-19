# Physics-Destruction — Issues

**Project:** Interi (`/home/eric/CLionProjects/Interi`), plus the local module repositories `modules/physics` (Jolt Physics Engine, `com.interi.physics.jolt` 5.0.0) and `modules/destruction` (Destruction Module, `com.interi.destruction` 1.0.0)
**Analyzed:** 2026-10-19
**Scope:**
- **Engine (this repo):** the physics contract and selection code (`engine/sdk` `PhysicsQueries.h`, `ModuleGraph`, `ContributionRegistry`; `engine/hotreload` `ModuleHost`, `ModuleApi.h`), the game loop (`engine/game/GameSession`), the physics-free triggers (`engine/runtime/TriggerVolumes`, `engine/scene` triggers), `docs/writing-a-physics-engine.md`, and the physics tests in `tests/sdk` and `tests/hotreload`.
- **Module `modules/physics` (separate git repo, not in the GitNexus index):** `include/`, `src/` (13 files), `src/module/JoltEngineModule.cpp`, `module.json`, `CLAUDE.md`, and the test list.
- **Module `modules/destruction` (separate git repo, not in the GitNexus index):** `libs/destruction` (stress, damage, islands, debris pool), `libs/destruction_runtime` (`DestructionWorld`, `DebrisSystem`), `libs/gameplay` (weapons, saves, the engine attachment), `libs/destruction_editor` (`PlaySession`), `src/module/DestructionModule.cpp`, `game/sandbox/main.cpp`, `module.json`, `CLAUDE.md`, and the test list.
- About 35k lines of module code were inventoried. The vendored Jolt sources (`external/vcpkg/buildtrees/joltphysics`) were not reviewed.

**Summary:** 17 issues — Critical: 0 · High: 5 · Medium: 10 · Low: 1 · Info: 1

Each **Location** says whether the code is in the **engine** (this repo) or in a **module** (a separate local repo under `modules/`).

## Findings

### PHYS-001 — The Physics Engine contract has no world lifecycle or step phase, so projects that select Jolt never simulate it
- **Severity:** High
- **Confidence:** High
- **Category:** Solver integration / engine physics contract
- **Location:** engine `engine/game/src/GameSession.cpp:90-95`, `engine/hotreload/include/interi/hotreload/ModuleApi.h:92`, `docs/writing-a-physics-engine.md:1-60`; module `modules/physics/src/module/JoltEngineModule.cpp:150-198`, `modules/physics/include/interi/physics/Engine.h:60-67`; module `modules/destruction/game/sandbox/main.cpp:749-777`
- **Evidence:**
  - `GameSession::Step()` is `RunSystems(...)`, then `m_scene->Step()`, then `StepNavigation()`. GitNexus `cypher` confirms that `GameSession::Step` calls nothing else. Nothing in the engine creates, steps or attaches a physics world.
  - A module can add only ECS systems (`ModuleRegistrar::AddSystem`, `ModuleApi.h:92`), and systems may not make structural changes. `PhysicsSync::Step` and `DestructionWorld::Update` create and destroy entities, so they are sync points and cannot run as module systems.
  - The Jolt module's `Register` declares hooks and chain events only. The world exists only after a game calls `JoltEngine::Get().Attach(world)`; until then `service.PhysicsQueries` returns `nullptr`.
  - The only working loop is the hand-written sandbox: chains, module systems, `physicsSync.Step`, `vehicle->Update`, `player.Update`, `weaponSystem.Update`, `destruction.Update`, `RaiseDestructionTriggers`, `debrisSystem.Update`, then audio. Each of these has its own ordering rule documented in a module CLAUDE.md.
  - `docs/writing-a-physics-engine.md` covers selection and queries but says nothing about who owns or steps the world.
- **Impact:**
  - A project that lists `com.interi.destruction` resolves, selects Jolt and loads both libraries, yet under `GameSession` or play-in-editor nothing falls, breaks or collides, and weapon chains silently do nothing (`DestructionEngine::Current()` is empty).
  - Every game must re-create an order-sensitive, roughly 10-call loop. A wrong order breaks the "after PhysicsSync::Step" rules, which no code enforces.

### PHYS-002 — The Jolt module's data-driven rules (step rate, gravity, body budget, Layers, PhysicsMaterials) are never applied outside tests
- **Severity:** High
- **Confidence:** High
- **Category:** Solver integration / configuration
- **Location:** module `modules/physics/include/interi/physics/PhysicsRules.h:43-53`, `modules/physics/module.json` (Values `gravity`, `stepRate`, `bodyBudget`; tables `Layers`, `PhysicsMaterials`); module `modules/destruction/game/sandbox/main.cpp:375-378`, `modules/destruction/libs/destruction_editor/src/PlaySession.cpp:16-21`, `.../PlaySession.h:33`
- **Evidence:**
  - Outside `tests/`, nothing calls `ReadPhysicsRules`, `MakeWorldDesc` or `ApplyPhysicsMaterial` (grep across both modules).
  - The sandbox builds `interi::physics::PhysicsWorld physics(jobs);` with the default desc at line 375, before the project's modules load at line 412, and steps with a default `FixedStepClock` (60 Hz).
  - The editor's `PlaySession` likewise uses a default `PhysicsWorld`, a hard-coded `kStep = 1.0f / 60.0f` and a default-constructed `DestructionWorld` (`m_destruction(m_physics)`), so it ignores the Destruction module's rules as well.
  - `PhysicsRules::stepRate` is read but never used anywhere.
- **Impact:**
  - Designers can edit `stepRate`, `gravity`, `bodyBudget`, the `Layers` rows (including the destruction layer rows added in module commit `205c183`) and `PhysicsMaterials`, and see them in the editor's data panels, but the game never applies them.
  - `hook.physics.BodyBudget` overrides from higher modules have no effect.
  - The editor's play preview simulates different thresholds from the game.

### PHYS-003 — Contact and explosion damage visit every bond of every instance, with no spatial culling and no per-frame cap
- **Severity:** High
- **Confidence:** High (code path); Medium (frame-time magnitude, not measured)
- **Category:** Damage model / per-frame budgets
- **Location:** module `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:404-420` (`ApplyDamage`), `:437-472` (`ApplyContactDamage`), `:751-757` (queued damage); `modules/destruction/libs/destruction/src/Damage.cpp:48-79`
- **Evidence:**
  - For each `ContactEvent` whose impulse exceeds the threshold, `ApplyContactDamage` calls `ApplyDamage` on the whole instance.
  - `ApplyDamage` allocates `std::vector<Transform> poses(instance.pieces.size())` and `std::vector<Vec3> centroids(bondCount)`, transforms every bond centroid, then `ApplyRadialDamage` scans every bond.
  - Each queued explosion or melee hit runs the same work against every live instance (`for (... instance : m_instances) ApplyDamage(...)`). No bounds test rejects instances that are far from the damage centre.
  - None of this counts against `stressWorkBudget`, and the number of contacts per step is not capped.
- **Impact:** cost per step is roughly (contacts + blasts) × total bonds, plus two heap allocations per call. A collapse produces a burst of high-impulse contacts exactly when the frame is already busiest, so large buildings or many destructibles in a level risk hard hitches. The skill guidance ranks unbounded break work as top severity. It is High here because the impulse threshold (2000 N·s) and Jolt's contact limits bound the count somewhat.

### PHYS-004 — Bodies can float: rubble becomes permanently static, and removing a supporting piece never wakes sleeping bodies on it
- **Severity:** High
- **Confidence:** High (rubble); Medium (sleeping bodies; this relies on Jolt not waking bodies that touched a removed body)
- **Category:** Structural integrity / debris lifecycle
- **Location:** module `modules/destruction/libs/destruction_runtime/src/DebrisSystem.cpp:294-305`, `modules/destruction/libs/destruction/src/Debris.cpp:49-52`, `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:604-616`, `modules/physics/src/PhysicsSync.cpp:109-131`
- **Evidence:**
  - `ChooseDebrisAction` returns `Settle` for any sleeping debris body. `MergeRubble` then replaces each cluster with `body.motion = physics::MotionType::Static` on `rubbleLayer` (default `ObjectLayer::Static`).
  - Nothing ever turns rubble dynamic again, despawns it or re-checks what it rests on. Rubble that settles on an intact floor, or on a sleeping falling piece, stays in mid-air after that support collapses.
  - When pieces are rebuilt, `RebuildPieces` destroys the replaced entities (`world.DestroyEntity(piece.entity)`), and `PhysicsSync::DestroyRemovedBodies` removes their bodies. No code calls `ActivateBodiesInAABox` or wakes bodies near a removed piece: the only activation call in the module is `Vehicle.cpp:185`. Jolt does not automatically wake bodies that were resting on a removed body.
  - No test covers a sleeping body on a support that is later removed. The `RubbleMerge_*` tests use only a flat floor.
- **Impact:** debris and pieces hang in the air after the part below them breaks: the classic destruction bug, and very visible in an RFG-style game. Static rubble can also block routes permanently. It is never cleaned up, and the navigation mesh does not know about it (see PHYS-015).

### PHYS-005 — Large falling pieces and rubble piles are outside every budget and accumulate for the whole session
- **Severity:** High
- **Confidence:** High
- **Category:** Debris lifecycle and budgets
- **Location:** module `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:685-702`, `modules/destruction/libs/destruction_runtime/include/interi/destruction_runtime/DestructionWorld.h:85,138-142`, `modules/destruction/libs/destruction_runtime/include/interi/destruction_runtime/DebrisSystem.h:88-99`, `modules/destruction/libs/destruction_runtime/src/DebrisSystem.cpp:294-305`, `modules/destruction/libs/destruction_editor/src/PlaySession.cpp:16-21`
- **Evidence:**
  - Only pieces with `volume <= smallPieceVolume` (0.05 m^3) are handed to the `DebrisSystem`. Larger falling pieces stay with their instance as dynamic bodies until the whole instance is despawned.
  - The `DebrisSystem` caps (`maxBodies` 2048, `maxParticles` 16384) count only adopted debris. Rubble piles are created as plain entities (`world.CreateEntity(origin, body, ...)`), never put in a pool, and never removed.
  - Large pieces never fade, freeze, merge or despawn, and nothing culls them by distance.
  - The handoff is opt-in (`SetDebrisSystem`). The editor's `PlaySession` never sets it, so every piece there is unbudgeted.
  - Jolt's `maxBodies` is 65536, and on overflow `PhysicsSync` logs an error and leaves entities without bodies (see PHYS-007).
- **Impact:** body count, broadphase size and the number of solver islands grow with every collapse during a play session, with no per-platform ceiling, which matters for fixed-memory consoles. Over a long session, step time and memory increase steadily until bodies silently fail to be created.

### PHYS-006 — The `service.PhysicsQueries` contract is too thin for physics-agnostic modules
- **Severity:** Medium
- **Confidence:** High
- **Category:** Engine physics contract
- **Location:** engine `engine/sdk/include/interi/sdk/PhysicsQueries.h:22-35,46-58`; module `modules/physics/src/Engine.cpp:14-27`
- **Evidence:**
  - `PhysicsLayerMask` is "bit i = the engine's layer i", but the contract has no way to look up a layer by name. The only portable mask is `kAllPhysicsLayers`. The Jolt module's layer ids depend on its built-in enum and on the project's `Layers` table rows.
  - `PhysicsHit::object` is "the entity's handle bits". It is not a `StableIdComponent` id, and decoding it uses `EntityFromBodyUserData` from the Jolt module.
  - The contract offers no multi-hit raycast, no shape sweep, no batched queries, no impulse or force API, no contact-event subscription and no step-phase information. The requirement "not while the engine steps" cannot be checked by the caller.
- **Impact:** a physics-agnostic module (a game mode, AI or an alternative destruction module) cannot select layers, push bodies or react to impacts without naming an engine. That defeats the engine/module boundary that `docs/writing-a-physics-engine.md` sets out, and makes a second Physics Engine hard to swap in.

### PHYS-007 — A Jolt step overflow is only asserted, and running out of bodies logs every step
- **Severity:** Medium
- **Confidence:** High
- **Category:** Solver robustness
- **Location:** module `modules/physics/src/PhysicsWorld.cpp:31-33`, `modules/physics/include/interi/physics/PhysicsWorld.h:28-32`, `modules/physics/src/PhysicsSync.cpp:151-157`
- **Evidence:**
  - `const JPH::EPhysicsUpdateError error = m_system.Update(...); INTERI_ASSERT(error == JPH::EPhysicsUpdateError::None, ...); (void)error;`
  - With `INTERI_SHIPPING` the assert is compiled out, so an overflow of manifolds, body pairs or contact constraints is silently ignored. Jolt then drops the contacts it could not store, and bodies pass through each other or the floor.
  - The defaults (`maxBodyPairs` 65536, `maxContactConstraints` 32768) are fixed numbers that do not scale with `maxBodies` (65536) or with the destruction budgets. The step does not count or record overflows.
  - When `CreateBody` returns `nullptr`, `CreateBodies` logs an error and returns from that chunk only. The entity stays without a `PhysicsBodyComponent` and is retried, and logged again, on every following step.
- **Impact:** large collapses, the case most likely to reach these limits, make bodies fall through geometry in shipping builds with no telemetry, and the body-cap path floods the log.

### PHYS-008 — Contact and break events reach Chain Reaction one per contact and one per bond, with no aggregation
- **Severity:** Medium
- **Confidence:** High
- **Category:** Gameplay coupling / event storms
- **Location:** module `modules/physics/src/Engine.cpp:95-118`, `modules/physics/include/interi/physics/PhysicsWorld.h:35-36`; module `modules/destruction/libs/gameplay/src/DestructionEngine.cpp:36-48`
- **Evidence:**
  - `RaiseContactTriggers` raises `OnContact` twice per contact.
  - `minContactImpulse` defaults to `0.0f`, so even resting contacts between new pieces produce events.
  - `RaiseDestructionTriggers` raises one `BondBroken` trigger for every entry in `BreakEvents()`. One collapse round can break hundreds or thousands of bonds.
  - Audio has an `ImpactBatcher` (one voice per bank and cell), but the chain path has no equivalent.
- **Impact:** a large collapse floods the `ChainExecutor`. It truncates link activations to its per-step budget and drops the rest, so gameplay logic that relies on these triggers (scoring, objectives, AI alerts) loses events unpredictably, and evaluation cost rises in the busiest frames.

### PHYS-009 — Every break round relabels the whole graph and rebuilds the full static compound, outside the stress budget
- **Severity:** Medium
- **Confidence:** High (code); Medium (cost)
- **Category:** Structural integrity / per-frame budgets
- **Location:** module `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:498,523-533,655,774-786,827-836`; `modules/physics/src/Components.cpp:68-80`
- **Evidence:**
  - `Update` runs up to `maxBreakRounds` (default 4) rounds and calls `RebuildPieces` for each dirty instance in every round, plus once more at the end.
  - Each call runs `LabelComponents` over all chunks and `GroupIslands`.
  - When the set of anchored chunks changes (`staticChanged`), the call rebuilds a `StaticCompoundShape` from every hull of every anchored chunk and creates a new static entity, even when one leaf chunk fell off a large building.
  - Entities created in round *n* can be destroyed again in round *n+1* of the same Update.
  - `stressWorkBudget` limits only CG iterations.
- **Impact:** for large structures, each break costs O(chunks + hulls·log hulls) up to five times per step, plus a broadphase remove and add of a huge static body. This is a hitch source that the solver budget does not cover.

### PHYS-010 — Stress breaking waits for full convergence, so starved or stalled solves leave overloaded structures standing indefinitely
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Structural integrity
- **Location:** module `modules/destruction/libs/destruction/src/StressBudget.cpp:19-41`, `modules/destruction/libs/destruction/include/interi/destruction/StressBudget.h:31-35`, `modules/destruction/libs/destruction/src/StressSolver.cpp:527-557`, `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:798`
- **Evidence:**
  - `SolveWithBudget` gives the most important unsolved task all of the remaining budget (`iterations = remaining / cost`). Every later task is skipped once `remaining == 0`.
  - Importance is `1/(1+age since last damage)`. Under sustained damage elsewhere, an older weakened structure may never receive iterations.
  - `Solve` exits early without converging when `curvature <= 0` (or NaN), and `DestructionWorld` checks for overloads only when `IsConverged()` is true. Nothing gives up, falls back to a partial-solution check, or reports a structure stuck unconverged.
  - The header's claim "so a structure always converges eventually" holds only when no other instance takes the budget.
- **Impact:** a support can be shot away while the structure above stays up, because its solve is starved or numerically stalled. Players see this as structures that don't collapse when they should.

### PHYS-011 — Explosion damage depends on fracture resolution and ignores occlusion
- **Severity:** Medium
- **Confidence:** High
- **Category:** Damage model
- **Location:** module `modules/destruction/libs/destruction/src/Damage.cpp:12-23,48-79`
- **Evidence:**
  - A bond's health is `min(fractureEnergy) * bond.area`.
  - `ApplyRadialDamage` subtracts the full `energy * RadialFalloff(distance)` from every bond inside the radius. Energy is not divided by the number of bonds or weighted by bond area, and there is no line-of-sight or occlusion test (bonds behind a wall take the same damage).
- **Impact:** cooking the same building with more Voronoi cells (smaller bonds) makes it far weaker against the same charge, so tuning `materials.json` and weapon `energy` is tied to cook settings. Blasts also cut through walls they should be blocked by. Balancing does not carry across assets.

### PHYS-012 — Stress loads are self-weight within one asset only: no external loads and no support across instances
- **Severity:** Medium
- **Confidence:** High
- **Category:** Structural integrity
- **Location:** module `modules/destruction/libs/destruction/include/interi/destruction/StressSolver.h:120` (`SetExternalForce`), `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:155-158`
- **Evidence:**
  - `StressSolver::SetExternalForce` exists, but no runtime code calls it (grep: defined only in `StressSolver.cpp:233`, used only in tests). The load vector is gravity on each chunk's own mass.
  - Anchors come only from the cooked asset (`source.anchors`, leaves touching the cook-time `groundHeight` or anchor meshes). A destructible spawned on top of another destructible, or on terrain at another height, keeps its cooked anchors whatever is actually under it.
- **Impact:**
  - Vehicles, characters and piles of debris on a floor add no stress, so they never cause progressive collapse.
  - A building stacked on a destructible foundation stays standing in mid-air after the foundation is destroyed. Level designers have no warning about either case.

### PHYS-013 — No CCD for destruction pieces or wrecks, and no out-of-bounds cleanup
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Collision / stability
- **Location:** module `modules/physics/include/interi/physics/Components.h:45`, `modules/physics/src/PhysicsSync.cpp:48`, `modules/destruction/libs/destruction_runtime/src/DestructionWorld.cpp:670-684`, `modules/destruction/libs/gameplay/src/Weapons.cpp:184-199`
- **Evidence:**
  - `RigidBodyComponent::continuous` defaults to `false` (Discrete). `RebuildPieces` and the vehicle wreck never set it.
  - Blast impulses are absolute N·s values (`weapon.impulse * falloff`, 3000 N·s for `remote_charge`) and do not scale with body mass. Pieces also get `separationSpeed` plus `explosionPushSpeed`.
  - Nothing in either module removes bodies that fall below the world or leave its bounds. Large pieces are not budgeted (PHYS-005), so they keep simulating forever.
- **Impact:** light pieces just above `smallPieceVolume` can reach tens of m/s and tunnel through thin walls or floors, depending on Jolt's speculative contacts. Anything that leaves the level costs simulation time for the rest of the session.

### PHYS-014 — Save restore matches instances by position in the list, stores no asset identity, and can leave a partial restore
- **Severity:** Medium
- **Confidence:** High
- **Category:** Save/load coupling
- **Location:** module `modules/destruction/libs/gameplay/src/DestructionState.cpp:49-93,95-138`
- **Evidence:**
  - `RestoreDestructionState` pairs `state.instances[i]` with `destruction.Instances()[i]` and checks only the instance count and, through `RestoreDamage`, the bond count.
  - The save records no level object id, asset path or cook key.
  - When instance *k* fails, instances `0..k-1` have already been changed (`INTERI_TRY(destruction.RestoreDamage(...))` inside the loop).
  - Falling pieces missing from the save are left "at rest where the asset is" (line 121), and debris and rubble are not saved (documented in the module CLAUDE.md).
- **Impact:**
  - If a level gains, loses or reorders a destructible, or an asset is re-cooked with the same bond count, old saves apply damage to the wrong buildings without any error.
  - A failed load leaves the world half restored. The engine's `SaveRegistry` reports failures per section, so this section is not atomic.

### PHYS-015 — Destruction never updates navigation
- **Severity:** Medium
- **Confidence:** High
- **Category:** Coupling (navigation)
- **Location:** module `modules/destruction/libs` (no `nav::` reference in either module); engine `engine/hotreload/include/interi/hotreload/ModuleHost.h:134` (`Navigation()` `NavSource`), `engine/game/src/GameSession.cpp:97-100` (cooked `.inav` stepping)
- **Evidence:**
  - Neither module includes or calls anything from `engine/nav`.
  - The engine ships a cooked navmesh (`content/navmesh.inav`) and offers a swappable `NavSource`, but nothing rebuilds tiles, adds obstacles or invalidates paths when walls break, pieces fall or static rubble forms (PHYS-004).
- **Impact:** AI (`nav.MoveTo`, `NavAgentComponent`) walks into fallen rubble and ignores new holes in walls, so destruction cannot open or close routes for agents.

### PHYS-016 — The sandbox renders physics poses without interpolation
- **Severity:** Low
- **Confidence:** High
- **Category:** Render coupling
- **Location:** module `modules/destruction/game/sandbox/main.cpp:657-660,780-860`; engine `engine/runtime/include/interi/runtime/FixedStepClock.h` (`Alpha()`)
- **Evidence:** the loop calls `clock.Advance(...)` and runs whole steps, but never reads `clock.Alpha()`. Debris, pieces and vehicles are drawn from the current `Transform`, with no previous pose kept.
- **Impact:** visible judder whenever the display rate isn't a multiple of 60 Hz (144 Hz monitors, VRR). The sandbox is also the reference integration others will copy.

### PHYS-017 — Strengths worth preserving (no action)
- **Severity:** Info
- **Confidence:** High
- **Category:** Observation
- **Location:** module `modules/physics/CLAUDE.md` (cross-platform deterministic Jolt port, golden hash); module `modules/destruction/libs/destruction/src/StressSolver.cpp` (beam-lattice PCG in double, fixed grains); `modules/destruction/libs/destruction_editor/src/StructureDebug.cpp:28-59` (stress heat map, anchors); `modules/destruction/tests/destruction_runtime/ScenarioTests.cpp`, `.../stress/CollapseDeterminismTests.cpp`
- **Evidence:**
  - Fixed-step simulation (`FixedStepClock` with a `maxSubsteps` cap) and deterministic body creation order.
  - Contact events that are identical at any thread count, a connectivity graph with anchors, a stress solver whose results are bit-identical at any worker count, and a budgeted CG solve.
  - Pooled debris with priority eviction, particle LOD and fading.
  - Hierarchical chunk rendering, editor stress and anchor debug views, scenario tests and golden hashes.
- **Impact:** a solid base. Remediation must keep the golden hashes stable or deliberately re-baseline them.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD (fresh) |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are all vendored (SDL Joystick 355, Hidapi, Tracy Profiler, Manifold, ImGui docking); no physics cluster |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 SDL example and video processes (`SDL_AppIterate → ...`); no first-party physics flow |
| 4 | query | "physics queries raycast sphere cast overlap layer mask" | `IPhysicsQueries` (`engine/sdk/.../PhysicsQueries.h:36`), `Raycast`/`OverlapSphere`, test `FloorQueries`, `TriggerVolume.mask` |
| 5 | query | "select physics engine module graph" | `ResolveModuleGraph`, `ModuleGraph`, `PhysicsEngineInfo`, `ModuleRegistrar.PhysicsEngine`, `ModuleHost.LoadProject/Reload`, `PhysicsContracts` |
| 6 | query | "trigger volume overlap tracker" | `OverlapTracker`, `Overlaps`, `Update` in `engine/runtime/src/TriggerVolumes.cpp:20-96` |
| 7 | query | "debris fracture destruction" | Only vendored hits (Jolt UnitTests `HQ_DEBRIS`/`LQ_DEBRIS`, Tracy example); confirms that destruction code is not in the engine index |
| 8 | context | `IPhysicsQueries` | Ambiguous: 4 candidates (class plus 3 special members) |
| 9 | impact | `SelectPhysicsEngine` upstream | Ambiguous; the `.cpp` definition has 1 direct caller, risk LOW; the header declaration is UNKNOWN (no resolved callers) |
| 10 | context | `OverlapTracker` | 4 methods; held by `SceneRuntime.m_overlaps`; no processes |
| 11 | context | `IPhysicsQueries` (uid, class) | Only incoming edge is EXTENDS from test `FloorQueries`; no in-index production implementer (Jolt's `JoltQueries` is in unindexed `modules/`) |
| 12 | cypher | IMPORTS of `PhysicsQueries.h` | 0 rows. The index records no include edges into this header (the test that includes it is unresolved too), so this was treated as unknown, not as proof the header is unused |
| 13 | cypher | engine/tests symbols named `*Physics*`/`*Destruct*` | 40 rows: SDK, hotreload and editor selection code, `MemoryTag` Physics/Destruction; no simulation code in the engine |
| 14 | cypher | CALLS from `engine/game` `Step` | `GameSession::Step` calls only `StepNavigation`, so the session has no physics step (PHYS-001) |
| 15 | query | "GameSession step fixed timestep module systems scene" | `GameSession.Step`, `ModuleHost.RunSystems`, `ModuleRegistrar.AddSystem`, `ExecutorDesc.stepNanoseconds`; no physics phase |

After the graph minimum, `modules/physics` and `modules/destruction` were read with file tools (Bash `cat`/`grep`/`sed -n`), because they are gitignored and not indexed.

## Analysis limitations
- `modules/physics` and `modules/destruction` are not in the GitNexus index, so the module findings rest on direct reading and grep, without caller counts or impact depth.
- The index's full-text (BM25) layer is weak, and its clusters and processes are dominated by vendored third-party code, so the graph surfaced little physics-specific structure.
- Static reading only. Nothing was built, run or benchmarked (rules for this analysis), so the frame-time costs in PHYS-003, PHYS-005 and PHYS-009 are estimates, not measurements.
- PHYS-004 (sleeping bodies) relies on Jolt not waking bodies that were touching a removed body. That is Jolt's documented behaviour as I understand it, but it was not checked against the vendored v5.6.0 sources or by running a scenario.
- The vendored Jolt library internals were out of scope.

## Cross-discipline notes
- Engine-Architecture / Hot reload: components with non-trivial members are registered from module shared libraries (`ColliderComponent` holds `JPH::RefConst<Shape>`; `DebrisSourceComponent` and `RubbleSourceComponent` hold `shared_ptr`/`vector`, `modules/destruction/.../DebrisSystem.h:61-80`). Their destroy functions live in `interi_destruction`/`interi_jolt_physics`, which is risky if a host that doesn't link those libraries unloads the module.
- Concurrency-Job-System: `JoltEngine::Detach` (`modules/physics/include/interi/physics/Engine.h:63-67`) does not wait for queries already running on other threads through `JoltQueries`, so destroying the world just after detaching can cause a use-after-free.
- Determinism-Replication: destruction state isn't replicated, and saves restore "equivalently, not bit-identically" (`modules/destruction/CLAUDE.md`, Saves). Replicating destruction for multiplayer needs its own design.
- Performance-Optimization: `ApplyDamage` makes two heap allocations per call in the contact loop (`DestructionWorld.cpp:407,412`).
- Content-Pipeline-Tools: anchors are baked against a cook-time `groundHeight` plane, and nothing validates them against the placement in the level (`modules/destruction/CLAUDE.md`, Bonds).
- AI / Navigation: no runtime navmesh rebuild or obstacle API in `engine/nav`, which PHYS-015 needs.
