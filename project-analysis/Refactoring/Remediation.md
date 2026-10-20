# Refactoring — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

Ground rule for every step: no behaviour change and no new features. One refactoring per commit, with the full preset matrix (debug, release, asan, tsan, tidy) green before and after. Run `impact` (GitNexus) on each symbol before you touch it, as CLAUDE.md requires.

## Recommended order
1. **REF-007** (Low, S) first. Add the complexity checks in warning-only mode, or with thresholds above today's worst offenders, so the baseline is measured and can't get worse while the rest proceeds.
2. **REF-003, REF-004, REF-005**: cheap, mechanical consolidations, each protected by existing tests. They also remove the helpers that later extractions would otherwise copy again.
3. **REF-002**, then **REF-001**: extract project loading into a tested engine type first, then use it as the first slice carved out of `EditorApp`.
4. **REF-006**, then **REF-008**: Split Phase on the long functions, one function per PR, in churn order (`ResolveModuleGraph`, `AddPasses`, `Executor::Step`, `BuildMesh`, `DrawCanvas`, and so on).
5. **REF-009, REF-010**: opportunistic, low risk.
6. Tighten REF-007's thresholds to the target values once step 4 is done.

## REF-001 — `EditorApp` is an untested god class and the highest-churn file
- **Severity / Effort:** High / XL (incremental; each slice is M)
- **Depends on:** REF-002 (first slice), Unit-Tests discipline (a smoke test)
- **Steps:**
  1. **Safety net first.**
     - Add a CTest smoke test (label `integration`, skipped without a Vulkan device, like the GPU tests) that runs `level_editor --video-driver offscreen --frames 30 --screenshot <tmp>`. Add a second one with `--project-file samples/modules/starter/templates/coins --play 1` and check that the output contains `Played`.
     - This characterises today's startup, project open and play paths before anything moves.
  2. **Extract Class `EditorProjectSession`** into `interi::editor` (headless, testable). It owns `m_projectDef`, `m_projectManifests`, `m_project`, `m_projectDataPath`, `m_physicsEngine`, `m_addableModules` and `m_templates`, and the logic of `OpenProjectFolder`, `OpenProject(desc)`, `SaveProjectData` and `ApplyProjectChange`. It takes an `IFileSystem&` (see REF-002). `EditorApp` keeps only the UI glue: mounting, window title, recent projects.
  3. **Extract Class `ModuleManagerController`**, covering `m_moduleManager`, `m_moduleRequests`, `m_trust`, `m_moduleModel`, `m_moduleGraph`, `m_moduleConflicts`, `m_resolvedGraph`, `m_libraryWatcher`, `m_moduleViewDirty`, plus `DrawModuleManager`, `HandleModuleManager`, `RefreshModuleManager` and `RunModuleLoop`. Its non-ImGui part goes to `interi::editor`, next to `ModuleManager.h`. Test it headless with `state.Queue(action)`, which already exists.
  4. **Move `HudState` and `ChainState` with their draw methods** into small window classes in `editor_ui` (`HudDesignerWindow`, `ChainEditorWindow`). Each owns its undo stack, path and message.
  5. **Extract `ViewportController`**, covering the gizmo drag state (`m_drag`, `m_dragTarget`, `m_dragStart`, `m_dragKey`, `m_hoveredAxis`), `HandleViewportInput`, `ApplyDrag`, `DropAsset` and `PlacementPoint`. Its pure parts (ray, placement, drag maths) are unit-testable with `EditorDocument`.
  6. **Split Phase on `Frame()`**: poll and feed input, begin the ImGui frame, draw panels, handle the requests gathered this frame, render and present. Each phase becomes a private method of 30 lines or fewer.
  7. Stop when `EditorApp.cpp` is below ~600 lines and the class has 20 or fewer methods. What remains is composition and wiring.
- **Illustrative change (not applied):**
  ```cpp
  // before: EditorApp owns ~75 members across 8 responsibilities
  // after:
  class EditorApp
  {
      EditorProjectSession m_session;       // interi::editor, unit-tested
      ModuleManagerController m_modules;    // interi::editor + editor_ui
      ViewportController m_viewport;
      ChainEditorWindow m_chainWindow;
      HudDesignerWindow m_hudWindow;
      // ...renderer, platform glue only
  };
  ```
- **Verification:**
  - The new smoke tests pass before and after every slice.
  - New headless tests cover `EditorProjectSession` (open the starter project, check the warnings surface, refuse a broken project) and `ModuleManagerController`.
  - `wc -l tools/editor/EditorApp.cpp` is below 600, and `cypher` HAS_METHOD shows 20 or fewer methods for `EditorApp`.
