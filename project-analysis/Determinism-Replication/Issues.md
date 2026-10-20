# Determinism-Replication — Issues

**Project:** Interi (`/home/eric/CLionProjects/Interi`, HEAD 4631f35)
**Analyzed:** 2026-10-19
**Scope:** First-party engine code that feeds simulation state or saves: `engine/chain` (Executor, CoreLibrary, Expression), `engine/scene` (SceneRuntime, SceneSave, Prefab, Triggers), `engine/game` (GameSession), `engine/runtime` (FixedStepClock, SaveGame, TriggerVolumes), `engine/core` (Random, math kernels used by chains), `engine/ecs` (CommandBuffers, Scheduler keys, World containers), `engine/nav` (as called from GameSession), `testing/StateHasher.h`, the determinism tests (`tests/chain/stress`, `tests/scene`, `tests/game`), `cmake/InteriArch.cmake` and `.github/workflows/ci.yml`. I used the module projects' docs (`modules/physics`, `modules/destruction`) for context only, and the milestone plan to see what determinism level the project promises. Editor, renderer, RHI, audio and asset import are out of scope except where they share code with simulation.
**Summary:** 11 issues — Critical: 0 · High: 2 · Medium: 4 · Low: 4 · Info: 1

## Required determinism level

- **What the project claims.** The claims come from CLAUDE.md and the milestone plan (`~/.claude/plans/create-a-plan-for-starry-dove.md` line 30: "Multiplayer is designed for but not built: Chain Reaction is deterministic and events carry authority markers. There is no netcode milestone"). Simulation results must be **bit-identical at any thread count**. The chain executor must also be **bit-identical across Clang/libstdc++ (Linux) and MSVC (Windows)**, which a cross-platform golden hash pins. Save games are a shipped feature (`GameSession::SaveToSlot/LoadFromSlot`). Replays exist only in the Destruction module (IRPL), not in the engine.
- **What I judged against.**
  1. Thread-count invariance for every stepped system.
  2. Cross-compiler bit-exactness for chain execution. Lockstep multiplayer is "designed for", so the chain layer needs this.
  3. Save/load that resumes the same game. A loaded game should continue as the uninterrupted run would, which is also what a future late-join or replay snapshot needs.
- **What I did not hold it to.** No networking code exists and none is promised for a milestone. Missing transport, replication, bandwidth and late-join machinery is therefore reported at Low or Info severity, not as High.

## Findings

### DET-001 — Chain-spawned prefab objects are lost when a save is loaded
- **Severity:** High
- **Confidence:** High
- **Category:** Save/replay completeness
- **Location:** `engine/scene/src/SceneSave.cpp:105-112`, `engine/scene/src/SceneSave.cpp:130-136`, `engine/game/src/GameSession.cpp:148-158`, `engine/chain/src/Executor.cpp:202-207`, `engine/chain/src/Executor.cpp:256`
- **Evidence:** `world.SpawnPrefab` allocates root ids from `m_nextSpawnId` (starts at `firstSpawnId` = 2^62, `Executor.cpp:256`, `:204`). `SceneRuntime::Step` instantiates the spawned roots and their prefab-derived children (`SpawnPrefabs`, `Prefab.cpp:180-210`). `CaptureSceneState` writes every hierarchy node, spawned ones included. `LoadFromSlot`, however, first calls `StartScene()`, which builds a fresh runtime that contains only authored objects. `RestoreSceneState` then iterates only that fresh scene's nodes, and handles saved ids the scene lacks by logging them:
  ```cpp
  for (const auto& [id, object] : saved) {
      if (!index.nodes.contains(id)) {
          INTERI_LOG_WARN("Scene", "save lists object {} but the scene has no such object", id);
  ```
  The prefab path is not stored either (`ObjectState` holds only id, transform and hidden), so the object could not be rebuilt anyway. Nothing saves the executor's `m_nextSpawnId` (see DET-003). `tests/scene/SceneSaveTests.cpp` has no spawn case.
- **Impact:** Any game that spawns objects through chains (pickups, enemies, projectiles) loses them on every load, with only a log line, while the Values that counted them are restored. The loaded game no longer matches the saved one. This is data loss on a shipped path, and it also rules out the save format as a future late-join or replay snapshot.

