# Determinism-Replication — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. **DET-003, then DET-002, DET-001, DET-004.** They share one change: a versioned "executor run state" plus overlap state in the `scene.state` section (`kSceneSaveVersion` 2). DET-003 defines what the save holds, and DET-002/001/004 use it (Start suppression, the spawn counter and spawned-object records, overlap pairs). Add a "save → load → continue equals uninterrupted run" test first, and mark it `pending` until the fix lands.
2. **DET-007, DET-005, then DET-006.** Make the FP flags explicit and replace the trig in Rotate. Then add the scene/game golden hashes that pin the cross-compiler claim, including Rotate.
3. **DET-008, DET-009, DET-010.** Contract clean-up and the replay/desync foundations, done when multiplayer or replays get scheduled.

## DET-001 — Chain-spawned prefab objects are lost when a save is loaded
- **Severity / Effort:** High / M
- **Depends on:** DET-003 (the spawn-id counter must be saved too)
- **Steps:**
  1. In `engine/scene/src/SceneSave.cpp`, add a `spawned` list to `SceneState`: for each live root whose id is ≥ `ExecutorDesc::firstSpawnId` and that carries a `PrefabInstanceComponent`, store `{id, prefab path, overrides}`. Derived children are rebuilt by expansion, so they need no record of their own.
  2. In `RestoreSceneState`, before the placement pass, rebuild those roots with `SpawnPrefabs(...)` (or `ExpandInstance` + `InstantiateScene`) so they join the `HierarchyIndex`. Then apply the saved transforms and hidden flags to them like any other object.
  3. Restore the executor's `m_nextSpawnId` (see DET-003), so new spawns never reuse a restored id.
  4. Bump `kSceneSaveVersion` to 2. Version-1 saves keep today's behaviour and log that spawned objects were not recorded.
  5. Decide whether a restored spawn raises `trigger.Spawned` again. Recommended: no, because it already fired before the save.
- **Illustrative change (not applied):**
  ```cpp
  struct SpawnedState { std::uint64_t id; std::string prefab; std::vector<std::string> overrides; };
  // Serialize: if (archive.Version() >= 2) archive(state.spawned);
  ```
- **Verification:** Add the new test `SceneSave.SpawnedPrefab_SurvivesLoad`: a chain spawns a prefab, then save, step, load. Assert the root and its derived children exist with the saved transforms, and that the next spawn's id is greater than every restored id.
- **Risks / trade-offs:** A save written before a prefab changed will expand the new prefab. That matches how authored instances already behave.

## DET-002 — Loading a save re-fires `trigger.Start` on every chain
- **Severity / Effort:** High / S
- **Depends on:** DET-003
- **Steps:**
  1. Save each chain instance's `started` flag and `startStep`, keyed by the owning object's stable id plus the chain's asset path and list index. `ChainId` is a creation index and is not stable across content changes.
  2. On restore, mark the matching instances as started with the saved `startStep`, so `CollectWave0` does not fire `kTriggerStart` for them. Chains that are new since the save (no record) still start.
  3. Add a `ChainExecutor` API for this (e.g. `RestoreRunState(const ExecutorRunState&)`) rather than reaching into `Instance`.
  4. Update the comment in `SceneSave.h` to describe the new behaviour.
- **Verification:** Add the test `GameSession.Load_DoesNotRefireStart`. A chain `trigger.Start → world.SetValue(score, 0)` and an input that adds to the score; then save, load and step. The score must stay at the saved value.
- **Risks / trade-offs:** Some games may rely on Start re-running after a load to rebuild non-saved state. Offer an explicit `trigger.Loaded` instead of keeping the implicit re-fire.

## DET-003 — Executor run state is not saved
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Define `chain::ExecutorRunState` in `Executor.h`. It holds `step`, `sequence`, `nextSpawnId`, the pending `m_raised` triggers, every `Continuation` (with the chain identified as in DET-002, node id, output, entity and payload), and each instance's `started` and `startStep`.
  2. Add `CaptureRunState()` and `RestoreRunState()` to `ChainExecutor`. Restore after `AddChain` has created the instances. Drop continuations whose chain or node no longer exists, with a warning, the same way recompiles already handle them (`Executor.cpp:503-508`).
  3. Write it into `scene.state` v2 next to the Values. Use `values::Value` serialization for the payloads.
  4. With `m_step` and `m_sequence` restored, `flow.Random` continues its stream instead of restarting. Document that the seed (`ExecutorDesc::seed`) is part of the game's identity: it is configuration, not saved state.