- **Risks / trade-offs:**
  - Member destruction order matters: `m_extensions` must outlive `m_modules` (`EditorApp.h:225`). Keep that order inside the new owners and comment it.
  - Don't move ImGui calls into `interi::editor`.

## REF-002 — Project data loading is duplicated three times, and the copies have drifted
- **Severity / Effort:** Medium / M
- **Depends on:** None (it enables REF-001 step 2)
- **Steps:**
  1. Add a characterization test in `tests/values` or `tests/game`: load a project data file that produces migration warnings through `game::ProjectModules::Open`, and check that `Warnings()` is non-empty. This pins today's game behaviour.
  2. Add a single helper, for example `Result<std::vector<std::string>> values::LoadProjectDataFile(const IFileSystem&, std::string_view path, DataRegistry&)`. It returns an empty list when the file is absent (both current behaviours) and the warnings otherwise.
  3. Replace the three copies (`EditorApp.cpp:218-231`, `:646-656`, `ProjectModules.cpp:52-64`). Keep each caller's error policy explicit: the game fails, the editor logs. Make the editor log the warnings on **both** paths. That changes `OpenProjectFolder` deliberately, so land it as a separate, labelled bug-fix commit after the pure refactor.
- **Illustrative change (not applied):**
  ```cpp
  INTERI_TRY_ASSIGN(warnings, values::LoadProjectDataFile(files, project.data, host.Data()));
  ```
- **Verification:** The characterization test and the `GameSession` / starter sample tests (`interi_starter_sample_tests`) pass. `git grep -n 'LoadProjectData(' -- engine tools` shows only the helper and its tests.
- **Risks / trade-offs:** `IFileSystem` paths are virtual and normalised. The editor's `m_projectDataPath` is a native path, so mount it or use `NativeFileSystem` on its parent.

## REF-003 — The hashed binary container is implemented four times outside its shared helper
- **Severity / Effort:** Medium / S–M
- **Depends on:** None
- **Steps:**
  1. Move `WriteHashed` / `ReadHashed` from `engine/assets/src/AssetIo.h` into a public core header, for example `engine/core/include/interi/core/HashedArchive.h`, next to `Archive.h`. Add the header to `engine/core/src/HeaderCheck.cpp` and instantiate it there.
  2. Add an overload or a visitor form for multi-field payloads (`SaveGame` writes `label, sections`; `NavMesh` writes `settings, data`), or wrap those fields in a small struct with an ADL `Serialize`.
  3. Replace the bodies of `WriteSave`/`ReadSave`, `WriteMeshAsset`/`ReadMeshAsset`, `WriteCookedData`/`ReadCookedData` and `NavMesh::Write`/`Read`. Keep each caller's post-read validation (`kMaxSections` and sort check, `ValidateMesh`, `ValidSettings`).
  4. Keep the exact error strings by passing `what` ("save", "mesh", "cooked data", "navigation mesh"), so messages are unchanged.
- **Illustrative change (not applied):**
  ```cpp
  Result<TriangleMesh> ReadMeshAsset(std::span<const std::byte> data)
  {
      INTERI_TRY_ASSIGN(mesh, ReadHashed<TriangleMesh>(data, kMeshMagic, kPayloadMagic, kMeshVersion, "mesh"));
      INTERI_TRY(ValidateMesh(mesh));
      return mesh;
  }
  ```
- **Verification:**
  - Existing tests stay green: `tests/runtime/SaveGameTests.cpp`, `tests/scene` mesh tests, `tests/values/ProjectDataTests.cpp`, `tests/nav/NavMeshTests.cpp` and the asset fuzzers.
  - Byte-identical output: hash `Write*` results before and after with `StateHasher`.
  - `git grep -n 'payload hash mismatch' -- engine` returns one location.
- **Risks / trade-offs:** The on-disk format must not change, because the golden files and saves depend on it. The byte-identity test is the guard.

## REF-004 — JSON parsing helpers are re-implemented per parser with inconsistent semantics
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Create a private-but-shared helper header, for example `engine/core/include/interi/core/JsonRead.h`, or a `values` detail header exported to sdk, chain, scene, hud, runtime, assets, anim and platform. Pick the lowest module that already links `nlohmann_json`. It provides:
     - `Invalid(std::string)`
     - `WarnUnknownKeys(object, known, where, warnings)` with one message format
     - `RequireString`/`OptionalString`
     - `ReadFinite<float>`
     - `ReadVec3`/`ReadQuat` (finite-checked)
     - `IsFinite(Vec3/Quat/Vec4)`
  2. Migrate one parser per commit. Each parser has a test suite (25 test files call the `Parse*` functions). Where a parser's warning text changes, update its test expectations in the same commit and say so in the message.
  3. Fix the `GetString` precondition in `Manifest.cpp:63` by checking `end()` (return `Invalid("... has no ...")`). Drop the `contains()` pre-checks at the call sites only once the helper owns that message.
  4. Decide the policy for `AssetDatabase.cpp:64-78` (meta files currently ignore malformed fields on purpose, because they are regenerated). Name it explicitly, for example `ReadFloatOr(json, key, fallback)`, so the lenient variant is intentional and visible.