### DET-002 — Loading a save re-fires `trigger.Start` on every chain after the saved Values are applied
- **Severity:** High
- **Confidence:** High
- **Category:** Save/replay completeness
- **Location:** `engine/game/src/GameSession.cpp:148-158`, `engine/game/src/GameSession.cpp:69-88`, `engine/chain/src/Executor.cpp:408-425`, `engine/scene/src/SceneSave.cpp:139-146`, `engine/scene/include/interi/scene/SceneSave.h:10-11`
- **Evidence:** The load runs in this order:
  1. `LoadFromSlot` does `Validate`, then `StartScene()` (a new `SceneRuntime`, so a new `ChainExecutor` whose `Instance::started = false`), then `Saves().Apply(game)`, which restores the Values.
  2. On the next `Step`, `CollectWave0` marks every instance started and fires `kTriggerStart` for it (`Executor.cpp:411-425`).
  3. The effects of those Start chains (`world.SetValue`, `world.SpawnPrefab`, `nav.MoveTo` …) then apply on top of the restored state.

  `SceneSave.h` documents that timers and continuations "start over", but it does not mention that Start runs again. The engine's own nav test chain is an example of the pattern: `trigger.Start → nav.MoveTo` (`tests/game/GameSessionTests.cpp:160-167`). The only save/load test (`GameSessionTests.cpp:134-151`) uses an input-driven chain, so it cannot catch this.
- **Impact:** Chains commonly initialise state on Start, e.g. "score = 0" or "spawn the boss". After a load, that initialisation overwrites the restored Values and spawns duplicates. The bug is silent and depends on content, so it surfaces as "my save didn't keep my score" reports.

### DET-003 — Executor run state is not saved, so a loaded game diverges from the uninterrupted run
- **Severity:** Medium
- **Confidence:** High
- **Category:** Save/replay completeness · RNG
- **Location:** `engine/chain/include/interi/chain/Executor.h:299-309`, `engine/chain/src/Executor.cpp:444`, `engine/chain/src/Executor.cpp:460-466`, `engine/chain/src/Executor.cpp:717-718`, `engine/scene/include/interi/scene/SceneSave.h:10-11`
- **Evidence:** None of the executor's run state goes into `scene.state`: `m_step`, `m_sequence`, `m_continuations` (pending `flow.Delay` / `FireAfter`), each instance's `startStep` (timer phase), `m_raised` (triggers raised for the next step) and `m_nextSpawnId`. The per-activation random seed is `HashCombine(HashCombine(HashCombine(m_desc.seed, m_step), firing.sequence), activation.link)` (`Executor.cpp:717-718`). After a load, `m_step` and `m_sequence` restart at 0, so `flow.Random` produces the same sequence it produced at the start of the game. Timer phases also realign to the load step (`elapsed = m_step - instance->startStep`), and pending delays are dropped. The header documents part of this ("they start over").
- **Impact:** For single player, delayed effects are lost and random outcomes become predictable across reloads (save-scumming yields the same rolls). A save also cannot serve as a resumable snapshot, so the "designed for multiplayer" claim has no late-join or replay foundation: a peer that loads a snapshot cannot continue in lockstep with one that never stopped.

### DET-004 — Trigger-overlap state is not saved, so `trigger.Overlap` re-fires on the first step after a load
- **Severity:** Medium
- **Confidence:** High
- **Category:** Save/replay completeness
- **Location:** `engine/scene/include/interi/scene/SceneRuntime.h:116`, `engine/scene/src/SceneRuntime.cpp:205-217`, `engine/runtime/include/interi/runtime/TriggerVolumes.h:60-72`
- **Evidence:** `OverlapTracker m_overlaps` keeps the set of overlapping (volume, actor) pairs in `m_active` and raises only the changes. A loaded game gets a fresh `SceneRuntime`, so `m_active` is empty. Every actor that was standing inside a volume at save time therefore produces a new `kTriggerOverlap` "begin" on the first step after the load. The uninterrupted run would raise nothing.
- **Impact:** Overlap-driven effects run a second time on load: `world.ModifyValue` counters, damage zones, checkpoint messages. Together with DET-002 and DET-003, a loaded game is not equivalent to the saved one.

