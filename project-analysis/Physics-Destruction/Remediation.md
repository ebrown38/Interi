# Physics-Destruction — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

Repository labels: **engine** means this repo; **physics** means `modules/physics`; **destruction** means `modules/destruction`. The modules are separate git repositories, so each change lands in the repo that owns the file. Any change to simulation output moves `JoltDeterminism.Pile_MatchesCrossPlatformGoldenHash`, `CollapseDeterminism.MatchesCrossPlatformGoldenHash` or the render goldens. Re-baseline those on purpose, in the same commit, and say why.

## Recommended order
1. **PHYS-001, PHYS-002.** Give the engine a physics lifecycle and step phase, and build the world from the module's rules. Every later fix needs a single, correct place where physics and destruction run, including `GameSession` and play-in-editor.
2. **PHYS-004, PHYS-005, PHYS-003.** Fix correctness and budgets for the visible failure modes: floating bodies, unbounded body growth, and damage cost that can cause hitches.
3. **PHYS-007, PHYS-009, PHYS-010.** Make the solver and break pipeline robust under load: overflow handling, rebuild cost, and stress starvation.
4. **PHYS-011, PHYS-012, PHYS-013.** Improve damage and support model fidelity and collision robustness. Plan these with design, because they change tuning.
5. **PHYS-008, PHYS-014, PHYS-015, PHYS-006.** Coupling: chain events, saves, navigation, and the breadth of the query contract.
6. **PHYS-016.** Polish.

## PHYS-001 — The Physics Engine contract has no world lifecycle or step phase
- **Severity / Effort:** High / L
- **Depends on:** None (an Engine-Architecture review of the ABI bump is advisable)
- **Steps:**
  1. **engine:** add a reserved contract, for example `service.PhysicsWorld`, in `engine/sdk/include/interi/sdk/` beside `PhysicsQueries.h`. It is a hook that returns an `IPhysicsRuntime` with `Create(World&, JobSystem&, const values::DataRegistry&, const HookRegistry&)`, `Step(World&, JobSystem&, CommandBuffers&, float dt)` (a sync point) and `Destroy()`. Add it to `PhysicsContracts()` so only a `PhysicsEngine` module may provide it.
  2. **engine:** add a sync-point phase to the module API. One option is `ModuleRegistrar::AddSyncStep(name, phase, fn)`, with phases `PrePhysics`, `Physics`, `PostPhysics` and `PostDestruction`. These run on the calling thread between `RunSystems` and `SceneRuntime::Step`, so modules can make structural changes there. Bump `INTERI_MODULE_ABI_VERSION`, as the CLAUDE.md rule requires.
  3. **engine:** in `GameSession::Step` (`engine/game/src/GameSession.cpp:90`), run the phases in order with the scene's fixed `dt` (`ExecutorDesc::stepNanoseconds`). Do the same in editor play (`ScenePlay`).
  4. **physics:** implement the runtime in `JoltEngineModule.cpp`. It owns the `PhysicsWorld` and `PhysicsSync`, attaches to `JoltEngine` and runs `PhysicsSync::Step` in the `Physics` phase.
  5. **destruction:** have `DestructionModule.cpp` create `DestructionWorld` and `DebrisSystem` when it registers. It runs `WeaponSystem::Update` and `DestructionWorld::Update` in `PostPhysics`, and `DebrisSystem::Update` plus `RaiseDestructionTriggers` in `PostDestruction`, then attaches to `DestructionEngine`.
  6. **destruction:** rewrite the sandbox loop (`game/sandbox/main.cpp:749-777`) to call the phases rather than the individual systems.
  7. **engine:** document the lifecycle and the phase order in `docs/writing-a-physics-engine.md`.
- **Illustrative change (not applied):**
  ```cpp
  // engine/game/src/GameSession.cpp
  void GameSession::Step()
  {
      Host().RunSystems(*m_jobs, m_scene->Commands());
      Host().RunSyncPhase(SyncPhase::PrePhysics, dt);
      Host().RunSyncPhase(SyncPhase::Physics, dt);        // the Physics Engine's PhysicsSync::Step
      Host().RunSyncPhase(SyncPhase::PostPhysics, dt);    // destruction, weapons, characters
      Host().RunSyncPhase(SyncPhase::PostDestruction, dt);// debris, triggers
      m_scene->Step();
      StepNavigation();
  }
  ```
