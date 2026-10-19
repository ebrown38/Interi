# Refactoring — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi)
**Analyzed:** 2026-10-19
**Scope:** First-party C++ under `engine/` (375 files), `tools/` (editor, module_check) and the matching `tests/<module>/` suites, used to find safety nets. I looked at god classes (method counts from the graph), long functions (line spans from the graph), duplication across parsers and binary containers, dead-code markers, path and id validation helpers, guardrails in `.clang-tidy`, the `HeaderCheck.cpp` lists, git churn and import cycles. Excluded: `external/`, `build/`, `cmake-build-*/`, `modules/` (not indexed, gitignored), `project-analysis/`.
**Summary:** 11 issues — Critical: 0 · High: 1 · Medium: 5 · Low: 4 · Info: 1

## Findings

### REF-001 — `EditorApp` is an untested god class and the highest-churn file
- **Severity:** High
- **Confidence:** High
- **Category:** Large class / testability
- **Location:** `tools/editor/EditorApp.h:72-258`, `tools/editor/EditorApp.cpp` (1652 lines), `tools/editor/CMakeLists.txt:2-4`
- **Evidence:**
  - The graph (`cypher` HAS_METHOD) gives `EditorApp` 44 methods, the most of any first-party class. The header declares about 75 data members, which cover project and module loading, the Module Manager (`m_moduleManager`, `m_trust`, `m_moduleModel`, `m_moduleGraph`, `m_moduleConflicts`, `m_libraryWatcher`), the asset database and browser, play-in-editor, the gizmo drag state, the HUD designer (`HudState`), the chain editor (`ChainState`), preferences and autosave, and the mesh cache (`EditorApp.h:166-257`).
  - Long methods: `Frame` is 164 lines (`EditorApp.cpp:1485-1649`), `HandleViewportInput` 104 (`:381`), `HandleModeling` 87 (`:796`), the constructor 86 (`:82`), `HandleModuleManager` 85 (`:1344`) and `OpenProjectFolder` 80 (`:601`).
  - Churn: `EditorApp.cpp` and `EditorApp.h` are the top two files in `git log` (18 and 15 of 184 commits).
  - Safety net: none. `tools/editor/CMakeLists.txt:3` says "CI builds it but does not run it". No test includes `EditorApp.h`; the only mention is a comment in `tests/editor_ui/ChainEditorTests.cpp:388`.
  - The class holds non-UI logic that other code needs too: project resolution, reading the data file with `std::ifstream` and computing addable modules (`EditorApp.cpp:601-681`). It also re-resolves the module graph for the Module Manager (`:1235`).
- **Impact:** Every editor milestone (M12, M21–M24) has grown this class. Logic in it can't be unit-tested, because it needs an `SdlPlatform`, an `IWindow` and the Vulkan renderer. Regressions in project opening, module reload or play-stop handling show up only through manual or smoke runs. This is the main obstacle to further editor work.

### REF-002 — Project data loading is duplicated three times, and the copies have drifted
- **Severity:** Medium
- **Confidence:** High
- **Category:** Duplication / divergent change
- **Location:** `tools/editor/EditorApp.cpp:205-232`, `tools/editor/EditorApp.cpp:616-656`, `engine/game/src/ProjectModules.cpp:52-64`
- **Evidence:** All three copies run the same sequence: resolve the graph or host, open the data file with `std::ifstream`, read it into a string and call `values::LoadProjectData`. They handle the result in three different ways:
  - `EditorApp::OpenProject(desc)` logs the error and logs every warning (`:223-231`).
  - `EditorApp::OpenProjectFolder` logs the error and **silently drops the returned warnings** (`:653-656`: `if (auto loaded = ...; !loaded)` and nothing else).
  - `game::ProjectModules::Open` returns the error and keeps the warnings (`ProjectModules.cpp:58-63`).

  None of the copies goes through `IFileSystem`, although the engine routes file I/O through it elsewhere.
- **Impact:** A project opened by folder (the usual path) hides the migration warnings from project data (CLAUDE.md: "another version migrates with warnings"). Any change to the loading policy has to be made in three places. The editor copies are untestable (REF-001).