### DET-005 — `world.Rotate` uses `std::sin`/`std::cos`, so chain results differ between standard libraries, and the golden hash does not cover it
- **Severity:** Medium
- **Confidence:** High
- **Category:** Floating point / cross-platform determinism
- **Location:** `engine/chain/src/CoreLibrary.cpp:244-266`, `engine/core/include/interi/core/math/MathTypes.h:26-31`, `tests/chain/stress/ChainDeterminismTests.cpp:152-156`
- **Evidence:** `world.Rotate` computes `Normalize(Quat::FromAxisAngle(axis, radians) * transform->rotation)`, and `FromAxisAngle` calls `std::sin(half)` and `std::cos(half)`. Correct rounding of these is not guaranteed, and glibc libm and the MSVC UCRT may differ in the last ulp. The chain golden hash deliberately leaves Rotate out (CLAUDE.md: "Rotate (sin/cos) stays out of it"), and the plan records this as an open M15 item (`starry-dove.md:310`). The Expression language bans trigonometry for this reason, but this core-library event does not follow the same rule. The rotated transform persists into `LocalTransformComponent`, then into the save and into later chain reads.
- **Impact:** Any chain that uses `world.Rotate` breaks the claim that chains are bit-identical across Linux and Windows, which is the basis of the "multiplayer designed for" statement. The failure is silent: no test or CI gate exercises it.

### DET-006 — The cross-platform golden hash pins only the chain executor; scene, game, ECS and anim stepping have only in-process thread-count checks
- **Severity:** Medium
- **Confidence:** High
- **Category:** Determinism testing
- **Location:** `tests/chain/stress/ChainDeterminismTests.cpp:152-156`, `tests/scene/SceneRuntimeTests.cpp:133-145`, `tests/scene/TriggerTests.cpp:107`, `.github/workflows/ci.yml:70`, `.github/workflows/ci.yml:114`
- **Evidence:** A search for `GoldenHash` or a pinned 64-bit hash literal in `tests/` finds only `ChainDeterminism.MatchesCrossPlatformGoldenHash` (plus fixed-value tests for `Random`, `Hash64` and `StateHasher`). `SceneRuntime` (transform propagation with `Transform::Inverse`, prefab spawning, overlap scanning), `GameSession` (module systems plus scene plus nav), the ECS `CommandBuffers` playback and `anim` sampling are compared across 1/4/32 (or 0/1/4/16) workers inside one process. That cannot detect a Clang-vs-MSVC difference. CI does run `stress` on both compilers (`ci.yml:70`, `:114`), so a pinned hash would be checked on both for free.
- **Impact:** A compiler-dependent change in propagation, prefab float round-trips or anim blending would go unnoticed until a cross-machine feature (replays shared between users, lockstep) is built on it.

### DET-007 — The MSVC floating-point model is left implicit, and nothing forbids fast-math
- **Severity:** Low
- **Confidence:** High
- **Category:** Floating point / build flags
- **Location:** `cmake/InteriArch.cmake:28-33`
- **Evidence:** The file adds `-ffp-contract=off` only for non-MSVC compilers (`if(NOT MSVC)`), and its comment says "MSVC does not" contract. Correctness on Windows therefore depends on the MSVC default `/fp:precise` and on the current default of not contracting. No `/fp:precise` (or `/fp:contract-` equivalent) is passed explicitly, and no check rejects `/fp:fast`, `-ffast-math` or `-funsafe-math-optimizations` coming in through `CMAKE_CXX_FLAGS` or a consumer toolchain file. The physics module, by contrast, does pin `/fp:precise` (`modules/physics/CLAUDE.md:15`).
- **Impact:** A toolchain-file or preset change could silently break cross-compiler bit-exactness. Only the single chain golden hash would notice (see DET-006).

### DET-008 — Nav stepping uses the default step length and per-toolchain float math inside the otherwise deterministic GameSession step
- **Severity:** Low
- **Confidence:** High
- **Category:** Time / determinism tiers
- **Location:** `engine/game/src/GameSession.cpp:97-101`
- **Evidence:** `const float kStepSeconds = static_cast<float>(chain::ExecutorDesc{}.stepNanoseconds) * 1e-9f;` takes the step length from a default-constructed `ExecutorDesc`, not from the executor that actually runs the scene. They agree today only because `StartScene` also uses a default `SceneRuntimeDesc` (`GameSession.cpp:82`). Nav agent moves are also written into `LocalTransformComponent`, and CLAUDE.md states that Recast/Detour math is "reproducible per toolchain, not across platforms". Neither tier is recorded on the session.
- **Impact:** A game that configures a different step would move agents at the wrong speed, while chains, timers and delays use the real step. A game that uses nav agents leaves the cross-platform tier without any signal, so the game state is no longer bit-exact across compilers.