- **Verification:** add a new integration test that plays `modules/destruction/projects/sandbox` through `GameSession` with no hand-written loop and checks that a charge placed through chains breaks bonds (`DestructionWorld::LastStats().bondsBrokenByDamage > 0`) and pieces fall. Compare the hashes at 1, 4 and 16 workers. `PhysicsEngineHostTests` gains a case where the selected engine is stepped.
- **Risks / trade-offs:** the ABI bump makes every module rebuild. The phase order becomes a public contract, so keep it small. Alternative: a `GameSession` callback for game-owned physics only. That is simpler, but play-in-editor would still have no physics.

## PHYS-002 — The Jolt module's data-driven rules are never applied
- **Severity / Effort:** High / S
- **Depends on:** PHYS-001 (the natural place to build the world)
- **Steps:**
  1. **physics:** build the world from the rules when the runtime is created: `MakeWorldDesc(ReadPhysicsRules(data), &hooks)`. The engine's session step (`ExecutorDesc::stepNanoseconds`, the same step `GameSession::Step` uses at `GameSession.cpp:100`) is the only physics clock: pass it into the physics config and step `PhysicsWorld` with exactly that `dt`, so physics stays in lockstep with chains, navigation and destruction. `rules.stepRate` never drives an independent `dt` or clock; it only validates (or, at project load, configures) that same session step: when `1s / stepRate` differs from the session step, log a warning (or fail the load) and keep the session step.
  2. **physics:** call `ApplyPhysicsMaterial` while bodies are created, wherever a component names a `PhysicsMaterials` row. Add a material row field to `RigidBodyComponent` if needed.
  3. **destruction:** in the sandbox (`main.cpp:375`), construct `PhysicsWorld` after `modules.LoadProject` with the desc built from the rules and the session step. In `PlaySession`, accept `PhysicsWorldDesc`/`DestructionSettings` built from the project's `DataRegistry` instead of defaults (`PlaySession.cpp:16-21`), and take the step from the session rather than from the rules.
- **Illustrative change (not applied):**
  ```cpp
  const auto rules = interi::physics::ReadPhysicsRules(modules.Data());
  interi::physics::PhysicsWorld physics(jobs, interi::physics::MakeWorldDesc(rules, &modules.Hooks()));
  const std::int64_t stepNs = interi::chain::ExecutorDesc{}.stepNanoseconds; // the session's step, the only clock
  if (rules.stepRate != 0 && 1'000'000'000 / rules.stepRate != stepNs) { INTERI_LOG_WARN("stepRate ignored: ..."); }
  interi::FixedStepClock clock({.step = std::chrono::nanoseconds(stepNs)}); // shared with chains, nav, destruction
  ```
- **Verification:** a module test sets the project Value `gravity = (0,-1,0)` and checks that a dropped body's velocity after one simulated second (session steps) matches; a `stepRate` that disagrees with the session step leaves the step unchanged and logs (or fails the load), and physics, chain and nav step counts stay equal. A test with a `Layers` row that disables Debris and Static shows debris falling through the floor. A grep in CI (or a review check) confirms that production code calls `MakeWorldDesc`.
- **Risks / trade-offs:** the sandbox keeps the default rules, so goldens shouldn't change. Projects that already set non-default Values will now behave differently, which is the intended fix.

## PHYS-003 — Contact and explosion damage visit every bond with no culling or cap
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. **destruction:** store the instance's world-space bounds and each piece's bounds (from the asset AABB and piece poses). In `Update` and `ApplyContactDamage` (`DestructionWorld.cpp:437-472,751-757`), skip instances whose bounds don't intersect `damage.center ± damage.radius`.
  2. **destruction:** give each asset a uniform grid or BVH over bond centroids in asset space, built once in `ShapesFor`/`AssetShapes`. Transform the damage centre into each piece's space and visit only bonds in the overlapping cells. Bonds keyed by piece let one transform per piece serve all of that piece's bonds.
  3. **destruction:** remove the per-call `poses` and `centroids` vectors (`:407,:412`) and reuse member scratch buffers.
  4. **destruction:** merge contact damage per instance per step before applying it. Cluster contacts by cell (as `ImpactBatcher` does for audio) and cap the merged events per step with a new `maxImpactEventsPerStep` setting, carrying any excess energy into the strongest event of its cell.
