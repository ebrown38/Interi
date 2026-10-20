# Unit Tests — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. TEST-001: add the failing-file-system double first. It unblocks the I/O failure tests and part of TEST-008.
2. TEST-002: put TSan on PRs so the concurrency work below (TEST-007, TEST-010, TEST-011) is checked on every change.
3. TEST-003, TEST-008: save integrity and the "a bad slot changes nothing" guarantee (data-loss boundary).
4. TEST-004, TEST-005, TEST-006, TEST-007: strengthen weak assertions and cover the untested risky branches.
5. TEST-009, TEST-010, TEST-011, TEST-012, TEST-013, TEST-014: low-cost hardening and clean-up.
6. TEST-015 (Info): optional coverage gate.

## TEST-001 — I/O failure paths are untested: there is no fault-injecting `IFileSystem` test double
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Add `testing/include/interi/testing/FaultyFileSystem.h` to `interi_testing`. It wraps any `IFileSystem` and fails `WriteFile` and/or `ReadFile` with `ErrorCode::IoError` by path prefix or after N successful calls. The platform module defines `IFileSystem`, so `interi_testing` needs `interi::platform` as a dependency. Alternatively, put the helper in `tests/platform/` as a header the way `FakeEventSource.h` is.
  2. `tests/editor/EditorDocumentTests.cpp`: a failed `Save` returns `IoError` and `IsDirty()` stays true; `Autosave` failure leaves no `.autosave`.
  3. `tests/asset_import/AssetDatabaseTests.cpp`: fail the 2nd product write of a model cook. Assert that the source is reported as failed, that the meta's cook key is unchanged, and that the next `Cook` with a healthy file system re-cooks and succeeds.
  4. `tests/editor/PrefabsTests.cpp`: a failed apply leaves the scene and undo stack unchanged.
  5. `tests/runtime/SaveGameTests.cpp`: `SaveSlots::Write` propagates `IoError`. `tests/game/GameSessionTests.cpp`: `SaveToSlot` propagates it.
  6. `tests/platform/FileSystemTests.cpp`: `NativeFileSystem::WriteFile` into a read-only directory returns `IoError`. Linux only; guard it with `GTEST_SKIP` when running as root.
- **Illustrative change (not applied):**
  ```cpp
  class FaultyFileSystem final : public IFileSystem {
  public:
      explicit FaultyFileSystem(IFileSystem& inner) : m_inner(inner) {}
      void FailWritesAfter(int n) { m_writesLeft = n; }
      Result<void> WriteFile(std::string_view p, std::span<const std::byte> d) override {
          if (m_writesLeft == 0) return MakeError(ErrorCode::IoError, "injected");
          if (m_writesLeft > 0) --m_writesLeft;
          return m_inner.WriteFile(p, d);
      }
      // Exists/ReadFile forward (with an optional read fault)
  private:
      IFileSystem& m_inner; int m_writesLeft = -1;
  };
  ```
- **Verification:** `ctest --preset linux-clang-debug -R 'IoError|WriteFails|SaveFails'` passes. A mutation that drops the `INTERI_TRY` before `m_undo.MarkClean()` in `EditorDocument::Save` makes the new test fail.
- **Risks / trade-offs:** The tests may reveal real non-graceful behaviour (for example the truncating native write), which is a product fix for another discipline. Record such cases as `pending` tests rather than deleting them.

## TEST-002 — TSan runs only nightly, so concurrency regressions can merge to `main`
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add `linux-clang-tsan` to the `linux` job matrix in `.github/workflows/ci.yml:48`, at least for `pull_request` events. If that costs too much, restrict it to `-L "unit|stress"` on PRs and keep the full run nightly.
  2. Keep the vcpkg cache key per triplet so the TSan dependency build is cached.
- **Verification:** A PR shows a `linux (linux-clang-tsan)` check. Introduce a deliberate race on a scratch branch and confirm the PR goes red.
- **Risks / trade-offs:** This adds CI minutes. A lavapipe false positive under TSan should be handled with the existing suppressions in `tests/rhi_vulkan/VulkanTestSupport.cpp`.

## TEST-003 — The SaveGame corruption test cannot fail for "corrupt but accepted", and the `ReadSave` validation branches are untested
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `Read_FlippedByte_IsCorrupt_AtEveryPosition`, assert `!read.has_value()` for every position and that `read.error().code` is one of `{Corrupt, VersionMismatch}`.
  2. Add `ReadSave_UnsortedOrDuplicateSections_Corrupt`: build a `SaveGame` whose `sections` vector is set directly (bypassing `Set`) as `{"b:x","a:x"}`, then `{"a:x","a:x"}`, then `{""}`. `WriteSave` serializes the sections as given and does not validate, so the bytes it returns are a well-hashed but invalid save; pass them to `ReadSave` and assert it fails with `ErrorCode::Corrupt`.
  3. Add `ReadSave_TooManySections_Corrupt`: serialize a `SaveGame` with 4097 sorted, unique sections through `WriteSave`, then assert `ReadSave` of those bytes fails with `ErrorCode::Corrupt`.
  4. In `Read_TruncatedOrEmpty_Fails`, assert the error code.