### DET-009 — The authority model is a filter only, and the `isOwner` callback runs on worker threads without a stated contract
- **Severity:** Low
- **Confidence:** High
- **Category:** Authority / replication readiness
- **Location:** `engine/chain/include/interi/chain/Executor.h:134-147`, `engine/chain/src/Executor.cpp:537-544`, `engine/chain/src/Executor.cpp:686-711`
- **Evidence:** `ExecutorRole::Client` skips `Server` events, and skips `Owner` events through `std::function<bool(uint64_t)> isOwner`. `HasAuthority` is called inside the `ParallelFor` body (`Executor.cpp:707`), so `isOwner` runs concurrently on workers. The header does not require it to be thread-safe and pure. Nothing replicates the skipped events' effects (the plan says "Authority is a filter only … no replication", `starry-dove.md:317`).
- **Impact:** A future netcode layer that plugs in an `isOwner` reading mutable ownership tables would race, or produce worker-count-dependent results. The authority markers by themselves give no server→client path for authoritative outcomes. This is acceptable while multiplayer is unbuilt, but it should be stated in the contract.

### DET-010 — No runtime desync detection or input recording; the state hashers are test-only
- **Severity:** Low
- **Confidence:** High
- **Category:** Desync detection / replays
- **Location:** `testing/include/interi/testing/StateHasher.h`, `tests/ecs/EcsTestUtil.h:31`, `tests/chain/ChainHash.h`, `engine/game/src/GameSession.cpp:129-132`
- **Evidence:** `StateHasher` (`interi::testing`), `HashWorld` and the chain hash helpers live under `testing/` and `tests/`, and the engine libraries cannot use them. A graph search over `engine/` for replay, checksum, network or replication symbols finds nothing relevant; the editor's `RunDevLoop` "replay" is a dev-loop callback. `GameSession::RaiseInputAction` forwards straight to the executor without recording the step-stamped input.
- **Impact:** A game cannot log per-step checksums to locate the first divergent step in a bug report, and cannot record or replay a session. Both are the cheapest foundations for the "designed for multiplayer" goal, and for reproducing player bugs today.

### DET-011 — Verified determinism mechanisms (no action)
- **Severity:** Info
- **Confidence:** High
- **Category:** Verification record
- **Location:** `engine/runtime/include/interi/runtime/FixedStepClock.h:22-51`, `engine/core/include/interi/core/Random.h:13-48`, `engine/chain/src/Executor.cpp:519-535`, `engine/chain/src/Executor.cpp:684-809`, `engine/ecs/src/CommandBuffer.cpp:112-113`, `engine/ecs/include/interi/ecs/Scheduler.h:47-50`, `engine/scene/src/Triggers.cpp:65-77`, `testing/include/interi/testing/StateHasher.h:20-22`
- **Evidence:** The following checked out:
  - **Clock.** `FixedStepClock` reads no clock and accumulates integer nanoseconds. Within the engine, only the editor's `ScenePlaySession` holds one (graph `context`).
  - **RNG.** `Random` is SplitMix64 with exact int-to-float conversions. Chains derive per-activation seeds by `HashCombine`, never from `<random>` or `rand()`.
  - **Chain ordering.** The executor sorts firings by (priority, entity, unique sequence) and merges parallel outcomes in activation order. Effects apply serially, and transforms flush ordered by entity handle with sequential command keys.
  - **Command playback.** `CommandBuffers::Playback` sorts by (key, sequence, slot). Scheduler keys are `(systemIndex << 40) | ordinal`, and each module's scheduler plays back after its own run.
  - **Trigger scans.** These are re-sorted by stable id after chunk iteration.
  - **Containers.** The only unordered containers on simulation paths (`World::m_archetypeByMask`, ConvexHull edge maps) are lookup-only.
  - **Hashing.** `StateHasher` rejects types with padding (`has_unique_object_representations_v`).
  - **FP flags and CI.** `-ffp-contract=off` is set for Clang, and CI runs the `stress` label (which includes the golden hash) on both Clang and MSVC.
