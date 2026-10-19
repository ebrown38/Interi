# Security — Goal State

## Vision
Interi treats every project folder, content file, save and module as untrusted until the user says otherwise.
- Opening a project never runs its native code, and never touches files outside it, without an explicit trust decision that is tied to the exact library bytes.
- Every runtime and import parser is bounded, validated, depth-limited and continuously fuzzed. Malformed content produces a `Result` error, never memory corruption, file deletion or a crash.
- Shipped binaries carry the platform's exploit mitigations.
- Dependencies and CI actions are pinned and scanned.

## Acceptance criteria
- [ ] AC-1: `NavMesh::Read` returns `Corrupt` for any `.inav` whose tile sizes don't match Detour's layout. A unit test with truncated, negative-count and inflated-count tiles passes under `linux-clang-asan` with no sanitizer report. (resolves SEC-001)
- [ ] AC-2: After `AssetDatabase::CookAll()` and `Prune()`, `.meta` product paths that are absolute, contain `..`, or point outside `imported/` cause no deletion. Regression tests in `tests/asset_import`, each with a valid source asset next to the crafted meta, cover all three forms. (resolves SEC-002)
- [ ] AC-3: Module shadow copies are created in a per-user directory owned by the current uid with mode 0700, using `O_EXCL|O_NOFOLLOW`. A test that pre-creates a foreign or symlinked shadow directory makes the load fail. No module file is written under the shared `/tmp`. (resolves SEC-003)
- [ ] AC-4: Opening a project that lists a module with a `library` and an empty trust store loads no library. A headless editor test asserts that `EditorModules` is null and that a trust request is pending. Changed libraries are not auto-reloaded until they are trusted again. (resolves SEC-004)
- [ ] AC-5: `TrustStore` entries record the SHA-256 of each library, and `NeedsTrust` returns true when the library hash differs even though id, author and license match. Preferences and the trust store are never written to the shared temp directory. (resolves SEC-005)
- [ ] AC-6: Under asan, each of these returns a compiler diagnostic with no crash: 10,000 nested `(`, 10,000 unary `-`, and a flat left-associative chain of 5,000 `+1` terms. The parser depth, the expression tree depth and the source length are all capped in `Expression.cpp`. (resolves SEC-006)
- [ ] AC-7: Release binaries pass `checksec` with Full RELRO, a stack canary, NX, PIE and FORTIFY on Linux, and `dumpbin /headers` shows CFG and CET compatibility on Windows. Both are produced by an `InteriHardening.cmake` applied to engine and tool targets. (resolves SEC-007)
- [ ] AC-8: Fuzz targets exist for `NavMesh::Read`, `ReadSave`/`RestoreSceneState`, `ReadCookedData`, chain and chain-function parsing and compilation, expressions, scenes and prefabs, HUD, string tables, anim JSON, module.json, project.json and project data. The nightly CI runs each target for at least 5 minutes under asan with no findings. (resolves SEC-008)
- [ ] AC-9: A dependency-vulnerability scan (OSV-Scanner or equivalent over the vcpkg SBOM) runs in CI. It reports no unwaived advisory against stb_image or any other enabled decoder or library. (resolves SEC-009, SEC-017)
- [ ] AC-10: Decoding a 1 MB archive that claims about 1M large elements keeps peak allocation below 16 MB, and `kMaxSections` is enforced before allocation. (resolves SEC-010)
- [ ] AC-11: `DecodeClip` rejects audio that declares or produces more than the configured maximum number of frames. A test covers it. (resolves SEC-011)
- [ ] AC-12: `NativeFileSystem::ReadFile` and `WriteFile` refuse paths that resolve outside the root through symlinks, `ReadFile` enforces a maximum size, and `CreateProject` doesn't dereference symlinks in templates. Tests cover all of these. (resolves SEC-012)
- [ ] AC-13: `RestoreSceneState` rejects non-finite or degenerate transforms before mutating the world. A test covers it. (resolves SEC-013)
- [ ] AC-14: On Windows, modules load with `LoadLibraryExW` and safe search flags, and the executables call `SetDefaultDllDirectories`. (resolves SEC-014)
- [ ] AC-15: The editor creates or opens no file under the shared temp directory. The ImGui layout lives in the per-user config folder. (resolves SEC-015)
- [ ] AC-16: Profile builds bind the Tracy listener to localhost unless `INTERI_PROFILING_REMOTE=ON`. (resolves SEC-016)
- [ ] AC-17: Every `uses:` in `.github/workflows/ci.yml` is pinned to a commit SHA, the container image is pinned by digest, and Dependabot updates them. (resolves SEC-017)
- [ ] AC-18: The properties recorded in SEC-018 still hold: no secrets in the repo (secret scan clean), CI `permissions: contents: read`, no `pull_request_target`, path confinement in `ResolveReference` and `project.json`, the non-throwing JSON parse, and the build command taken only from the UI. (preserves SEC-018)

## Guardrails to keep it that way
- Fuzz targets for every format reader and parser, run with fixed iterations in the PR CI and time-budgeted nightly. A new `Read*`/`Parse*`/`Load*` entry point without a fuzz target fails review. A checklist item in the PR template is enough.
- The CI dependency scan and Dependabot for vcpkg baseline bumps and GitHub Actions.
- A secret-scanning step in CI (gitleaks or GitHub secret scanning with push protection).
- A grep-based CI check that every `std::filesystem::remove`/`copy`/`ofstream` in `engine/` goes through a containment helper, or carries a reviewed `// SECURITY: path is ...` comment.
- `InteriHardening.cmake` applied in `interi_add_module`, so new modules inherit it, plus a `checksec`/`dumpbin` assertion test on the release preset.
- Security notes in `docs/module-manager.md` kept in sync with the trust behaviour, with tests pinning that opening a project doesn't load untrusted modules.

## Out of scope
- Sandboxing native modules. Trusted modules run with full trust by design; the goal is an informed, hash-bound decision.
- Anti-cheat and tamper-proofing of single-player saves. XXH3 stays an integrity check, not authentication.
- Multiplayer or network trust boundaries. No networking code exists yet; review it when it is added.
- The `modules/` projects (Physics Engine, Destruction Module) and the private console backends. They need their own reviews.
