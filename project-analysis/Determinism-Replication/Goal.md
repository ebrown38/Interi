# Determinism-Replication — Goal State

## Vision
Interi states its determinism contract per subsystem and meets it. Chains and the scene runtime are bit-identical at any worker count and across Clang/libstdc++ and MSVC, and a golden hash checked on both CI platforms pins each claim. Nav and physics carry their documented tiers. A save is a complete, resumable snapshot: loading it and continuing gives exactly the run that never stopped. Spawned objects, executor state, the RNG stream, timers, delays and overlaps all survive the load. The engine owns the cheap foundations its "designed for multiplayer" promise needs: per-step state hashes, step-stamped input recording, and a documented thread-safe authority hook.

## Acceptance criteria
- [ ] AC-1 After save → load, `SceneSave.SpawnedPrefab_SurvivesLoad` passes: chain-spawned roots and their prefab children exist with saved transforms and hidden flags, and the next spawn id exceeds every restored id. (resolves DET-001)
- [ ] AC-2 `GameSession.Load_DoesNotRefireStart` passes: a chain `trigger.Start → world.SetValue` does not overwrite restored Values after a load, while chains added since the save still start. (resolves DET-002)
- [ ] AC-3 `GameSession.SaveLoadContinue_MatchesUninterruptedRun` passes at 0/1/4/16 workers. It compares the hash of world, Values and trace after N steps for "save at K, load, continue" against "run straight to N", with `flow.Random`, `flow.Delay` and `trigger.Timer` in the scenario. (resolves DET-003, DET-004)
- [ ] AC-4 `SceneSave.ActorInsideVolume_NoOverlapRefireAfterLoad` passes. (resolves DET-004)
- [ ] AC-5 No simulation-path code calls `std::sin/cos/tan/atan2/pow/exp/log`. `grep -nE "std::(sin|cos|tan|atan2?|pow|exp|log)\b" engine/chain engine/scene/src/SceneRuntime.cpp engine/scene/src/Hierarchy.cpp engine/game` returns nothing. `world.Rotate` uses engine-owned deterministic trig and is included in `ChainDeterminism.MatchesCrossPlatformGoldenHash`. (resolves DET-005)
- [ ] AC-6 `SceneDeterminism.MatchesCrossPlatformGoldenHash` and `GameSessionDeterminism.MatchesCrossPlatformGoldenHash` (label `stress`) exist and pass with the same constant on `linux-clang-*` and `windows-msvc-*` CI jobs. (resolves DET-006)
- [ ] AC-7 MSVC compile commands contain `/fp:precise`. Clang ones contain `-ffp-contract=off`. Configuring with `-DCMAKE_CXX_FLAGS=-ffast-math` (or `/fp:fast`) fails with a determinism error. (resolves DET-007)
- [ ] AC-8 `GameSession::StepNavigation` uses the running executor's step length, and a test with a non-default step passes. Docs list each subsystem's determinism tier (cross-platform, per-toolchain, or engine-defined). (resolves DET-008)
- [ ] AC-9 `ExecutorDesc::isOwner` documents its concurrency and purity contract, or is called only serially. A 32-worker TSan test of a Client-role executor passes. (resolves DET-009)
- [ ] AC-10 The engine exposes a runtime state hash (`SceneRuntime::StateHash()` or equivalent) and a `GameSession` input recorder/player. A recorded session replays with equal per-step hashes. (resolves DET-010)
- [ ] AC-11 The verified properties in DET-011 still hold: integer-ns fixed step, SplitMix `Random`, (priority, entity, sequence) firing order, (key, sequence, slot) command playback, id-sorted trigger scans, and no unordered-container iteration on simulation paths. (preserves DET-011)

## Guardrails to keep it that way
- Cross-platform golden hashes (chain, scene, game session) run in the `stress` label on both Linux Clang and Windows MSVC CI. A change to the constant requires a reviewed commit that explains why behaviour changed.
- The 1/4/32 and 0/1/4/16 worker comparisons stay in `interi_*_stress_tests` and must be TSan-clean.
- The save/load continuity test (AC-3) is part of the `integration` label. Any new runtime state added to `ChainExecutor`, `SceneRuntime` or `GameSession` must either be captured in `scene.state` (with a version bump) or be listed in `SceneSave.h` as intentionally transient.
- A CI grep (or clang-tidy `bugprone`/custom check) forbids `std::` transcendentals, `<random>` distributions, `rand()`, `std::chrono::*_clock::now()` and iteration over `std::unordered_*` in `engine/chain`, `engine/scene` (except `Primitives.cpp`), `engine/game`, `engine/ecs` and `engine/anim`.
- A configure-time check rejects fast-math flags (AC-7).
- Review checklist item: "Does this change add simulation state? Is it ordered, seeded, hashed and saved?"

## Out of scope
- Building netcode: transport, server-authoritative replication, delta compression, quantization, relevancy, late-join streaming and rollback. Nothing is promised for a milestone yet; DET-009 and DET-010 only lay foundations.
- Physics and destruction determinism and replays (Jolt `CROSS_PLATFORM_DETERMINISTIC`, IRPL). These belong to the gitignored `modules/physics` and `modules/destruction` projects.
- Cross-platform bit-exactness of Recast/Detour nav baking and queries. These remain per-toolchain, and cooked `.inav` files ship.
- Render-only math (camera rigs, `Slerp` interpolation, primitive mesh generation, particles), which never feeds simulation state.