- **Verification:** add benchmarks `BM_ContactDamage_1kContacts_50kBonds` and `BM_Explosion_ManyInstances` under `benchmarks/destruction_runtime` with baselines. Expect at least a 10x improvement on the many-instances case. The scenario tests and `CollapseDeterminism` must keep passing. The golden hash may change only if contact merging alters results; re-baseline it deliberately.
- **Risks / trade-offs:** merging contacts changes which bonds take damage, which affects gameplay feel. Keep the merge cell small (the impact radius) to limit the change.

## PHYS-004 — Bodies can float (static rubble; sleeping bodies on removed supports)
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. **destruction:** after `RebuildPieces` destroys or replaces pieces (`DestructionWorld.cpp:604-616`), collect the world AABB of each removed piece, expanded by a small margin. After `PhysicsSync` removes the bodies, or straight away through `BodyInterface::ActivateBodiesInAABox`, wake every body in those boxes. Do it at a sync point, in a deterministic order (sorted boxes).
  2. **physics:** add a helper `PhysicsSync::WakeAround(aabb)`, or have `DestroyRemovedBodies` record the bounds of removed bodies and wake around them automatically. That covers every system that destroys bodies.
  3. **destruction:** for rubble (`DebrisSystem.cpp:294-305`), record which bodies each member was resting on (contact pairs or a downward raycast at settle time). When one of those bodies is removed or wakes, turn the pile back into dynamic debris bodies (store the member colliders, which `RubbleSourceComponent` already keeps in part), or make rubble kinematic-asleep instead of `Static`. Alternatively, merge into rubble only over `Static` or anchored ground, never over dynamic or destructible support.
  4. **destruction:** add the tests `Scenario.SleepingPieceOnFloor_FloorDestroyed_PieceFalls` and `DebrisSystem.RubbleOnDestructibleFloor_FloorBreaks_RubbleFalls`.
- **Illustrative change (not applied):**
  ```cpp
  // after removing replaced pieces, with their last world AABBs in `removedBounds` (sorted)
  JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
  for (const JPH::AABox& box : removedBounds)
      bodies.ActivateBodiesInAABox(box, broadPhaseFilter, objectFilter);
  ```
- **Verification:** the two new scenario tests pass at 1, 4 and 32 workers. In the sandbox with `--auto-spawn 1`, add a debug counter for "sleeping dynamic bodies with no contact below" and confirm it stays at 0.
- **Risks / trade-offs:** waking more bodies costs some solver time after each break. Rubble that can revert needs its member data kept, which costs memory.

## PHYS-005 — Large falling pieces and rubble piles are outside every budget
- **Severity / Effort:** High / M
- **Depends on:** PHYS-004 (rubble ownership)
- **Steps:**
  1. **destruction:** have `DebrisSystem` own rubble piles. Track them in a third pool, `maxRubble`, with priority from size and distance and eviction or fade, and count their bodies against a total budget.
  2. **destruction:** give falling pieces above `smallPieceVolume` a lifecycle. Once asleep, hand them to the `DebrisSystem` (adopt on settle) or freeze them into rubble. Alternatively, add a `maxDynamicPieces` cap per world that evicts by the same priority (size up, age down, distance down). Never evict pieces flagged as gameplay-relevant.
  3. **destruction:** make the debris handoff the default. `PlaySession` should own a `DebrisSystem` too (`PlaySession.cpp:16-21`).
  4. **destruction:** expose a single body budget (pieces + debris + rubble) as a Value, `destructionMaxBodies`, with per-platform defaults in project data. Check it at startup against the physics `bodyBudget`.
- **Verification:** a soak test (label `stress`) runs the sandbox auto-spawn scenario for 20,000 steps and asserts that `PhysicsSync::BodyCount()` stays at or below the configured budget and that step time does not trend upward (compare the means of the first and last 1,000 steps). Add a benchmark baseline.
- **Risks / trade-offs:** evicting large pieces can be visible. Use fading or off-screen eviction first (`DebrisComponent::visible` already exists).

