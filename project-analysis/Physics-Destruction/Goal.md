# Physics-Destruction — Goal State

## Vision
A project that lists the Destruction Module gets a working, stable physics and destruction simulation everywhere it runs: `GameSession`, play-in-editor and the sandbox. Two contracts make this possible:
- The engine defines a Physics Engine lifecycle and a fixed sync-phase order, built from the module's data-driven rules.
- Physics-agnostic modules have a query and interaction contract that never needs to name Jolt.

Destruction keeps its strong deterministic core: stress lattice, connectivity graph, hierarchical chunks and pooled debris. It also guarantees four properties:
- Every bond-breaking, rebuild, contact and spawn cost is budgeted per frame and per platform.
- Nothing floats once its support is gone.
- Every body, whether a piece, debris or rubble, has a full lifecycle under one budget.
- Destruction stays consistent with navigation, chains, saves and rendering.

## Acceptance criteria
- [ ] AC-1 An integration test plays `modules/destruction/projects/sandbox` through `GameSession::Step` with no hand-written loop: a charge placed and detonated through chains breaks bonds and spawns falling pieces, with identical state hashes at 1, 4 and 16 workers. (resolves PHYS-001)
- [ ] AC-2 `docs/writing-a-physics-engine.md` documents the world lifecycle contract and the sync-phase order, and `tests/hotreload/PhysicsEngineHostTests.cpp` has a test in which the selected engine is created and stepped by the host. (resolves PHYS-001)
- [ ] AC-3 Changing the project Values `com.interi.physics.jolt:gravity`, `stepRate` and `bodyBudget`, a `Layers` row or a `PhysicsMaterials` row changes simulation behaviour in `GameSession`, the sandbox and the editor `PlaySession`, each covered by a test. No production `PhysicsWorld` is built with the default `PhysicsWorldDesc`. (resolves PHYS-002)
- [ ] AC-4 The benchmarks `BM_ContactDamage_1kContacts_50kBonds` and `BM_Explosion_ManyInstances` exist with baselines, and damage cost scales with the bonds near the impact, not with total bonds (shown by a benchmark at 10x total bonds with flat cost). Contact damage events per step are capped by a setting. (resolves PHYS-003)
- [ ] AC-5 The scenario tests `SleepingPieceOnFloor_FloorDestroyed_PieceFalls` and `RubbleOnDestructibleFloor_FloorBreaks_RubbleFalls` pass at 1, 4 and 32 workers. (resolves PHYS-004)
- [ ] AC-6 A 20,000-step auto-spawn soak test (label `stress`) keeps `PhysicsSync::BodyCount()` within a single configured destruction body budget that covers pieces, debris and rubble, and shows no upward trend in step time. `PlaySession` uses a `DebrisSystem`. (resolves PHYS-005, PHYS-013)
- [ ] AC-7 A physics-agnostic test module that does not link `interi_jolt_physics` builds a layer mask by layer name, applies an impulse, reads contacts and receives stable ids in hits, all through `service.Physics*` contracts. (resolves PHYS-006)
- [ ] AC-8 With asserts compiled out, a forced contact-constraint overflow is counted in `PhysicsStats` and logged once per error kind, and running out of bodies logs at most one line per step. (resolves PHYS-007)
- [ ] AC-9 Breaking 1,000 bonds in one step produces at most one `BondBroken` (or aggregated) trigger per instance or cell and no `ChainExecutor` overrun. `OnContact` is raised at most once per entity per step, above a configurable minimum impulse. (resolves PHYS-008)
- [ ] AC-10 `BM_RebuildPieces_LargeStructure_SingleBreak` shows rebuild cost proportional to the changed region, entities and compounds are created once per `Update`, and a rebuild-work budget exists next to `stressWorkBudget`. (resolves PHYS-009)
- [ ] AC-11 With one structure damaged every frame, a second weakened structure still breaks within a bounded number of frames, and an ill-conditioned lattice still reaches a break decision. `DestructionStats` reports unconverged instances. (resolves PHYS-010)
- [ ] AC-12 The same wall cooked at 20 and 200 cells loses a broken-area fraction within ±20% from the same charge, and a charge behind an intact wall damages the far side less than 10% as much as an unshielded charge. (resolves PHYS-011)
- [ ] AC-13 A heavy body resting on a weak cantilever breaks it (external loads reach the stress solver), and a destructible stacked on another destructible falls when the lower one is destroyed. (resolves PHYS-012)
- [ ] AC-14 A 30 m/s piece does not pass through a 0.1 m wall, and pieces, debris and rubble below the world kill height are removed within 2 s. (resolves PHYS-013)
- [ ] AC-15 Destruction saves are keyed by level object id and asset cook key. Reordering a level's destructibles keeps damage on the right objects, and a save with one bad instance changes nothing. (resolves PHYS-014)
- [ ] AC-16 After a wall is breached, `nav.FindPath` finds a route through the hole within N steps, and rubble that blocks a corridor changes the path. (resolves PHYS-015)
- [ ] AC-17 The sandbox renders dynamic bodies interpolated with `FixedStepClock::Alpha()`, verified by a debug hook at a display rate above the step rate. (resolves PHYS-016)
- [ ] AC-18 `JoltDeterminism.Pile_MatchesCrossPlatformGoldenHash` and `CollapseDeterminism.MatchesCrossPlatformGoldenHash` still pass on Linux and Windows; any re-baseline is deliberate and explained in the commit. (preserves PHYS-017)

## Guardrails to keep it that way
- CI in both module repos runs the scenario, soak and determinism suites (`integration` and `stress` labels) on Linux and Windows, including the new floating-body and body-budget tests.
- Benchmark baselines (`modules/destruction/benchmarks/baselines`) cover damage, rebuild and contact handling. The nightly comparison flags regressions above 20%.
- `PhysicsStats` and `DestructionStats` (step errors, body high-water marks, unconverged instances, dropped events) are shown in the editor's Structure panel and in a sandbox overlay. Review new destruction features against them.
- A review checklist item for any new body-creating code: Is it budgeted? Does it wake its neighbours when removed? Is it saved? Does it dirty navigation?
- A grep-based CI check, or a review rule: production code must not construct `PhysicsWorld` without `MakeWorldDesc`.
- Changes to the `service.Physics*` contracts bump `INTERI_MODULE_ABI_VERSION` and update `docs/writing-a-physics-engine.md` in the same PR.

## Out of scope
- Replacing Jolt or the beam-lattice stress model; both are sound.
- Runtime (non-cooked) Voronoi fracture. Pre-fractured hierarchical assets are the intended design.
- Network replication of destruction state (Determinism-Replication discipline).
- Generic CPU optimisation outside the physics and destruction paths (Performance-Optimization) and job-system internals (Concurrency-Job-System).
- Fracture asset authoring and cook pipeline UX (Content-Pipeline-Tools), apart from the anchor-placement warning in PHYS-012.
