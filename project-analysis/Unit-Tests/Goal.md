# Unit Tests — Goal State

## Vision
Every Result-returning boundary in Interi has tests for success, boundary and failure cases. The tests prove that the system reports the right error and stays in a valid state, including when the disk or the GPU fails. Each test asserts the behaviour its name describes, with a check that a realistic one-line mutation would break. Concurrency tests run under TSan on every PR and are paced by observable conditions, not wall-clock guesses. Shared test helpers live in `interi_testing`, and duplicated per-format checks are parameterized.

## Acceptance criteria
- [ ] AC-1 `interi_testing` (or `tests/platform`) provides a fault-injecting `IFileSystem`, and tests assert graceful `IoError` handling for `EditorDocument::Save`/`Autosave`, `AssetDatabase` cooking, prefab apply, `SaveSlots::Write` and `NativeFileSystem::WriteFile`. `grep -rn "ErrorCode::IoError" tests` returns at least 7 assertions. (resolves TEST-001)
- [ ] AC-2 `.github/workflows/ci.yml` runs the `linux-clang-tsan` preset on `pull_request` events (at least `-L "unit|stress"`). (resolves TEST-002)
- [ ] AC-3 `SaveGame` tests assert `!has_value()` plus a `Corrupt`/`VersionMismatch` code at every flipped byte, and cover unsorted, duplicate, empty-id and over-4096 sections. (resolves TEST-003)
- [ ] AC-4 The four tests named in TEST-004 assert the behaviour in their names (clamped values, a real reflection round trip, a position-dependent outcome, the resolved behaviour id), or are renamed to match their bodies. (resolves TEST-004)
- [ ] AC-5 The parser error tables (HUD, Value JSON and schema, string tables, primitives) assert an error code or message fragment per case, and every `Invalid(...)` site in `engine/hud/src/HudDocument.cpp` has a case. (resolves TEST-005)
- [ ] AC-6 Each `ReflectSpirv` error branch has a CPU-only test that runs on Windows CI, and each bindless table-full path has a `VulkanTest`. (resolves TEST-006)
- [ ] AC-7 A deterministic test fails when the `ownDequeOnly` cap in `JobSystem::WaitFor` is disabled. (resolves TEST-007)
- [ ] AC-8 `GameSession` tests cover a newer-version slot (run unchanged) and a failing section restore, with behaviour that matches the CLAUDE.md wording. (resolves TEST-008)
- [ ] AC-9 A FileWatcher test detects a same-size change via an explicitly set mtime. (resolves TEST-009)
- [ ] AC-10 `JobSystem.BurstWhileOneWorkerSearches_WakesEveryWorker` contains no `sleep_for` and passes 200 repetitions under TSan. (resolves TEST-010)
- [ ] AC-11 `AsyncFileReader.DestroyWithRequestsInFlight_Drains` contains an assertion that fails in a non-sanitizer build when the destructor stops draining. (resolves TEST-011)
- [ ] AC-12 `Workers`/`Bytes` helpers are defined once in `interi_testing`, and every hashed-container implementation has one every-byte corruption test. (resolves TEST-012)
- [ ] AC-13 A shipping (`INTERI_SHIPPING=ON`) preset runs `-L unit` at least nightly, and the assert-dependent tests have asserts-off siblings or documented exemptions. (resolves TEST-013)
- [ ] AC-14 `tests/editor_ui/ValueWidgetsTests.cpp` covers read-only suppression, type mismatch and list add/remove. (resolves TEST-014)
- [ ] AC-15 (optional) The coverage job enforces an agreed floor or a diff-coverage gate. (addresses TEST-015)

## Guardrails to keep it that way
- Review checklist: a new `EXPECT_FALSE(x.has_value())` comes with an `error().code` or message check. A test name's "Condition_ExpectedResult" must be asserted literally.
- Every new `Result`-returning file or I/O path gets a fault-injection test that uses the failing `IFileSystem`.
- TSan is a required PR check. The `pending` gate in CI stays as it is.
- Periodically run a mutation spot-check (by hand, or with a tool such as `mull` for Clang) on the parsers, `SaveGame`, `JobSystem::WaitFor` and `FileWatcher`.
- Add new public headers to `HeaderCheck.cpp` and new helpers to `interi_testing`, never as copies in test files.

## Out of scope
- Running GPU tests on Windows CI runners (no Vulkan driver). Only CPU-only backend logic is required to run there.
- Fixing product behaviour that the new failure tests may reveal (for example atomic file writes). That belongs to the owning discipline. The tests here only pin the behaviour.
- Raising coverage for its own sake, or testing the vendored third-party code under `external/` and the unindexed `modules/`.