- **Impact:** None. These are the properties the remediation must preserve.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) … ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 clusters are all third-party (SDL Joystick/Hidapi, gtest Test, Tracy, Manifold, ImGui docking) |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 examples/backends under external/vcpkg/buildtrees |
| 4 | cypher | Processes whose step-1 symbol is under `engine/` | 5 flows, all `CookModel → …` (asset_import); no simulation-tick process indexed |
| 5 | query | "random seed HashCombine" | `interi::Random` (engine/core/Random.h) plus gtest/Catch2 seed noise |
| 6 | query | "snapshot serialize save capture restore" | SaveGame.h `SaveSectionDef`/`RestoreReport`, `SaveRegistry::Add/Restore`, `ModuleHost::Saves`, EditorScene snapshots |
| 7 | query | "fixed step clock checksum state hash" | `FixedStepClock` (+`Alpha`), `ScenePlaySession::Advance`; xxhash noise |
| 8 | context | `Random` (Class, core/Random.h) | 5 methods, `m_state`; no indexed incoming callers (header-only use) |
| 9 | context | `FixedStepClock` (uid, Class) | 7 methods, integer-ns members; only typed holder `ScenePlaySession::m_clock` |
| 10 | cypher | CALLS into `Step/Restore/Capture/SaveToSlot/LoadFromSlot` from `engine/` | 1 row (DataTables Restore → ContributionRegistry); member-call edges sparse |
| 11 | cypher | engine symbols named Executor/SceneRuntime/GameSession/StateHasher/SceneSave | Located `ChainExecutor`, `ExecutorRole`, `ExecutorDesc`, `SceneRuntime`, `RegisterSceneSaveSection`, `kSceneSaveVersion` |
| 12 | context | `ChainExecutor` (uid, Class) | 28 methods incl. `HasAuthority`, `SortFirings`, `CollectWave0`; held by `SceneRuntime::m_executor`, tests only |
| 13 | cypher | engine symbols named replay/record/checksum/hashworld/network/replicat | No replay/checksum/network symbols in engine; only editor dev-loop `Replay`, `TraceRecord`, allocation records |

Follow-up file reads (after the graph pass): `Executor.h/.cpp`, `CoreLibrary.cpp`, `FixedStepClock.h`, `Random.h`, `SceneSave.h/.cpp`, `SceneRuntime.cpp`, `GameSession.cpp`, `Triggers.cpp`, `CommandBuffer.cpp`, `Scheduler.h`, `MathTypes.h`, `StateHasher.h`, `InteriArch.cmake`, `ci.yml`, and literal greps for `std::sin|cos|pow…`, `unordered_*|HashMap`, `sort`, `from_chars|strto*` and golden hashes.

## Analysis limitations
- Static reading only. No builds or test runs, so cross-compiler divergence of `std::sin/cos` (DET-005) is inferred from the C/C++ standards (transcendentals are not required to be correctly rounded), not measured.
- The BM25 index failed to build, so `query` relied on vector search. Member-call edges into `Step`/`Restore` are sparse in the graph, so I followed call order by reading the code.
- `modules/` is not indexed and is a separate repository. I read the Physics and Destruction modules' docs only as context (Jolt `CROSS_PLATFORM_DETERMINISTIC`, IRPL replays) and did not analyze their code.
- I did not trace JSON float parsing inside nlohmann-json in depth. `Values_RoundTrip_BitExact` reportedly pins it, and both platforms' `strtod` are correctly rounded.

## Cross-discipline notes
- Concurrency-Job-System: `ExecutorDesc::isOwner` is invoked from `ParallelFor` workers (`engine/chain/src/Executor.cpp:707`); its thread-safety is undocumented.
- Unit-Tests / CI-CD: no test covers save → load → continue equivalence, spawned objects across a load, or Start re-firing after a load (`tests/scene/SceneSaveTests.cpp`, `tests/game/GameSessionTests.cpp:134-151`).
- Gameplay/Scene: `SpawnPrefabs` (`engine/scene/src/Prefab.cpp:180-210`) does not register `ChainsComponent` chains of spawned objects with the executor (only `SceneRuntime::Create` does). Worth confirming whether chains on spawned prefabs are meant to run.
- Physics-Destruction: engine-side `service.PhysicsQueries` results feed chains/modules; their cross-platform tier is the physics module's responsibility (`modules/physics/CLAUDE.md:15-16`).
