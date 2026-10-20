# Refactoring — Goal State

## Vision
Interi's code is made of small units with one job each. The editor application is thin wiring over headless, unit-tested controllers in `interi::editor`. Each cross-cutting format and rule exists once and is reused everywhere: the hashed binary container, JSON field reading and its "unknown fields warn, malformed is an error" policy, path containment, and local-id parsing. The long pipeline functions (module graph resolution, renderer passes, chain execution, mesh building, the chain canvas) are expressed as named phases. clang-tidy keeps function size and complexity from creeping back. Every refactor landed as a behaviour-preserving commit with the existing determinism and golden tests unchanged.

## Acceptance criteria
- [ ] AC-1 A CTest test (label `integration`, skipped without a Vulkan device) runs `level_editor --video-driver offscreen` for a fixed number of frames. A second one opens the starter project with `--play 1` and checks for `Played`. (resolves REF-001)
- [ ] AC-2 `tools/editor/EditorApp.cpp` is under 600 lines and `EditorApp` has 20 or fewer methods (GitNexus `cypher` HAS_METHOD). Project session, Module Manager control and viewport input live in `interi::editor` classes with headless tests. (resolves REF-001)
- [ ] AC-3 Project data files are loaded through one helper that takes an `IFileSystem`. `git grep -n 'values::LoadProjectData(' -- engine tools` shows only that helper. Both editor open paths log migration warnings, and a test proves that they surface. (resolves REF-002)
- [ ] AC-4 The hashed container lives in one public core header. `git grep -n 'payload hash mismatch' -- engine` returns exactly one location, and a byte-identity test shows the ISAV, IMSH, IVAL and `.inav` outputs unchanged. (resolves REF-003)
- [ ] AC-5 `git grep -c 'std::unexpected<Error> Invalid(' -- engine` totals 1. One `WarnUnknownKeys` produces every unknown-key warning, with one message format. `Finite`, `ReadVec3` and `ReadFloat` each exist once (a lenient variant only under an explicit name). `Manifest.cpp`'s string getter checks `end()`. (resolves REF-004)
- [ ] AC-6 Manifest, project and import references are validated by one containment function covered by a table test (`..`, `a..b`, drive letters, backslashes, absolute paths, empty segments). (resolves REF-005)
- [ ] AC-7 No function in `engine/` exceeds 150 lines (GitNexus `cypher` over Function and Method `endLine - startLine`). `DrawCanvas`, `BuildMesh`, `AddPasses`, `ResolveModuleGraph`, `ChainExecutor::Step`, `Lex`, `ImportPrimitive`, `LoadProjectData`, `ParseVersionRange` and `ModuleHost::Execute` are split into named phases. `ChainDeterminism.MatchesCrossPlatformGoldenHash` and every render golden are unchanged. (resolves REF-006)
- [ ] AC-8 `.clang-tidy` enables `readability-function-cognitive-complexity` (threshold 25 or lower, with documented per-directory exceptions for ImGui draw code) and `readability-function-size`. `linux-clang-tidy` is green. (resolves REF-007)
- [ ] AC-9 `engine/chain/src/Expression.cpp` is split into lexer, parser/typing and evaluator files, none over 700 lines, with the chain golden hash unchanged. (resolves REF-008)
- [ ] AC-10 `git grep -n "find(':')" -- 'engine/*.cpp'` lists only the id helpers in `sdk` and the path validator. Every owner/name split goes through `IsLocalId`/`OwnerOf`/`ItemNameOf`. (resolves REF-009)
- [ ] AC-11 A test fails when a public header under `engine/<m>/include` is not listed in `engine/<m>/src/HeaderCheck.cpp`, and it passes today. (resolves REF-010)
- [ ] AC-12 The `IDevice`/`ICommandList` lockstep rule with `MockDevice` and `NullDevice` stays documented in CLAUDE.md. No action beyond keeping it. (records REF-011)

## Guardrails to keep it that way
- clang-tidy `readability-function-cognitive-complexity` and `readability-function-size` as errors (AC-8), in the existing tidy preset run before commits.
- The HeaderCheck completeness test (AC-11) and the editor smoke tests (AC-1) in CTest.
- Review checklist: "Does this add a second copy of a parser helper, container reader or path check?" Point reviewers at the shared headers from REF-003, REF-004 and REF-005.
- GitNexus `impact` before each refactor, and `detect_changes` before each commit (already mandated by CLAUDE.md). Refactor commits must not change any golden file or the chain golden hash.
- A periodic GitNexus `cypher` report of the top 20 functions by length and classes by method count, compared per milestone.

## Out of scope
- Behaviour changes, new features and performance tuning. Where a refactor exposes a bug (the dropped warnings in REF-002, tightening the path rules in REF-005), the fix lands as a separately labelled commit.
- Restructuring `VulkanDevice` (1796 lines, 41 methods): it is a backend implementing a wide interface, and its size follows from the API.
- Code in `modules/` (Physics Engine, Destruction Module), `external/` and generated or third-party sources.
- Module-level architecture and layering (Program Architecture discipline) and security hardening decisions (Security discipline).