- **Illustrative change (not applied):**
  ```cpp
  const auto read = ReadSave(bad);
  ASSERT_FALSE(read.has_value()) << "byte " << i;
  EXPECT_TRUE(read.error().code == ErrorCode::Corrupt || read.error().code == ErrorCode::VersionMismatch) << i;
  ```
- **Verification:** Commenting out the sorted/unique loop in `engine/runtime/src/SaveGame.cpp:85-90` makes the new test fail.
- **Risks / trade-offs:** If some header flip legitimately decodes (for example an unused padding byte), the stricter assertion fails. Document such a byte explicitly in the test instead of loosening the assertion.

## TEST-004 — Several tests assert less than their names claim
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. `GltfImport.OutOfRangeMaterialFactors_AreClamped`: add `EXPECT_EQ(model->materials[0].metallic, 1.0f); EXPECT_EQ(model->materials[0].roughness, 0.0f);`.
  2. `TriggerTest.Reflection_RoundTripsComponents`: write a component with non-default fields through reflection, read it back, and compare. Alternatively, rename the test to `Reflection_RegistersComponents`.
  3. `CanAddModule.AtPosition_ChangesPriority`: build a scenario where the position decides acceptance (for example a pinned or default engine choice that only works when the candidate ranks below a given module). Expect one position to succeed and the other to fail with `VersionMismatch`. If no such scenario exists, rename the test.
  4. `AiCatalog.PlayerUsesAnotherModulesBehaviour`: assert `FindPlayer(...)->behaviour == "com.test.ai:Patrol"`, using whatever field the catalog exposes.
- **Verification:** For each test, a targeted mutation (clamp to the default, ignore `position`, skip the reflection write) now fails it.
- **Risks / trade-offs:** None significant.

## TEST-005 — Bulk "Malformed/Errors" tests check only that parsing failed, not why
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Convert the multi-case error tests into tables of `{input, expectedCode, messageSubstring}`, using `TEST_P` with `INSTANTIATE_TEST_SUITE_P` or a loop with `SCOPED_TRACE`. Start with `HudDocument.Parse_Malformed_Fails`, `Value.Json_WrongShape_Error`, `Value.ValidateSchema_Errors`, `StringTable.Parse_Malformed_Rejected` and `Primitives.InvalidParams_Fail`.
  2. Add a case for every `Invalid(...)` site that has none: HUD non-finite number, `referenceSize`, `widgets` not an array, `name` not a string.
  3. Add a short reviewer guideline: a new `EXPECT_FALSE(x.has_value())` should be paired with an `error().code` check, or a substring check when codes collide.
- **Illustrative change (not applied):**
  ```cpp
  struct BadHud { const char* json; const char* reason; };
  for (const BadHud& c : kBadHuds) {
      SCOPED_TRACE(c.json);
      const auto r = ParseHud(c.json);
      ASSERT_FALSE(r.has_value());
      EXPECT_NE(r.error().message.find(c.reason), std::string::npos) << r.error().message;
  }
  ```
- **Verification:** For each table, deleting any one targeted validation in the parser makes exactly its row fail.
- **Risks / trade-offs:** Message-substring checks couple tests to wording. Use short stable fragments (the field name) rather than whole sentences.

## TEST-006 — Vulkan backend failure branches are untested, including ones that need no GPU
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Add CPU-only `ShaderReflection` tests with small HLSL fixtures compiled by `interi_add_shaders`: two entry points (if DXC can emit them; otherwise hand-assemble SPIR-V words), push constants over 128 bytes, a resource at binding 7 or in set 1, and an unsupported stage. Assert the codes `InvalidArgument` or `Unsupported`.
  2. Make sure these tests are not `VulkanTest` fixtures, so they also run on Windows CI.
  3. Add a `VulkanTest` that creates a device with small bindless table capacities (if `VulkanDeviceDesc` allows; otherwise add a test-only capacity knob). Fill each table and expect `OutOfMemory`, then free one entry and expect success again.
- **Verification:** `ctest -R ShaderReflection` on `windows-msvc-debug` runs the new tests instead of skipping them.
- **Risks / trade-offs:** A capacity knob is a small production API addition and should be reviewed.

## TEST-007 — The JobSystem wait-nesting cap (stack-depth guard) has no deterministic test
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `tests/jobs/JobSystemTests.cpp`, create a `JobSystem` with 1 worker. Inside a job, recursively submit and `WaitFor` 40 levels deep. At each level, record whether a sibling job (submitted by another thread into the injection queue) ran on that thread while depth > 32. Assert that it never did, and that the chain completes.
  2. Add the zero-worker variant: depth 40 still completes because the cap does not apply.
- **Verification:** Changing `ownDequeOnly` to `false` in `engine/jobs/src/JobSystem.cpp:308` makes the test fail deterministically.
- **Risks / trade-offs:** Make sure the test cannot deadlock: the sibling must not be needed for completion.