### REF-003 — The hashed binary container is implemented four times outside its shared helper
- **Severity:** Medium
- **Confidence:** High
- **Category:** Duplication
- **Location:** `engine/runtime/src/SaveGame.cpp:50-93`, `engine/scene/src/Mesh.cpp:143-176`, `engine/values/src/ProjectData.cpp:574-605`, `engine/nav/src/NavMesh.cpp:286-319`, `engine/assets/src/AssetIo.h:21-55`
- **Evidence:** `assets::detail::WriteHashed` / `ReadHashed` (`AssetIo.h`, private to `interi::assets`) already implement the container: payload archive, `HashBytes`, an outer archive holding `(hash, payload)`, a hash check returning `Corrupt`, then the inner reader and `Finish()`. Four other modules copy that body line for line. Only the magic constants and the error string differ ("save payload hash mismatch", "mesh payload hash mismatch", "cooked data payload hash mismatch", "navigation mesh payload hash mismatch"). `impact ReadHashed` resolves no callers (UNKNOWN), and `query` found no shared core-level helper.
- **Impact:** Five copies of the format for ISAV, IMSH, IVAL, `.inav` and the asset files. A change to the container (a hash upgrade, a size cap before allocating the payload, a different error code) has to be repeated five times, and a missed copy changes behaviour silently. The rule "every cooked file is the container ... read all-or-nothing" is enforced only by copy-paste.

### REF-004 — JSON parsing helpers are re-implemented per parser with inconsistent semantics
- **Severity:** Medium
- **Confidence:** High
- **Category:** Duplication / inconsistent conventions
- **Location:** `engine/chain/src/ChainAsset.cpp:34-55`, `engine/values/src/JsonSchema.cpp:168-179`, `engine/sdk/src/Manifest.cpp:18-21,63-70`, `engine/runtime/src/CameraRig.cpp:20-42,65,170`, `engine/assets/src/Material.cpp:46-58,143`, `engine/asset_import/src/AssetDatabase.cpp:64-78`, `engine/anim/src/Animation.cpp:20,77-92`, `engine/hud/src/HudDocument.cpp:24,142,275`, `engine/platform/src/InputConfig.cpp:180,264`, `engine/scene/src/SceneFile.cpp:21,56-63,156`, `engine/sdk/src/Project.cpp:115`
- **Evidence:**
  - `std::unexpected<Error> Invalid(std::string)` is defined, identically, in 25 first-party `.cpp` files (`cypher` lists 23 in `engine/`, and `git grep` counts 25).
  - The "unknown key" warning has two named helpers (`WarnUnknownKeys` in `ChainAsset.cpp:34` and in `JsonSchema.cpp:168`) plus about 9 inline loops. Their messages use four spellings: `unknown key '{}'`, `unknown key "{}" ignored`, `unknown field "{}" ignored` and `unknown field "{}"`.
  - `GetString` exists three times (`ChainAsset.cpp:45`, `Manifest.cpp:63`, `JsonSchema.cpp:64`). The `Manifest.cpp` copy dereferences `object.find(key)` without checking for `end()` (`:65-66`). It is safe today only because each of its ~20 callers tests `contains(key)` first: an unchecked precondition and a double lookup.
  - `ReadFloat` exists three times with three contracts. `CameraRig.cpp:25` errors on non-finite values. `Material.cpp:46` errors when the key is present and not a number. `AssetDatabase.cpp:72` silently ignores a bad value.
  - `ReadVec3` exists twice (`CameraRig.cpp:34`, `Animation.cpp:77`), and neither checks finiteness.
  - `Finite(Vec3)` is defined in 5 files (`Animation.cpp:25`, `ModelProcess.cpp:59`, `Skeleton.cpp:26`, `Mesh.cpp:20`, and `Skeleton.cpp:31,36` for overloads).
- **Impact:** CLAUDE.md states one rule for every parser ("unknown fields warn, anything malformed is an error"), but each parser implements it on its own, so warnings and accepted inputs differ between file types. A new data format (M25+) will copy one of these variants. The `GetString` precondition is a latent end-iterator dereference for the next caller.