- **Illustrative change (not applied):**
  ```cpp
  // before (CameraRig.cpp:65, Material.cpp:143, HudDocument.cpp:275 ... each its own loop)
  json::WarnUnknownKeys(root, kRootKeys, "camera rigs", set.warnings);
  ```
- **Verification:** All parser test suites pass. `git grep -c 'std::unexpected<Error> Invalid(' -- engine` returns 1, and `git grep -n 'unknown field\|unknown key' -- 'engine/*.cpp'` returns only the helper.
- **Risks / trade-offs:** Shared libraries on Windows export functions, not data, which is fine for free functions (CLAUDE.md). Avoid adding a JSON dependency to modules that don't have one yet.

## REF-005 — Duplicated path validators with divergent rules
- **Severity / Effort:** Medium / S
- **Depends on:** Security review (which rule is canonical)
- **Steps:**
  1. Write table tests for `IsModuleRelativePath` and `IsProjectPath` covering `a..b`, `../x`, `a/../b`, `C:x`, `\\x`, `/x`, empty segments and the empty string. This records today's divergence.
  2. Replace the inline expression at `Manifest.cpp:462-463` with `IsModuleRelativePath(path)`. This is the pure refactor.
  3. With the Security discipline's decision, pick one canonical `IsContainedRelativePath(std::string_view)` in `interi::platform` (beside the virtual-path normalisation) and route manifests, projects and `ResolveReference` through it. Land behaviour changes separately from the move.
- **Verification:** The table tests pass. `git grep -n "find(\"..\")" -- engine` shows a single validator.
- **Risks / trade-offs:** Tightening a rule can reject existing module.json or project.json files. Check `samples/` and `tests/sdk/modules` fixtures.

## REF-006 — Very long functions in core pipelines (Split Phase candidates)
- **Severity / Effort:** Medium / L overall (M per function)
- **Depends on:** REF-007 (to measure), and REF-004 for the parser-heavy ones (`LoadProjectData`, `ParseVersionRange`)
- **Steps:** Apply **Split Phase / Extract Function** at the existing phase comments, one function per PR:
  1. `ResolveModuleGraph` (`ModuleGraph.cpp:314`): `CollectIncluded`, `CheckModules`, `PriorityOrder`, `PlacePhysicsEngine`, `LoadEdges`, `KahnLoadOrder`, passing a small `ResolveState` struct. Net: `tests/sdk` (ModuleGraph and PhysicsEngine tests).
  2. `ForwardRenderer::AddPasses` (`ForwardRenderer.cpp:234`): one `AddXxxPass(FrameGraph&, const FrameInputs&)` per commented pass. Net: golden images on lavapipe (`INTERI_REQUIRE_GPU`) and `ConventionTests`.
  3. `ChainExecutor::Step` (`Executor.cpp:568`) and `CollectWave0` (`:407`): split into wave evaluation and serial effect application. Net: `interi_chain_stress_tests` and the cross-platform golden hash (must stay identical).
  4. `BuildMesh` (`ModelProcess.cpp:202`): `PlaceCorners`, `WeldPositions`, `AssignSlots`, `GenerateNormals`, `GenerateTangents`, `Optimize` (the six numbered steps). Net: `tests/asset_import` fixtures and the fuzz corpus.
  5. `DrawCanvas` (`ChainEditor.cpp:308`): `HandleZoomPan`, `HitTest`, `HandleClicks`, `HandleKeyboard`, `DrawGroups`, `DrawLinks`, `DrawNodes`, `HandleDrop`, `DrawMenus` on a `CanvasFrame` struct. Net: `tests/editor_ui/ChainEditorTests.cpp` (headless ImGui).
  6. Then `Lex`, `ImportPrimitive`, `LoadProjectData`, `ParseVersionRange` and `ModuleHost::Execute` (keep the five transaction steps named as in CLAUDE.md).
- **Illustrative change (not applied):**
  ```cpp
  Result<ModuleGraph> ResolveModuleGraph(...)
  {
      ResolveState s{order, modules, host};
      INTERI_TRY(CollectIncluded(s));
      INTERI_TRY(CheckModules(s));
      INTERI_TRY(PriorityOrder(s));
      INTERI_TRY(PlacePhysicsEngine(s, pin));
      return KahnLoadOrder(s);
  }
  ```