## TEST-008 — The guarantee that a bad slot changes nothing in `GameSession::LoadFromSlot` is tested only for missing or corrupt files
- **Severity / Effort:** Medium / M
- **Depends on:** TEST-001 (optional, for an I/O-error variant)
- **Steps:**
  1. In `tests/game/GameSessionTests.cpp`, write a slot whose `scene.state` section version is higher than the engine's. Call `LoadFromSlot` and expect `VersionMismatch`. Then check that the run continued (positions and Values are unchanged).
  2. Add a slot with an unknown module section whose restore fails, and decide the contract. Either `LoadFromSlot` validates restorability before `StartScene()` (product change), or the documentation says the scene restarts and the report lists failures. Pin the chosen behaviour.
  3. Update CLAUDE.md's Game sessions sentence to match.
- **Verification:** Both new tests pass, and the documented guarantee matches what they assert.
- **Risks / trade-offs:** Step 2 may need a product decision. Until then, add the test with the `pending` label.

## TEST-009 — Removing the FileWatcher mtime check would pass every test
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:** Add `FileWatcher.SameSizeNewerTime_Fires`: write `"one"`, poll, rewrite `"two"` (same size), set `std::filesystem::last_write_time(file, old + std::chrono::seconds(2))`, and expect `Poll()` to be true.
- **Verification:** Dropping `.time` from the entry comparison makes the test fail.
- **Risks / trade-offs:** None. Setting mtime explicitly avoids granularity issues.

## TEST-010 — The JobSystem burst-wake unit test depends on wall-clock timing
- **Severity / Effort:** Low / S
- **Depends on:** TEST-002
- **Steps:**
  1. Replace `sleep_for(5ms)` with a wait for an observable condition, for example a test-only `JobSystem::SleepingWorkers()` accessor (or a debug counter) reaching the worker count, bounded by a generous deadline.
  2. Raise the per-job deadline to 10 s, in line with the other rendezvous tests, or move the test to `interi_jobs_stress_tests`.
- **Verification:** The test still fails if the wake-next protocol is removed, and it is stable over 200 local repetitions under TSan (`--gtest_repeat=200`).
- **Risks / trade-offs:** The accessor exposes internal state. Keep it documented as diagnostics only.

## TEST-011 — `AsyncFileReader.DestroyWithRequestsInFlight_Drains` has no assertion
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:** Wrap the `MemoryFileSystem` in a counting `IFileSystem` that increments an atomic on each `ReadFile` return. After each reader's destructor returns, assert that the count equals the number of requests (or that the count stops changing). This proves the jobs finished before destruction completed.
- **Verification:** Removing the wait from `AsyncFileReader::~AsyncFileReader` makes the test fail in a plain Debug build.
- **Risks / trade-offs:** None.

## TEST-012 — Duplicated test helpers and inconsistent per-format corruption tests
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Move `Workers(count)` and `Bytes(text)` into `testing/include/interi/testing/` (for example a `TestHelpers.h`) and delete the copies.
  2. Add one every-byte flip test per hashed-container implementation (`AssetIo.h` through any one asset type, mesh, cooked data, NavMesh), modelled on the SaveGame test after the TEST-003 fix. Keep the per-format tests only where the format adds its own validator.
- **Verification:** `grep -rn "JobSystemDesc Workers(" tests` returns nothing, and every container has an every-byte test.
- **Risks / trade-offs:** `interi_testing` must not pull in `interi::jobs` for every test executable. Use a separate small header if needed.

## TEST-013 — The asserts-off (shipping) configuration is never tested
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Add a `linux-clang-shipping` preset (`INTERI_SHIPPING=ON`, RelWithDebInfo) and a nightly CI job that runs `-L unit`.
  2. For each test that skips when asserts are off, add a sibling test for the shipping behaviour, for example `TaskGraph.Cycle_ShippingRunsNothingOrReturns`, after deciding what that behaviour should be.
- **Verification:** The nightly shipping job is green, and the skip counts drop because the asserts-off siblings run.
- **Risks / trade-offs:** Some asserts-off behaviour is undefined by design. Document it rather than test it.

## TEST-014 — Editor `ValueWidget` read-only suppression and type-mismatch paths are untested
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:** Add `tests/editor_ui/ValueWidgetsTests.cpp` using `ImGuiTestContext.h`. Drive a click or drag on a read-only Float field and assert `std::nullopt`. Pass a mismatched type and assert `std::nullopt`. Press `+` and `-` on a list and assert the item count changes. Add the file to `editor_ui`'s `TEST_SOURCES`.
- **Verification:** Changing `ValueWidgets.cpp:246` to `return edited;` fails the read-only test.
- **Risks / trade-offs:** Headless ImGui input needs `io.ConfigInputTrickleEventQueue = false`, as documented.

## TEST-015 — The coverage report has no floor, and GPU tests never run on Windows (observation)
- **Severity / Effort:** Info / S (optional)
- **Depends on:** None
- **Steps (optional):**
  1. Add `llvm-cov report --summary-only` parsing with a floor, or a diff-coverage check, to the coverage job.
  2. Add `unittest` tests for `tools/bench_compare/compare_benchmarks.py` if it becomes part of CI.
- **Verification:** The coverage job fails when line coverage drops below the agreed floor.
- **Risks / trade-offs:** A global floor can encourage low-value tests. Prefer per-module or diff-based gates.