- **Illustrative change (not applied):**
  ```cpp
  struct ExecutorRunState {
      std::uint64_t step = 0, sequence = 0, nextSpawnId = 0;
      std::vector<RaisedState> raised; std::vector<ContinuationState> continuations;
      std::vector<InstanceState> instances; // key: object id + chain path + index
  };
  ```
- **Verification:** Add the test `GameSession.SaveLoadContinue_MatchesUninterruptedRun`. Run N steps with scripted inputs and save at step K. Then load into a second session and continue to N with the same inputs. Hash the world, the Values and the trace with `ChainHash` and require them to match the uninterrupted run. Run it at 0/1/4/16 workers.
- **Risks / trade-offs:** The save grows with pending continuations. Cap the count or warn, as the activation budget already does.

## DET-004 — Trigger-overlap state is not saved
- **Severity / Effort:** Medium / S
- **Depends on:** DET-003 (same section version bump)
- **Steps:**
  1. Expose `OverlapTracker`'s active pairs (sorted `(volume, actor)`) through a read accessor and a `Restore(pairs)` method in `engine/runtime/include/interi/runtime/TriggerVolumes.h`.
  2. Capture them in `scene.state` v2 and restore them into `SceneRuntime::m_overlaps`. Drop pairs whose volume or actor no longer exists; the next `Update` then emits the correct end events.
- **Verification:** Add the test `SceneSave.ActorInsideVolume_NoOverlapRefireAfterLoad`: an actor inside a volume with a chain `trigger.Overlap → ModifyValue(+1)`. Save, load and step; the Value must be unchanged.
- **Risks / trade-offs:** None significant. The list is small and sorted.

## DET-005 — `world.Rotate` uses `std::sin`/`std::cos`
- **Severity / Effort:** Medium / M
- **Depends on:** None (DET-006 then pins the result)
- **Steps:**
  1. Add engine-owned `math::DetSin`/`DetCos` (or `DetSinCos`) in `engine/core`. Use a fixed-order minimax polynomial with explicit range reduction, all in plain `float`/`double` arithmetic with no `std::` transcendentals. With `-ffp-contract=off` and no FMA intrinsics, the result is identical on Clang and MSVC.
  2. Add `Quat::FromAxisAngleDeterministic`, or switch `FromAxisAngle` itself if editor/render callers can accept the tiny accuracy change. Use it in `CoreLibrary.cpp:261`.
  3. Add Rotate to the `ChainDeterminism` scenario and regenerate the golden hash on both CI platforms.
  4. Optionally expose `sin`/`cos` in the Expression language on the same functions (the M15 open item).
- **Illustrative change (not applied):**
  ```cpp
  transform->rotation = Normalize(Quat::FromAxisAngleDeterministic(axis, radians) * transform->rotation);
  ```
- **Verification:** A golden test of `DetSin`/`DetCos` over a fixed input sweep (hash of the bit patterns) passes on Linux and Windows. `ChainDeterminism.MatchesCrossPlatformGoldenHash` covers Rotate. The maximum error against `std::sin` stays under a stated bound (for example 2 ulp).
- **Risks / trade-offs:** The golden hash changes once. Primitives and cameras can keep `std::sin` because they are not simulation state.

## DET-006 — The cross-platform golden hash pins only the chain executor
- **Severity / Effort:** Medium / M
- **Depends on:** DET-005 (so Rotate can be included); DET-001 to DET-004 (so save/load can be part of the scenario)
- **Steps:**
  1. Add `SceneDeterminism.MatchesCrossPlatformGoldenHash` (label `stress`). It runs a fixed scene with hierarchy, prefab spawns, Move/Rotate, triggers and Values for a few hundred steps, and hashes `LocalTransformComponent` and `Transform` bits, the Values, the hidden flags and the overlap events through `StateHasher`.
  2. Add `GameSessionDeterminism.MatchesCrossPlatformGoldenHash` with the starter project (`samples/modules/starter/templates/coins`) driven by scripted input, without nav (see DET-008).
  3. Add an anim golden (`SampleClip`/`BlendPoses` over a fixed clip) if anim output is meant to feed gameplay.
  4. Keep the existing 1/4/32 and 0/1/4/16 worker comparisons.