## PHYS-006 — The `service.PhysicsQueries` contract is too thin
- **Severity / Effort:** Medium / M
- **Depends on:** PHYS-001 (the same ABI bump)
- **Steps:**
  1. **engine:** add `std::optional<std::uint32_t> LayerBit(std::string_view layerName) const` to `IPhysicsQueries`, or a separate `service.PhysicsLayers` contract, so modules build masks by name.
  2. **engine:** add `std::uint64_t stableId` to `PhysicsHit` (the hit entity's `StableIdComponent`, 0 for none) next to `object`, and document that `object` is valid only until the next step.
  3. **engine:** add `RaycastAll` (sorted hits), `AddImpulse(object, impulse, point)` and a contact-event view (`std::span<const PhysicsContact> Contacts()`, valid between steps). Expose the step phase so callers can assert they are not running during a step.
  4. **physics:** implement the additions in `modules/physics/src/Engine.cpp`. Extend `tests/sdk/PhysicsQueriesTests.cpp` with a fake engine and `JoltEngineModuleTests` with the real one.
- **Verification:** a physics-agnostic test module, built without linking `interi_jolt_physics`, pushes a body and reacts to a contact through the contract alone. `interi_module_check` loads it with the Jolt engine pinned.
- **Risks / trade-offs:** every engine must implement a larger interface. Keep the additions minimal and give each new method a default that returns "unsupported".

## PHYS-007 — A Jolt step overflow is only asserted; running out of bodies logs every step
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. **physics:** in `PhysicsWorld::Step` (`PhysicsWorld.cpp:31-33`), keep the assert and also record overflows in a `PhysicsStats` struct (`stepErrors`, the last error, the body count, and the body-pair and contact-constraint high-water marks). Log the first occurrence per error kind with `INTERI_LOG_ERROR` in every build, shipping included.
  2. **physics:** scale `maxBodyPairs`/`maxContactConstraints` with the body budget in `MakeWorldDesc`, or expose them as Values next to `bodyBudget`.
  3. **physics:** in `CreateBodies` (`PhysicsSync.cpp:151-157`), log once per step with a count and expose `BodyCreationFailures()`. Consider tagging the entity so it isn't retried until bodies are freed.
- **Verification:** a unit test with `maxContactConstraints = 16` and a pile of 64 boxes checks that `PhysicsStats::stepErrors > 0` in a release build (asserts off), and that the step still completes. A body-cap test checks that one error log line is written per step, not one per entity.
- **Risks / trade-offs:** none of note. This adds telemetry only.

## PHYS-008 — Contact and break events reach Chain Reaction unaggregated
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. **physics:** set a non-zero default `minContactImpulse` from a Value (`contactEventMinImpulse`). In `RaiseContactTriggers` (`Engine.cpp:95-118`), raise at most one `OnContact` per entity per step (the strongest contact) and add a per-step cap with a dropped counter.
  2. **destruction:** in `RaiseDestructionTriggers` (`DestructionEngine.cpp:36-48`), aggregate `BondBroken` per instance per step (count, total energy and area, impulse-weighted centroid), or per spatial cell as `ImpactBatcher` does. Keep the per-bond `BreakEvents()` for effects only.
- **Verification:** a test breaks 1,000 bonds in one step and checks that at most one `BondBroken` trigger per instance (or cell) reaches the executor and that `ChainExecutor::LastStats()` reports no overrun.
- **Risks / trade-offs:** the payload shape of `BondBroken` changes (count and totals), which affects existing chains. Version the event or add a new `BondsBroken` event.

## PHYS-009 — Every break round relabels the whole graph and rebuilds the full static compound
- **Severity / Effort:** Medium / L
- **Depends on:** PHYS-003 (the same Update pipeline)
- **Steps:**
  1. **destruction:** within one `Update`, defer `RebuildPieces` entity creation to the end. Break rounds should only update the graph and islands (`DestructionWorld.cpp:774-786`), and entities and compounds are created once, after the last round (`:827-836`).
  2. **destruction:** make island labelling incremental. Relabel only the components that contain the endpoints of newly broken bonds, using BFS from those endpoints with a union-find split check, instead of `LabelComponents` over every chunk (`:498`).
  3. **physics + destruction:** for the anchored piece, use a `MutableCompoundShape` (sub-shapes can be removed) or split the anchored structure into per-region static compounds, so a single lost chunk does not rebuild every hull (`:523-533,:655`).
  4. **destruction:** add a budget for rebuild work (chunks relabelled plus hulls compounded per step) next to `stressWorkBudget`, and carry the excess over to the next step.
- **Verification:** a new benchmark `BM_RebuildPieces_LargeStructure_SingleBreak` should be O(changed region), not O(structure). `CascadeLimit_SpreadsBreaksOverFrames` and the collapse golden hash pass, re-baselined deliberately if the creation order changes.
- **Risks / trade-offs:** `MutableCompoundShape` queries are slower than `StaticCompoundShape`, so measure. Deferred creation changes when bodies appear within a frame. Keep the deterministic order (by smallest chunk).

## PHYS-010 — Stress breaking waits for full convergence
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. **destruction:** in `SolveWithBudget` (`StressBudget.cpp:19-41`), give each unconverged task a guaranteed minimum share (for example `max(1, budget / (4 * tasks))` iterations) before the most important task takes the rest. Alternatively, round-robin the leftover budget, with age as a tie-breaker that favours long-starved tasks.
  2. **destruction:** after N frames unconverged (a setting, for example 30), or when CG stops early (`StressSolver.cpp:531-534`), check overloads on the current iterate when its relative residual is below a looser tolerance, or restart CG from zero. Log persistent non-convergence.
  3. **destruction:** add `DestructionStats::unconvergedInstances` and `framesUnconvergedMax` so the editor's Structure panel can show them.
  4. **destruction:** correct the comment in `StressBudget.h:31-35`.
- **Verification:** a test spawns two structures, damages A every frame and weakens B's support once, then asserts that B breaks within K frames. A test with a deliberately ill-conditioned lattice (stiffness ratios of 1e8) asserts that a break decision is still made.
- **Risks / trade-offs:** checking overloads on an unconverged solution can produce false breaks. Use the looser tolerance only after the stall window.

## PHYS-011 — Explosion damage depends on fracture resolution and ignores occlusion
- **Severity / Effort:** Medium / M
- **Depends on:** PHYS-003 (the spatial bond index)
- **Steps:**
  1. **destruction:** define radial damage as a pressure (J/m^2) and apply `energy * falloff * bond.area` per bond in `ApplyRadialDamage` (`Damage.cpp:48-79`). A bond then loses the same fraction of its health whatever its size, and the cook resolution no longer changes how strong a structure is.
  2. **destruction:** add an occlusion term. Either raycast from the centre to each piece's centroid against the instance's own hulls (`IPhysicsQueries::Raycast`, at piece granularity), or attenuate by graph distance through intact bonds from the bonds nearest the centre.
  3. Re-tune `materials.json` and `weapons.json` with design, and record the new units in `module.json` tooltips.
- **Verification:** a test cooks the same wall at 20 and 200 Voronoi cells, applies the same charge, and checks that the broken-area fraction is within ±20%. A test places a charge behind an intact wall and checks that the far wall takes less than 10% of the damage of an unshielded one.
- **Risks / trade-offs:** this is a tuning change that alters every golden involving explosions, and occlusion raycasts cost time. Do occlusion at piece granularity, not per bond.

## PHYS-012 — Stress loads are self-weight within one asset only
- **Severity / Effort:** Medium / M
- **Depends on:** PHYS-001 (the order: contacts available before the destruction update)
- **Steps:**
  1. **destruction:** every N steps, sum the contact normal impulses on each chunk of the anchored or dynamic pieces from Jolt's persisted contacts (a `ContactListener::OnContactPersisted` estimate, or `Body` contact constraints), divide by `dt`, and feed them through `StressSolver::SetExternalForce` (`StressSolver.h:120`). Mark the solver unconverged only when the change exceeds a threshold.
  2. **destruction:** support between instances. At spawn, raycast or overlap-test each cooked anchor against the world. If it rests on another destructible's chunk, record a cross-instance support link, and when that chunk leaves the anchored piece, drop the anchor and re-solve.
  3. **destruction:** warn in the editor (Structure panel) when an instance's cooked anchors are not touching static geometry at its placed transform.
- **Verification:** a test parks a 5 t box on a weak cantilever and checks that the cantilever breaks, and that the same cantilever with no box stands. A test stacks two destructible walls, destroys the lower one and checks that the upper one falls.
- **Risks / trade-offs:** external loads change frequently and keep re-triggering solves. Use hysteresis and a minimum change. Cross-instance links make despawn and restore more complex.

## PHYS-013 — No CCD for destruction pieces or wrecks, and no out-of-bounds cleanup
- **Severity / Effort:** Medium / S
- **Depends on:** PHYS-005 (lifecycle)
- **Steps:**
  1. **destruction:** set `body.continuous = true` (LinearCast) for pieces created with a high initial speed, or for pieces below a size threshold (`DestructionWorld.cpp:670-684`) and for wreck parts. Alternatively, let the physics module switch a body to LinearCast while its speed × dt exceeds half its smallest extent.
  2. **destruction:** scale blast impulses by mass, or clamp the resulting velocity change (`Weapons.cpp:184-199`), so light pieces don't reach extreme speeds.
  3. **physics or destruction:** add world bounds (a Value or the level's `killY`) and remove pieces, debris and rubble outside them during the sync phase. Log the count.
- **Verification:** a test fires a 30 m/s piece at a 0.1 m wall and asserts that it doesn't pass through. A test drops a piece off the level edge and asserts that it is removed within 2 s of crossing `killY`.
- **Risks / trade-offs:** LinearCast costs more per body. Limit it to fast or small pieces.

## PHYS-014 — Save restore matches instances by position and can leave a partial restore
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. **destruction:** add a key per instance to `InstanceDamageState`: the level object's stable id (or the level's destructible index plus name) and the asset cook key (`.idest` header). Bump `kDestructionStateVersion` and keep reading the old version by position, with a warning.
  2. **destruction:** in `RestoreDestructionState` (`DestructionState.cpp:95-138`), match by key. First validate every instance (sizes, keys, the rules `RestoreDamage` checks) in a dry-run pass, then apply. Return an error before anything changes if any instance fails. Skip, and log, instances whose key is missing from the save.
- **Verification:** tests (a) reorder the level's destructibles, then save and load, and expect damage on the correct objects; (b) corrupt the bond count of the third instance and expect the first two to be untouched; (c) load a v1 save and get a warning.
- **Risks / trade-offs:** the format bump needs migration. The existing IDSS hash and version mechanism already support it.

## PHYS-015 — Destruction never updates navigation
- **Severity / Effort:** Medium / L
- **Depends on:** PHYS-001 (the phase to run in), PHYS-004 (rubble lifecycle)
- **Steps:**
  1. **engine:** add a runtime update path to `engine/nav`: tiled navmesh rebuild of dirty tiles on a Low-priority job (Recast tile cache or per-tile rebake), swapped into `NavSource` between steps. Note the cross-platform float caveat in the CLAUDE.md M24 section: runtime rebuilds are toolchain-reproducible, not identical across platforms.
  2. **destruction:** after each Update, push the AABBs of changed anchored pieces, new rubble piles and removed static geometry into a "nav dirty" list. Give dynamic pieces above a size threshold temporary obstacles (cylinders or boxes).
  3. **engine:** `StepNavAgents` re-plans agents whose path crosses a dirty tile.
- **Verification:** an integration test blows a hole in a wall between two rooms and checks that `nav.FindPath` succeeds within N steps where it failed before. Dropping rubble across a corridor makes the path detour or fail.
- **Risks / trade-offs:** runtime rebakes are expensive and break cross-platform bit-equality. Ship them as a game option, and keep cooked meshes for deterministic tests.

## PHYS-016 — The sandbox renders physics poses without interpolation
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. **destruction:** before each step, snapshot the previous `Transform` of the rendered dynamic entities (pieces, debris, vehicle, wheels), or keep a `PreviousTransform` component written by `PhysicsSync::PullTransforms`. Render `Interpolate(previous, current, clock.Alpha())` (`game/sandbox/main.cpp:657-660`).
  2. **physics:** optionally provide `PreviousTransformComponent` in `PhysicsSync` so every game gets it for free.
- **Verification:** run the sandbox at a forced 144 Hz with `--frames` and confirm that a falling piece's rendered position changes on every frame, not only on steps (a debug log or test hook).
- **Risks / trade-offs:** one frame of visual latency, which is standard.