- **Verification:** Identical test results. The chain golden hash and the render goldens are unchanged. Each refactored function is 80 lines or fewer and passes the REF-007 cognitive-complexity check.
- **Risks / trade-offs:**
  - Performance: in `AddPasses` and `Executor::Step`, keep the extracted helpers in the same TU (internal linkage) so the compiler can inline them, and benchmark with the release preset.
  - Determinism: in `Executor`, don't reorder floating-point operations or iteration order.

## REF-007 — No complexity guardrail in clang-tidy
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `readability-function-cognitive-complexity` and `readability-function-size` to `.clang-tidy`, with `CheckOptions` thresholds just above today's worst function, so the tidy preset stays green.
  2. Lower the thresholds as REF-006 lands, aiming for cognitive complexity 25 and 120 lines for engine code (UI draw functions may need a per-directory `.clang-tidy`, as `engine/rhi_vulkan` and `engine/modeling` already have).
- **Illustrative change (not applied):**
  ```yaml
  Checks: >
    ...,
    readability-function-cognitive-complexity,
    readability-function-size
  CheckOptions:
    - key: readability-function-cognitive-complexity.Threshold
      value: 25
    - key: readability-function-size.LineThreshold
      value: 120
  ```
- **Verification:** The `linux-clang-tidy` preset builds cleanly. Adding a deliberately oversized function in a scratch branch fails tidy.
- **Risks / trade-offs:** Tidy runs only on library targets, so `tools/editor` (`level_editor`, an executable) may not be covered. Check `INTERI_CLANG_TIDY` wiring for tools, or cover `EditorApp` through REF-001's extraction into libraries.

## REF-008 — `Expression.cpp` mixes lexer, parser/type checker and evaluator in one 1509-line file
- **Severity / Effort:** Low / M
- **Depends on:** REF-006 (`Lex`)
- **Steps:** Move File only, with no code edits:
  - `ExpressionLexer.cpp` (Token, `Lex`)
  - `ExpressionParser.cpp` (Parser, typing)
  - `ExpressionEval.cpp` (Evaluator)

  They share a private `ExpressionInternal.h` for `ExprNode`/`ExprProgram`/`Token`. Optionally, Extract Class `TypeChecker` out of `Parser` later.
- **Verification:** `tests/chain` and `ChainDeterminism.MatchesCrossPlatformGoldenHash` pass unchanged.
- **Risks / trade-offs:** Anonymous-namespace helpers become internal-header functions. Keep them in a `detail` namespace so they are not exported from the shared library unnecessarily.

## REF-009 — Local ids (`<module>:<name>`) are raw strings parsed ad hoc
- **Severity / Effort:** Low / S (helpers), L (strong type)
- **Depends on:** None
- **Steps:**
  1. Replace the ad hoc `find(':')` splits with `sdk::IsLocalId`, `sdk::OwnerOf` and a new `sdk::ItemNameOf`, starting with `Manifest.cpp:428` and `DataPanels.cpp:30`.
  2. Only if the id grammar is expected to change, introduce a `ContributionId` value type (owner and name views, plus a shared flag) at the `ContributionRegistry` boundary.
- **Verification:** `tests/sdk` (ContributionRegistry, Manifest) pass. `git grep -n "find(':')" -- engine` lists only the id helpers and path validators.
- **Risks / trade-offs:** A strong type touches the module ABI if it crosses `ModuleApi.h`. In that case, bump `INTERI_MODULE_ABI_VERSION`, or keep it engine-internal.

## REF-010 — Hand-maintained `HeaderCheck.cpp` lists (shotgun surgery)
- **Severity / Effort:** Low / S–M
- **Depends on:** None
- **Steps:**
  1. Short term: add the three transitively covered headers (`Profiling.h`, `EditorExtensionsApi.h`, `ModuleService.h`) to their `HeaderCheck.cpp`.
  2. Add a `license`- or `unit`-labelled CTest, in the style of the module `TEST_SOURCES` check, that fails when a public header under `engine/<m>/include` is missing from `engine/<m>/src/HeaderCheck.cpp`.
  3. Optional: generate the include list in CMake (`file(GLOB_RECURSE ... CONFIGURE_DEPENDS)` into a generated TU). Keep the hand-written part only for explicit template instantiations.
- **Verification:** The new test passes, and deleting a line from any `HeaderCheck.cpp` makes it fail.
- **Risks / trade-offs:** Globbing adds configure-time cost and hides intent. The check-only test is the lighter option.

REF-011 is Info and needs no remediation.