### REF-005 — Duplicated path validators with divergent rules
- **Severity:** Medium
- **Confidence:** High
- **Category:** Duplication / divergent change
- **Location:** `engine/sdk/src/Manifest.cpp:119-123`, `engine/sdk/src/Manifest.cpp:461-465`, `engine/sdk/src/Project.cpp:23-40`, `engine/asset_import/src/ImportUtil.h:30`
- **Evidence:**
  - `IsModuleRelativePath` (`Manifest.cpp:119`) rejects empty paths, a leading `/`, any `..` substring, `:` and `\`.
  - Further down the **same file**, the template `path` check (`:462-463`) inlines the identical expression instead of calling it.
  - `IsProjectPath` (`Project.cpp:23`) applies a different rule (segment-based `..` handling).
  - Asset import has a third resolver (`ResolveReference`), and `VirtualFileSystem` has its own normalisation.
- **Impact:** These are trust-boundary checks for module and project files. Duplicated, slightly different validators drift: a fix to one, such as rejecting drive letters, empty segments or NUL bytes, misses the others. It is also unclear which rule is canonical.

### REF-006 — Very long functions in core pipelines (Split Phase candidates)
- **Severity:** Medium
- **Confidence:** High
- **Category:** Long function
- **Location:**
  - `engine/editor_ui/src/ChainEditor.cpp:308-839` (`DrawCanvas`, 531 lines)
  - `engine/asset_import/src/ModelProcess.cpp:202-567` (`BuildMesh`, 365)
  - `engine/render/src/ForwardRenderer.cpp:234-573` (`AddPasses`, 339)
  - `engine/sdk/src/ModuleGraph.cpp:314-619` (`ResolveModuleGraph`, 305)
  - `engine/chain/src/Executor.cpp:568-827` (`ChainExecutor::Step`, 259)
  - `engine/chain/src/Expression.cpp:154-367` (`Lex`, 214)
  - `engine/asset_import/src/GltfImport.cpp:578-787` (`ImportPrimitive`, 209)
  - `engine/values/src/ProjectData.cpp:220-421` (`LoadProjectData`, 201)
  - `engine/sdk/src/SemVer.cpp:283-482` (`ParseVersionRange`, 199)
  - `engine/hotreload/src/ModuleHost.cpp:566-716` (`ModuleHost::Execute`, 150)
- **Evidence:** The line spans come from the graph (`cypher` over Function and Method nodes). These are 3 to 9 times the ~60-line threshold. Each function already marks its phases with comments, which are the natural seams:
  - `BuildMesh` has six numbered steps ("1. Place the corners" ... "6. meshoptimizer").
  - `AddPasses` has six passes (scene data, culling, Hi-Z, light culling, shadows, shading).
  - `ResolveModuleGraph` has seven phases (included set, per-module checks, priority order, Physics Engine, load edges, Kahn).
  - `DrawCanvas` has ten (zoom/pan, hit test, clicks, keyboard, groups, links, nodes, drop, menus).

  `impact DrawCanvas`: 1 direct caller, LOW risk, inside the editor `facade` process. `context BuildMesh`: callers in the OBJ and FBX importers; the glTF caller is not resolved by the index.
- **Impact:** Every phase shares one scope, so locals leak between phases. The phases can't be tested on their own, and review diffs are large. These are the areas that each milestone extends (renderer passes, module graph rules, chain semantics). Unlike REF-001, all of them have strong safety nets: golden images, `tests/sdk`, the chain cross-platform golden hash, `tests/asset_import` fixtures and fuzzers, and headless `tests/editor_ui/ChainEditorTests.cpp`.

### REF-007 — No complexity guardrail in clang-tidy
- **Severity:** Low
- **Confidence:** High
- **Category:** Tooling guardrail
- **Location:** `.clang-tidy:4-21`
- **Evidence:** The enabled check set covers `bugprone-*`, `modernize-*`, `performance-*` and a few `readability-*` checks. Neither `readability-function-cognitive-complexity` nor `readability-function-size` is on, and `WarningsAsErrors: '*'` would make either one blocking.
- **Impact:** Nothing stops REF-001 and REF-006 from growing back, and function size isn't visible in review.

### REF-008 — `Expression.cpp` mixes lexer, parser/type checker and evaluator in one 1509-line file
- **Severity:** Low
- **Confidence:** High
- **Category:** Large module / cohesion
- **Location:** `engine/chain/src/Expression.cpp:154` (`Lex`), `:450-1142` (`class Parser`, 24 methods per the graph, including typing such as `Combine` `:572` and `Builtin` `:953`), `:1143-1486` (`class Evaluator`)
- **Evidence:** There are three distinct phases in one translation unit. The Parser class is about 700 lines and does both parsing and type checking.
- **Impact:** This file is pinned by `ChainDeterminism.MatchesCrossPlatformGoldenHash`, so every edit carries determinism risk. Splitting the phases into separate files would make typing changes and evaluation changes easier to review on their own. The behaviour is stable, so the priority is low.

### REF-009 — Local ids (`<module>:<name>`) are raw strings parsed ad hoc
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Primitive obsession
- **Location:** `engine/sdk/src/Manifest.cpp:428,527,534`, `engine/runtime/src/SaveGame.cpp:110`, `engine/sdk/src/ContributionRegistry.cpp:156`, `engine/values/src/DataTables.cpp:146`, `engine/values/src/JsonSchema.cpp:206`, `engine/values/src/ProjectData.cpp:355`, `engine/editor_ui/src/DataPanels.cpp:30`
- **Evidence:** `sdk::IsLocalId` and `OwnerOf` exist (`Manifest.cpp:525-535`). Even so, at least 9 sites split or test ids with `find(':')` directly. `Manifest.cpp:428` re-derives the owner with `target.substr(0, target.find(':'))` instead of calling `OwnerOf`.
- **Impact:** The rules for ids (shared vs local, owner, item name) appear in many places as string tricks. If the id grammar changes (for example a namespace separator or versioned ids), every site has to be found by hand.

### REF-010 — Hand-maintained `HeaderCheck.cpp` lists (shotgun surgery)
- **Severity:** Low
- **Confidence:** High
- **Category:** Shotgun surgery / build hygiene
- **Location:** `engine/editor/src/HeaderCheck.cpp:1-31` (and 22 sibling files, 359 lines total)
- **Evidence:** Every public header must be added by hand to its module's `HeaderCheck.cpp` (CLAUDE.md: "Add new public headers there"). `engine/editor/src/HeaderCheck.cpp` is the third most-changed file in history (15 commits). A comparison script found three headers that are not listed and are covered only transitively: `interi/core/Profiling.h`, `interi/editor_ui/EditorExtensionsApi.h` and `interi/hotreload/ModuleService.h`. `platform_sdl` and `rhi_vulkan` have no `HeaderCheck.cpp`.
- **Impact:** Adding a header costs a manual edit, and coverage depends on reviewer discipline. If a transitive include is removed later, a header-only template drops out of lint and warnings coverage without anyone noticing.

### REF-011 — `IDevice` changes require edits in three places in lockstep (documented)
- **Severity:** Info
- **Confidence:** High
- **Category:** Shotgun surgery (accepted)
- **Location:** `engine/rhi/include/interi/rhi/Device.h` (`IDevice` 35 methods, `ICommandList` 23 per the graph), `tests/rhi/MockDevice.h`, `tests/rhi/NullDevice.h`
- **Evidence:** CLAUDE.md requires "Every new `IDevice`/`ICommandList` virtual needs a `MOCK_METHOD` and a `NullDevice` override in the same change". The compiler enforces the `NullDevice` part (pure virtuals) but not the mock.
- **Impact:** None today. This note records the coupling as accepted. No action needed.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD; BM25 index unavailable, ~37.9k files mostly third-party)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | Community by symbolCount LIMIT 30 | Top 30 all third-party (SDL Joystick 355, hidapi, Tracy, Manifold); no first-party cluster in top 30 |
| 3 | cypher | Process LIMIT 30 | 30 processes, all SDL3 examples/video backends; no first-party flows in the first page |
| 4 | query | "parse JSON unknown field warning malformed error" | ParseScene, ParseMaterial, ParseProject, ParseManifest, InputConfig ParseAxis/ApplyInputConfig, led to REF-004 |
| 5 | query | "read hashed container magic version payload hash" | assets AssetIo.h WriteHashed/ReadHashed, runtime WriteSave/ReadSave, ArchiveReader, led to REF-003 |
| 6 | query | "editor app frame draw menus panels" | tools/editor EditorApp (class 72-258), EditorExtensions, PanelDef, led to REF-001 |
| 7 | cypher | HAS_METHOD count, engine/ + tools/ | EditorApp 44, VulkanDevice 41, IDevice 35, ModuleHost 32, InputMapper 32, ChainExecutor 28, Parser 24 ... |
| 8 | cypher | Function line spans in engine/tools .cpp | DrawCanvas 531, BuildMesh 365, ResolveModuleGraph 305, Lex 214, LoadProjectData 201, ParseVersionRange 199 ... |
| 9 | cypher | Method line spans in engine/tools .cpp | ForwardRenderer::AddPasses 339, Executor::Step 259, ImportPrimitive 209, EditorApp::Frame 164, ModuleHost::Execute 150 |
| 10 | context | EditorApp (Class) | Imported by EditorApp.cpp and main.cpp only; 30+ has_method edges listed; no processes |
| 11 | impact | ReadHashed upstream d2 | Ambiguous (function + template); 0 callers resolved, risk UNKNOWN (template use not indexed) |
| 12 | impact | ResolveModuleGraph upstream d2 (summary) | Ambiguous decl/def; definition 1 direct caller (CanAddModule), LOW; text search found 10 call sites |
| 13 | context | BuildMesh (ModelProcess.cpp) | Callers: ObjImport reader lambda, FbxImporter::ImportMeshes; glTF caller unresolved |
| 14 | impact | DrawCanvas upstream d3 (summary) | 3 impacted, LOW, 1 process (EditorApp facade), module Editor |
| 15 | cypher | Helper functions by name (Invalid, GetString, ReadFloat, ReadVec3, WarnUnknownKeys ...) | Invalid in 23 engine files; GetString ×3; ReadFloat ×3; ReadVec3 ×2; WarnUnknownKeys ×2 |
| 16 | cypher | Zero-caller engine src functions with no public declaration | 92 rows, nearly all lambdas or locals the index can't attribute; no confirmed dead code |
| 17 | cypher | Callers of ResolveModuleGraph / LoadProjectData / LoadProjectManifests | 3 rows only (incomplete); confirmed by git grep, led to REF-002 |
| 18 | check | cycles | 3 cycles in 2 components, all in external/ (Manifold wasm, SDL cocoa); 0 first-party cycles |

## Analysis limitations
- The BM25 full-text index failed, so `query` results came from vector search only and were dominated by third-party hits. I used Cypher with `filePath STARTS WITH 'engine/'` filters and `git grep` for literal duplication.
- The call graph is incomplete for templates, lambdas and some cross-file calls (for example `ReadHashed` has 0 resolved callers, and `ResolveModuleGraph` showed 1 call edge where text search found 10). Caller counts quoted here are lower bounds.
- Line spans from the graph measure length, not cyclomatic complexity. No complexity tool (lizard, clang-tidy cognitive complexity) was run, because builds and new tool runs are out of scope.
- Git history is squashed roughly per milestone (184 commits), so churn counts reflect how many milestones touched a file rather than individual edits.
- No dead code was confirmed: there are no `#if 0`, `TODO` or `FIXME` markers in `engine/`, `tools/`, `testing/` or `samples/`, and the graph's zero-caller list is noise from lambdas. Unused public API across the shared libraries was not checked, because modules outside the repo may call it.
- `modules/` (Physics Engine, Destruction Module) is not indexed and was not analysed.

## Cross-discipline notes
- Security: the path validators in REF-005 (`Manifest.cpp:119`, `:462`, `Project.cpp:23`) are trust-boundary checks with divergent rules; worth a dedicated review.
- Unit-Tests: `level_editor` / `EditorApp` has no automated test, not even a headless `--frames` smoke run in CTest (`tools/editor/CMakeLists.txt:3`).
- Program Architecture: the `interi::assets::detail` hashed container (`AssetIo.h`) belongs in `interi::core` next to `Archive.h` (REF-003).
- Program Architecture: the editor's project-loading path (`EditorApp.cpp:601-681`) duplicates `game::ProjectModules`, and project data I/O bypasses `IFileSystem` (REF-002).
