# Unit Tests — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi)
**Analyzed:** 2026-10-19
**Scope:** All first-party tests: `tests/` (206 C++ files, about 1,290 `TEST`/`TEST_F`/`TEST_P` cases in the 25 per-module test folders, plus `tests/samples`, `tests/package`, `tests/meta` and `tests/smoke`), the shared helpers in `testing/`, the Python `unittest` suites in `tools/license_check/tests` and `tools/new_module/tests`, test registration (`engine/*/CMakeLists.txt`, `tests/CMakeLists.txt`), `CMakePresets.json` and `.github/workflows/ci.yml`. Tests were read next to the production code they target (`engine/*/src`). No tests were run.
**Summary:** 15 issues — Critical: 0 · High: 1 · Medium: 7 · Low: 6 · Info: 1

Overall the suite is strong. Names follow `Suite, Condition_ExpectedResult`. Randomness is seeded (`testing/include/interi/testing/DeterministicRng.h`). Concurrency tests use bounded rendezvous instead of sleeps. Failure tests usually check that state is unchanged after a refusal (for example `ModuleHostTest.LoadFailure_KeepsOldModule` and the `ChainEditingTests` refusals). GPU tests fail rather than skip in Linux CI (`INTERI_REQUIRE_GPU`). Only one test has no assertion, and it is a deliberate compile-time check (`tests/core/ProfilingShippingTests.cpp:13`, backed by a `static_assert`). The findings below are the gaps that remain.

## Findings

### TEST-001 — I/O failure paths are untested: there is no fault-injecting `IFileSystem` test double
- **Severity:** High
- **Confidence:** High
- **Category:** Failure states / resource failure
- **Location:** `engine/platform/include/interi/platform/FileSystem.h:26-41` (the seam), `engine/runtime/src/SaveGame.cpp:260`, `engine/editor/src/EditorDocument.cpp:76-78,88`, `engine/editor/src/Prefabs.cpp:346`, `engine/asset_import/src/AssetDatabase.cpp:688-700`, `engine/editor/src/MaterialEditing.cpp:90`, `engine/platform/src/FileSystem.cpp:91-108`
- **Evidence:** Every engine write goes through `IFileSystem::WriteFile`, which documents an `IoError` result. The only implementations are `NativeFileSystem`, `MemoryFileSystem`, `VirtualFileSystem` and the importer's private `RecordingFileSystem`, and none of them can be made to fail on demand. The whole test tree has exactly two `IoError` assertions: `tests/core/LoggerTests.cpp:197` and `tests/sdk/ManifestTests.cpp:192` (a missing manifest file). `grep -rn MOCK_METHOD tests` finds only `tests/rhi/MockDevice.h` and `tests/platform/FakeEventSource.h`. The following failure branches are therefore never exercised:
  - `EditorDocument::Save` (`INTERI_TRY(files.WriteFile(...))` followed by `m_undo.MarkClean()`): nothing proves the document stays dirty when the save fails.
  - `AssetDatabase` cooking writes products in a loop (`INTERI_TRY(files.WriteFile(output.product.path, ...))` at :690). A failure partway through leaves some new products on disk next to the old meta.
  - `Prefabs` apply writes the prefab and then reloads the scene and clears undo (:346 onward).
  - `SaveSlots::Write` and `GameSession::SaveToSlot`.
  - `NativeFileSystem::WriteFile` truncates the target file before writing it (`std::ios::trunc`, :100). A failed write destroys the previous save, and no test covers that.
- **Impact:** Disk-full, permission-denied and read-only cases are where users lose data (saves, scenes, prefabs, cooked assets). The code returns `Result`, but no test shows that callers stay in a valid state afterwards: not marked clean, no half-cooked database, previous file intact.