- **Verification:** The new tests pass on `linux-clang-*` and `windows-msvc-*` in CI (`ci.yml:70`, `:114`) with the same constant.
- **Risks / trade-offs:** Golden hashes must be regenerated deliberately whenever behaviour changes. Document the regeneration in CLAUDE.md as is done for the chain hash.

## DET-007 — The MSVC floating-point model is left implicit
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `cmake/InteriArch.cmake`, add `/fp:precise` to `INTERI_ABI_COMPILE_OPTIONS` for MSVC (and `/fp:contract-` where the toolset supports it), so installed consumers inherit it through `interi::build_settings`.
  2. Add a configure-time check that fails when `CMAKE_CXX_FLAGS` (or a target's options) contain `-ffast-math`, `-funsafe-math-optimizations`, `-Ofast` or `/fp:fast`.
  3. Optionally add `-fno-fast-math` for Clang as a belt-and-braces measure.
- **Illustrative change (not applied):**
  ```cmake
  if(MSVC)
      list(APPEND INTERI_ABI_COMPILE_OPTIONS /fp:precise)
  else()
      list(APPEND INTERI_ABI_COMPILE_OPTIONS -ffp-contract=off)
  endif()
  if(CMAKE_CXX_FLAGS MATCHES "fast-math|fp:fast|Ofast|unsafe-math")
      message(FATAL_ERROR "fast-math breaks Interi's determinism contract")
  endif()
  ```
- **Verification:** `cmake --preset windows-msvc-release` shows `/fp:precise` in the compile commands. Configuring with `-DCMAKE_CXX_FLAGS=-ffast-math` fails.
- **Risks / trade-offs:** Adding to the ABI options changes the toolchain fingerprint inputs only if they are hashed. Check `Toolchain.h`.

## DET-008 — Nav stepping uses the default step length and per-toolchain math
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `GameSession::StepNavigation`, derive the step from the running executor: store the `ExecutorDesc` used in `StartScene`, or expose `ChainExecutor::StepNanoseconds()`.
  2. Expose a `DeterminismTier` on `GameSession`, or a warning in `Warnings()`, when a nav mesh is loaded: "nav agents: reproducible per toolchain only".
  3. Document the tiers in CLAUDE.md and the docs: chains are cross-platform, scene propagation is cross-platform once DET-006 lands, nav and Recast are per toolchain, and physics is per the selected Physics Engine.
- **Verification:** A test configures a non-default step and checks that agents move `speed * step` per step.
- **Risks / trade-offs:** None.

## DET-009 — The authority model is a filter only, and `isOwner` runs on workers
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Document on `ExecutorDesc::isOwner` that it is called concurrently from job workers during waves, must be thread-safe, must be pure for the duration of a step, and may read only state frozen before `Step`.
  2. Alternatively, have the executor snapshot ownership once per step (call `isOwner` serially for the entities in wave 0, then cache the results) so that user code never runs on workers.
  3. Record in the plan or docs that authority markers do not replicate outcomes. A future netcode milestone needs server→client event or state replication for Server-authority effects.
- **Verification:** A TSan test runs a Client-role executor with a non-trivial `isOwner` at 32 workers.
- **Risks / trade-offs:** Snapshotting costs one serial pass per step over the activated entities.

## DET-010 — No runtime desync detection or input recording
- **Severity / Effort:** Low / M
- **Depends on:** DET-003 (a resumable state makes recordings start from saves)
- **Steps:**
  1. Promote a small runtime state hasher into the engine, e.g. `interi::core::StateHash` over XXH3, keeping the padding-free `Hashable` concept. Add `SceneRuntime::StateHash()` covering transforms, Values, the hidden set and the executor run state.
  2. Add an optional per-step checksum log to `GameSession` (step → hash) behind a debug flag, so bug reports and future peers can find the first divergent step.
  3. Add an input recorder to `GameSession`: a list of (step, action) pairs from `RaiseInputAction`, plus seed, project/module versions and the starting save. Add a matching player that feeds it back. Version the format like `ISAV`.
- **Verification:** A test records a session, replays it into a fresh session, and gets equal per-step hashes at 0/1/4/16 workers.
- **Risks / trade-offs:** Replays are tied to content and module versions. Store the module graph and refuse mismatches, the way `SaveRegistry::Validate` does.

## DET-011 — Verified determinism mechanisms
Info only; no remediation required. Preserve these properties (Goal.md AC-11).