### TEST-002 — TSan runs only nightly, so concurrency regressions can merge to `main`
- **Severity:** Medium
- **Confidence:** High
- **Category:** Test-suite health / CI
- **Location:** `.github/workflows/ci.yml:48` (push/PR matrix), `.github/workflows/ci.yml:146-173` (`nightly` job, `if: github.event_name == 'schedule' || ...`)
- **Evidence:** The push/PR matrix is `[linux-clang-debug, linux-clang-release, linux-clang-asan, linux-clang-tidy, linux-clang-profile]`. `linux-clang-tsan` appears only in the `nightly` job. CLAUDE.md requires that "`ModuleHostTest` and `ModuleProjectTest` must stay clean under TSan" and that TSan "matters for any concurrent code" (the job system, Logger, AudioEngine SPSC queues, FramePipeline, CommandBuffers, AsyncFileReader).
- **Impact:** A data race added in a PR passes every blocking check and is reported only after it is on `main`. The many concurrency tests (`interi_jobs_stress_tests`, `AudioEngineStress`, `FramePipelineStressTests`) catch races only under TSan. Without it they mostly check results.

### TEST-003 — The SaveGame corruption test cannot fail for "corrupt but accepted", and the `ReadSave` validation branches are untested
- **Severity:** Medium
- **Confidence:** High
- **Category:** Assertion quality / failure states
- **Location:** `tests/runtime/SaveGameTests.cpp:68-84`, `tests/runtime/SaveGameTests.cpp:86-94`, `engine/runtime/src/SaveGame.cpp:66-93`
- **Evidence:** `Read_FlippedByte_IsCorrupt_AtEveryPosition` asserts `EXPECT_FALSE(read.has_value() && read->label == "x" && read->sections.size() == 1 && Text(...) == "payload")`. A flip that `ReadSave` accepts but that changes the label or payload (for example, if the hash stopped covering part of the payload) satisfies the assertion. The test name says "IsCorrupt", but no error code is checked. `ReadSave` also rejects `sections.size() > kMaxSections` (:81) and unsorted, duplicate or empty section ids (:85-90). `WriteSave` serializes `game.sections` as given, so a test could build such a file. None does (`grep kMaxSections tests` finds nothing). `Read_TruncatedOrEmpty_Fails` checks only `has_value()`.
- **Impact:** Save integrity is a data-loss boundary. A hash scope regression, or removing the sorted/unique check (which `SaveGame::Find`'s `lower_bound` relies on), would pass the suite.

### TEST-004 — Several tests assert less than their names claim
- **Severity:** Medium
- **Confidence:** High
- **Category:** Name/body mismatch
- **Location:** `tests/asset_import/ModelImportTests.cpp:283-291`, `tests/scene/TriggerTests.cpp:113-117`, `tests/sdk/PhysicsEngineTests.cpp:430-438`, `tests/sdk/AiTests.cpp:91-101`
- **Evidence:**
  - `GltfImport.OutOfRangeMaterialFactors_AreClamped` feeds `metallicFactor 5.0, roughnessFactor -2.0` but asserts only `ValidateMaterial(model->materials[0]).has_value()`. Replacing the clamp at `engine/asset_import/src/GltfImport.cpp:253-254` with "use the default 1.0" would still pass. The test should assert metallic == 1 and roughness == 0.
  - `TriggerTest.Reflection_RoundTripsComponents` only checks `m_reflection.Find(...) != nullptr` for the two trigger components. Nothing is written or read back.
  - `CanAddModule.AtPosition_ChangesPriority` asserts that `CanAddModule(..., 0)` and `CanAddModule(..., 1)` both succeed. `CanAddModule` returns `Result<void>`, so a mutation that ignores `position` passes.
  - `AiCatalog.PlayerUsesAnotherModulesBehaviour` checks only `FindPlayer(...) != nullptr`, not that the player's behaviour resolves to `com.test.ai:Patrol`.
- **Impact:** These tests give false confidence. Readers trust the name, but the behaviour the name describes is unguarded.

### TEST-005 — Bulk "Malformed/Errors" tests check only that parsing failed, not why
- **Severity:** Medium
- **Confidence:** High
- **Category:** Assertion quality / error propagation
- **Location:** `tests/values/ValueTests.cpp:120-139` and `:247-278`, `tests/hud/HudDocumentTests.cpp:62-83`, `tests/loc/LocalizationTests.cpp:131-137`, `tests/scene/PrimitivesTests.cpp:209`, `tests/assets/MaterialTests.cpp:58`, `tests/sdk/SemVerTests.cpp:42,110`
- **Evidence:** Across `tests/`, 423 assertions have the form `EXPECT/ASSERT_FALSE(x.has_value())`, while only 158 lines inspect `error().code`. A heuristic scan found about 65 tests whose only assertions are `has_value()` or `!= nullptr`. Many are table-style lists of invalid inputs, for example `HudDocument.Parse_Malformed_Fails`, which has 16 cases, one code check, and 24 distinct `Invalid(...)` sites in `engine/hud/src/HudDocument.cpp`. With failure-only assertions, each case passes as long as any earlier or unrelated check rejects the input. If the targeted check (for example `"rowHeight" must be positive` at `HudDocument.cpp:147`) is removed, the case may still fail parsing for another reason. Some branches have no case at all: HUD non-finite numbers (`HudDocument.cpp:65`), `referenceSize` (:252) and `"widgets"` not an array (:260).
- **Impact:** Error messages and codes, which the editor shows to users, are unpinned. These tests are weaker mutation detectors than they look.

### TEST-006 — Vulkan backend failure branches are untested, including ones that need no GPU
- **Severity:** Medium
- **Confidence:** High
- **Category:** Failure states / untested risky unit
- **Location:** `engine/rhi_vulkan/src/ShaderReflection.cpp:42-88`, `tests/rhi_vulkan/ComputeTests.cpp:91-95`, `engine/rhi_vulkan/src/VulkanDevice.cpp:1017,1112,1381`
- **Evidence:** `ReflectSpirv` has five error returns: invalid SPIR-V, not exactly one entry point, unsupported stage, push constants over the limit, and a binding outside the bindless tables. Only the first is tested (`ShaderReflection.NotSpirv_ReturnsError`, with no code check). `ReflectSpirv` is pure CPU code, so these tests could also run on the Windows runners, where every GPU test skips. The bindless table-full paths (`OutOfMemory` for buffers, textures and samplers) have no test. In total, `engine/rhi_vulkan` has 13 tests for about 2,500 lines of source.
- **Impact:** CLAUDE.md states that `ReflectSpirv` rejects resources outside the bindless tables, and that rule is unguarded. Running out of bindless slots in a large scene goes down a path that has never executed.

### TEST-007 — The JobSystem wait-nesting cap (stack-depth guard) has no deterministic test
- **Severity:** Medium
- **Confidence:** High
- **Category:** Concurrency failure / untested branch
- **Location:** `engine/jobs/src/JobSystem.cpp:305-308`, `engine/jobs/include/interi/jobs/JobSystem.h:83-92`, `tests/jobs/stress/JobSystemStressTests.cpp:111,156,177`
- **Evidence:** `WaitFor` switches to `ownDequeOnly` when `waitDepth > kMaxWaitNesting` (32) and workers exist. This guard keeps stack depth bounded on 1 MB Windows threads. The only related test is a stress test whose tree has `static_assert(NestedTree::kDepth < kMaxWaitNesting)` and which asserts `maxStackedVisits <= kMaxWaitNesting + kDepth + 1`. Whether nesting ever goes past 32 depends on work-stealing timing. Deleting the `ownDequeOnly` condition would usually still pass. Nothing pins the no-worker exception (`!m_threads.empty()`) either.
- **Impact:** A regression here causes a stack overflow on Windows under deep nested waits, which is a crash that unit tests on Linux would not show.

### TEST-008 — The guarantee that a bad slot changes nothing in `GameSession::LoadFromSlot` is tested only for missing or corrupt files
- **Severity:** Medium
- **Confidence:** High
- **Category:** Graceful degradation
- **Location:** `engine/game/src/GameSession.cpp:148-158`, `tests/game/GameSessionTests.cpp:128-151`, `tests/samples/StarterSampleTests.cpp:231-240`
- **Evidence:** `LoadFromSlot` reads the slot, validates versions, then calls `StartScene()` and `Apply(game)`. The tests cover an unsupported save directory, `NotFound` and a corrupt slot (the run stays intact). They do not cover:
  - a slot with a section newer than its owner (the `Validate` refusal, after which the running scene must be unchanged);
  - a slot whose section `restore` fails after `StartScene()` has already restarted the scene.

  In the second case the scene has changed, which contradicts the CLAUDE.md claim that "a bad slot changes nothing". The expected behaviour is not pinned by any test.
- **Impact:** A save from a newer game version, or a module whose restore fails, can silently reset the player's run. The engine already has the per-section building blocks (`SaveRegistry.NewerSection_RefusesEverything`), but the session-level guarantee is unproven.

### TEST-009 — Removing the FileWatcher mtime check would pass every test
- **Severity:** Low
- **Confidence:** High
- **Category:** Mutation gap
- **Location:** `engine/hotreload/src/FileWatcher.cpp:20,32-40`, `tests/hotreload/FileWatcherTests.cpp:33-43`
- **Evidence:** The watcher compares size and `last_write_time`. To avoid coarse mtimes, every test changes the file size ("new size (modification times can be coarse)"). A same-size rebuild (common for a recompiled `.so` or `.dll`) is detected only by mtime, and no test covers it. A test can set the mtime explicitly with `std::filesystem::last_write_time(path, t + 1s)`.
- **Impact:** A hot-reload or asset-watch regression for same-size changes would go unnoticed (the `AssetWatcher` wraps the same class).

### TEST-010 — The JobSystem burst-wake unit test depends on wall-clock timing
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Determinism / flakiness
- **Location:** `tests/jobs/JobSystemTests.cpp:373-415`
- **Evidence:** `BurstWhileOneWorkerSearches_WakesEveryWorker` is in the `unit` executable, which CI runs on push under ASan. Each of 20 rounds starts with `sleep_for(5ms) // all workers go to sleep`, an assumption that nothing checks, and the test fails if 6 spinning jobs do not all meet within a 2 s `steady_clock` deadline. Other concurrency tests use 10 s rendezvous bounds (`tests/ecs/SchedulerTests.cpp:80`, `tests/runtime/FramePipelineTests.cpp:61`).
- **Impact:** On an overloaded or sanitizer-slowed runner this can fail spuriously. If workers have not actually gone to sleep, the test passes without exercising the wake protocol it targets.

### TEST-011 — `AsyncFileReader.DestroyWithRequestsInFlight_Drains` has no assertion
- **Severity:** Low
- **Confidence:** High
- **Category:** Assertion quality
- **Location:** `tests/platform/AsyncFileReaderTests.cpp:124-140`
- **Evidence:** The test creates and destroys readers with 64 in-flight requests 20 times and asserts nothing. It relies on the destructor not crashing, which a non-sanitizer build may not show for a use-after-free.
- **Impact:** In Debug and RelWithDebInfo CI runs the test cannot detect "destructor no longer waits". Only the ASan and TSan presets can.

### TEST-012 — Duplicated test helpers and inconsistent per-format corruption tests
- **Severity:** Low
- **Confidence:** High
- **Category:** Overlap / duplication
- **Location:** The `JobSystemDesc Workers(...)` helper is defined in 8 files: `tests/ecs/SchedulerTests.cpp`, `tests/hotreload/ModuleHostTests.cpp:41`, `tests/hotreload/ModuleProjectTests.cpp`, `tests/jobs/JobSystemTests.cpp`, `tests/jobs/ParallelAlgorithmsTests.cpp`, `tests/jobs/TaskGraphTests.cpp`, `tests/platform/AsyncFileReaderTests.cpp`, `tests/runtime/FramePipelineTests.cpp`. A `Bytes(...)` helper is defined in 5 files. Single-position flipped-byte tests: `tests/assets/SkeletonTests.cpp:75`, `tests/assets/TextureTests.cpp:271`, `tests/assets/MaterialTests.cpp:79`, `tests/scene/PrimitivesTests.cpp:274`, versus every position in `tests/runtime/SaveGameTests.cpp:68`.
- **Evidence:** Skeleton, Texture and Material share `engine/assets/src/AssetIo.h` (`WriteHashed`/`ReadHashed`), but each has its own near-identical test that flips one byte in the middle and checks `Corrupt`. `WriteHashed`/`ReadHashed` have no direct test that flips every byte. Mesh, SaveGame, cooked data and NavMesh each implement a similar hashed container, and their tests vary in strength.
- **Impact:** This is maintenance cost, and it gives uneven protection. One parameterized every-byte test per container implementation would replace the copies and be stronger.

### TEST-013 — The asserts-off (shipping) configuration is never tested
- **Severity:** Low
- **Confidence:** High
- **Category:** Test-suite health / skipped tests
- **Location:** `CMakePresets.json:25-58` (no preset sets `INTERI_SHIPPING`), `tests/jobs/TaskGraphTests.cpp:131-170`, `tests/core/AssertTests.cpp:52,68,106`, `tests/ecs/SchedulerTests.cpp:283,306`, `tests/rhi/CommandListPoolTests.cpp:128`, `tests/core/HashTests.cpp:106`
- **Evidence:** Eight tests `GTEST_SKIP` when `!kAssertsEnabled`, and one more (`HashTests.cpp:106`) skips when the StringId name table is compiled out. Every preset is Debug or RelWithDebInfo with asserts on and shipping off, so these skips never happen and the shipping behaviour never runs. For example, `TaskGraph::Run` on a cyclic graph in a shipping build is untested: it might hang or run partially. Only the compile-time TU `tests/core/ProfilingShippingTests.cpp` touches `INTERI_SHIPPING`.
- **Impact:** Release builds of games run code paths (compiled-out asserts, no StringId table) that CI never executes.

### TEST-014 — Editor `ValueWidget` read-only suppression and type-mismatch paths are untested
- **Severity:** Low
- **Confidence:** High
- **Category:** Untested unit
- **Location:** `engine/editor_ui/src/ValueWidgets.cpp:230-247`, `engine/editor_ui/src/ViewportOverlay.cpp:37,50`
- **Evidence:** No test includes `ValueWidgets.h` or `ViewportOverlay.h`, and `grep readOnly tests/editor_ui` finds only the asset panel's state. `ValueWidget` returns `std::nullopt` for `meta.readOnly` (:246) and for a type mismatch (:232-236), and it handles list `+`/`-`. These are the editor's guards that keep users from editing read-only module Values.
- **Impact:** If line 246 regresses, users can edit read-only Values in the editor, and nothing catches it. Headless ImGui tests already exist for other panels (`tests/editor_ui/ImGuiTestContext.h`), so these are cheap to add.

### TEST-015 — The coverage report has no floor, and GPU tests never run on Windows (observation)
- **Severity:** Info
- **Confidence:** High
- **Category:** Test-suite health
- **Location:** `.github/workflows/ci.yml:116-143` (coverage job uploads HTML only), `cmake/InteriCoverage.cmake:25`, `.github/workflows/ci.yml:114` (`# GPU tests skip: no Vulkan driver`), `tools/bench_compare/compare_benchmarks.py` (no tests)
- **Evidence:** The coverage job produces an llvm-cov report but enforces no threshold or diff gate. On Windows, every `VulkanTest`, golden and SDL-surface test skips, so the MSVC-compiled Vulkan backend and renderer never execute in CI. This is documented and intentional. `compare_benchmarks.py` has no tests and is not wired into CI.
- **Impact:** Recorded for awareness. Coverage regressions are visible only to someone who opens the artifact. Combined with TEST-006, CPU-only backend logic could be moved into tests that also run on Windows.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top clusters are third-party (Joystick, Hidapi, Profiler, Manifold, Tracy) plus six first-party "Test" clusters (331, 318, 304, 241, 157, 89 symbols) |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | All 30 are SDL3 vcpkg buildtree flows (SDL_AppIterate, Android_CreateWindow...); no first-party process in the first page |
| 4 | query | "parse manifest error malformed input Result" | Mostly nlohmann-json; first-party hits `ParseManifest`, `ParsePhysicsSections` (engine/sdk/src/Manifest.cpp), test helpers in tests/sdk/AiTests.cpp |
| 5 | query | "save game slot load corrupt validate" | `ReadSave`, `SaveSlots::List`, `SaveSlots`, `GameSession::SaveToSlot/LoadFromSlot`, `ModuleRegistrar::Saves` |
| 6 | query | "localization strings fallback language missing keys" | `Localization::Find`, `Localization::MissingKeys` (engine/loc/src/Localization.cpp), values JsonSchema getters |
| 7 | context | ReadSave (engine/runtime/src/SaveGame.cpp) | 2 callers: `SaveSlots::Read` and tests/runtime/SaveGameTests.cpp TEST; calls `ArchiveReader::Open` and `MakeError`; led to TEST-003 |
| 8 | context | ParseManifest (engine/sdk/src/Manifest.cpp) | 4 callers (LoadManifest, AiTests Parse/TEST, ManifestTests TEST); 7 callees (ParseSemVer, ParseVersionRange, ParseSections...) |
| 9 | context | Localization::MissingKeys | 0 incoming edges, although tests/loc/LocalizationTests.cpp:89-91 calls it, so method calls through object receivers are not resolved (see limitations) |
| 10 | cypher | Engine `Function`/`Method` label disjunction with `NOT EXISTS` test callers | Parser error (Kuzu label syntax); replaced by #11 |
| 11 | cypher | Engine `Method`s under `/src/` with no incoming CALLS, grouped by file | VulkanDevice.cpp 39, AssetDatabase.cpp 34, ModuleHost.cpp 34, EditorExtensions.cpp 28, SdlPlatform.cpp 24 ... (inflated by unresolved receiver calls; used only to choose where to read) |

## Analysis limitations
- Tests were not run (analysis-only rules), so there are no pass/fail, timing or coverage numbers. Flakiness findings (TEST-010) are inferred from the code.
- The GitNexus BM25 index had not been built, so `query` results were dominated by vendored code. Processes and large clusters are all third-party (SDL3, Tracy, hidapi).
- The graph does not resolve method calls on object receivers from tests: `Localization::MissingKeys` shows 0 callers but is called in `LocalizationTests.cpp`. The planned "production symbols with no test caller" Cypher ranking was therefore unreliable. The untested-unit inventory was rebuilt with file-level counts (source lines and `TEST` counts per `engine/<module>` against `tests/<module>`) and targeted greps, then confirmed by reading the code.
- `modules/` (Jolt Physics Engine, Destruction Module) is gitignored and unindexed and was not analysed.
- The scans for assertion-free and weak-assertion tests were line-based heuristics (awk over `TEST` bodies). Every test cited by name was read.

## Cross-discipline notes
- Reliability/Data integrity: `NativeFileSystem::WriteFile` truncates in place (`engine/platform/src/FileSystem.cpp:100`). It has no write-to-temp-then-rename, so a failed save destroys the previous slot.
- Code quality: the hashed-container read/write pattern is implemented separately for assets (`engine/assets/src/AssetIo.h`), meshes, saves (`engine/runtime/src/SaveGame.cpp:48-93`), cooked data and NavMesh.
- CI/Build: no preset or CI job builds `INTERI_SHIPPING=ON`, so the shipping configuration is not even compiled in CI apart from one TU.
