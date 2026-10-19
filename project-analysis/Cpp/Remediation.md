# Cpp — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

Per CLAUDE.md, run `impact` (GitNexus) on each symbol before editing it, work test-first, and verify with the debug, release, asan, tsan and tidy presets.

## Recommended order
1. **CPP-004** (shipping preset and CI job). This is cheap, and it makes CPP-002 and CPP-003 observable and keeps them fixed.
2. **CPP-001** (hook signature type confusion). This is UB reachable from module code that passes every load check. The fix is small and local to `Hooks.h`.
3. **CPP-002, CPP-003** (assert-only guards; Vulkan errors). Both require deciding per site between "return `Result`" and "always-on fatal". Do them together under one policy.
4. **CPP-006, CPP-007** (Archive hardening). One change to `Archive.h` covers both.
5. **CPP-005** (module component registry). This needs a small design decision.
6. **CPP-008, CPP-009, CPP-010** (hardening and tooling). Enable CPP-010's checks last, because they will surface items the earlier fixes resolve.

---

## CPP-001 — Hook signature is checked only on Override and only against the lowest entry
- **Severity / Effort:** High / S
- **Depends on:** None
- **Steps:**
  1. In `HookRegistry::Declare` (`engine/sdk/include/interi/sdk/Hooks.h:158`), when `m_registry.Stack(id)` is non-empty, require `stack.front().value.signature == SignatureName<Signature>()` and return `ErrorCode::InvalidArgument` ("hook '{}' has another signature") otherwise. Use the same text as `CheckSignature`.
  2. Make `CheckSignature` compare against **every** entry, or keep the invariant from step 1 so that comparing one entry is enough. With step 1 in place, all entries share one signature. Check `front()` rather than `back()`, because `front()` is what `HookPoint` calls first.
  3. Make `HookPoint::operator()` safe across reloads. Store the expected signature name, a `std::string_view` to a `constexpr` literal, and compare it with `stack.front().value.signature` on each call. On a mismatch, take the "invalid HookPoint" path: assert, then return a value-initialised `R`. The cost is one pointer and length compare per call. Alternatively, invalidate cached `HookPoint`s through `Registry().Subscribe` when the winner changes.
  4. Document in `Hooks.h` that implementations must not `Declare`/`Override` re-entrantly. Optionally, assert re-entrancy with a depth counter in debug builds.
  5. Add tests to `tests/sdk/HooksTests.cpp`:
     - `TEST(Hook, SharedDeclare_SignatureMismatch_Error)`: two providers declare `service.Greeter` with different signatures, and the second `Declare` fails.
     - `TEST(Hook, Reload_ChangedSignature_OldHandleInvalid)`: extract the provider, re-declare with a new signature, and check that calling the old `HookPoint` asserts (`ScopedAssertHandler`) and does not invoke the new implementation.
- **Illustrative change (not applied):**
  ```cpp
  template <typename Signature, typename F>
  Result<void> Declare(std::string_view provider, std::string_view id, F&& implementation)
  {
      if (!m_registry.Stack(id).empty())
      {
          INTERI_TRY(CheckSignature<Signature>(id)); // compares stack.front()
      }
      return m_registry.Provide(provider, id, Erase<Signature>(std::forward<F>(implementation)));
  }
  ```
- **Verification:** The new tests pass under `linux-clang-asan` and `linux-clang-tsan`. The existing `Shared_HigherProviderWinsAndChains` and `ClassFactory_*` tests still pass. The `ModuleHostTest`/`ModuleProjectTest` hot-reload tests stay green.
- **Risks / trade-offs:** The per-call signature compare adds a few nanoseconds. If hooks are hot, cache a generation counter from the registry instead. Changing the Module ABI semantics may need an `INTERI_MODULE_ABI_VERSION` bump (CLAUDE.md: "bump when ... the hook types ... change").

## CPP-002 — Assert-only guards on runtime-reachable conditions
- **Severity / Effort:** High / M
- **Depends on:** CPP-004 (to test the asserts-off path)
- **Steps:**
  1. Decide the policy and document it in `Assert.h`:
     - `INTERI_ASSERT` stays for invariants.
     - A new always-on `INTERI_CHECK_FATAL(cond, msg)` logs and calls `std::abort()` in every configuration, ignoring the handler's `Continue` in shipping. Use it for conditions the engine cannot survive.
     - Anything input-, allocation- or capacity-driven returns `Result`.
  2. `engine/ecs/src/Component.cpp:57-58`: never index past the array. Either make `RegisterComponent` return `Result<ComponentTypeId>`, which ripples through `ComponentId<T>`, or switch to `INTERI_CHECK_FATAL`. Fatal is the pragmatic choice, because 1024 types is a process-wide budget. At minimum, guard the write with `if (count >= kMaxComponentTypes) { ReportFatal(...); }`.
  3. `engine/ecs/src/World.cpp:159-163` and `engine/ecs/src/CommandBuffer.cpp:50-52`: throw `std::bad_alloc` when `Allocate` returns null, matching `SmallVector`/`HashMap`/`PoolAllocator`, or use `INTERI_CHECK_FATAL`. Do not proceed with a null chunk.
  4. `engine/hotreload/src/ModuleHost.cpp:269`: return `ErrorCode::Unsupported` ("component '{}' alignment {} above 64") instead of VERIFY. `MigrateComponents` already returns `Result`, so the rollback path exists.
  5. `engine/rhi_vulkan/src/VulkanDevice.cpp:1092-1098`: propagate `ErrorCode::OutOfMemory` from texture creation when `storageImageSlots.Allocate()` fails, and free already-created views. Do the same for every other `BindlessSlots::Allocate` caller.
  6. `engine/rhi_vulkan/src/VulkanDevice.cpp:1644-1649`: `FindPool` on a stale handle is a programming error. Use `INTERI_CHECK_FATAL`, not a VERIFY followed by `**found`.
  7. `engine/core/include/interi/core/containers/HashMap.h:283`: either widen `m_distances` to `uint32_t`, or grow (rehash) when `distance` reaches `kMaxDistance` instead of asserting.
  8. `engine/audio/src/AudioEngine.cpp:152-156`: keep the assert, but on failure (asserts off) record the voice in a game-thread-visible overflow counter so the retained clip is reclaimed on the next `Update`.
  9. Audit the remaining `INTERI_VERIFY` sites (`git grep -n INTERI_VERIFY engine`) with the same policy. The editor `Commands.cpp` VERIFYs are invariants and are fine.
- **Illustrative change (not applied):**
  ```cpp
  // Component.cpp
  if (count >= kMaxComponentTypes) [[unlikely]]
  {
      detail::ReportFatal("more than kMaxComponentTypes component types", __FILE__, __LINE__); // [[noreturn]]
  }
  registry.infos[count] = std::move(info);
  ```
- **Verification:**
  - Add tests that run with asserts disabled (CPP-004 job): register 1025 types and expect death (`EXPECT_DEATH`) rather than corruption.
  - Use a failing `IAllocator` stub to check that `World::Create` throws or aborts deterministically.
  - Check that `ModuleHost` reports alignment above 64 as an error.
  - `linux-clang-asan` stays clean.
- **Risks / trade-offs:** Turning an assert into `Result` changes public signatures and the module ABI where they cross it. Prefer fatal for process-wide budgets and `Result` for per-asset and per-module failures.

## CPP-003 — `CheckVk` treats device loss and out-of-memory as programmer errors
- **Severity / Effort:** High / M
- **Depends on:** CPP-002 (policy); cross-discipline with Rendering
- **Steps:**
  1. Split the macro:
     - `INTERI_VK_CHECK` (always-on fatal with the `VkResult` name) for genuinely impossible failures such as `vkCreateImageView` with valid input.
     - `INTERI_VK_TRY` (returns `std::unexpected(VkError(result, #call))`) for calls that can fail at run time.
  2. Make `IDevice::Submit` return `Result<std::uint64_t>`. On `VK_ERROR_DEVICE_LOST` or OOM:
     - Do not advance `lastSubmitted`. Increment it only after a successful `vkQueueSubmit2`. Today it is pre-incremented at `VulkanDevice.cpp:1704`.
     - Set a sticky `deviceLost` flag.
  3. Make `WaitForValue` return `Result<void>` and stop waiting on `deviceLost`. Make `CompletedValue` return the last known value on failure and set the flag.
  4. Have `ResourceManager`, `FrameGraph`, `ForwardRenderer` and the editor frame loop surface `ErrorCode::DeviceLost`, which needs a new code. The editor can then autosave and show a message instead of aborting.
  5. Update the `MockDevice` and `NullDevice` overrides (`tests/rhi/`) in the same change, per CLAUDE.md.
  6. Fix the comment at `VulkanImpl.h:23`, which wrongly says the macro "aborts" when asserts are off.
- **Illustrative change (not applied):**
  ```cpp
  #define INTERI_VK_TRY(call)                                                        \
      if (const VkResult r_ = (call); r_ != VK_SUCCESS)                              \
          return std::unexpected(VkError(r_, #call))
  ```
- **Verification:**
  - Add a unit test with `MockDevice` returning `DeviceLost` from `Submit`, and check that `ResourceManager` neither hangs nor double-frees.
  - Add a GPU test that injects `VK_ERROR_DEVICE_LOST` through a wrapped dispatch table entry (volk lets the test replace `vkQueueSubmit2`) and expects an error, not an abort.
  - Golden tests are unchanged.
- **Risks / trade-offs:** This is an RHI interface change touching every renderer call site and the private console backends. Bump the module ABI if `IDevice` crosses it. True device-loss recovery (recreating the device) is out of scope; reporting it is not.

## CPP-004 — The asserts-off configuration is never built or tested
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add the configure presets `linux-clang-shipping` and `windows-msvc-shipping` (Release, `-DINTERI_SHIPPING` through a cache variable that `cmake/InteriBuildSettings.cmake` turns into a compile definition on `interi::build_settings`). Add matching build and test presets.
  2. Add both to the CI matrix (`.github/workflows/ci.yml:48` and `:97`), running `ctest -L unit`. Tests that rely on asserts already `GTEST_SKIP` when `!kAssertsEnabled`.
  3. Optionally add `linux-clang-asan` with `-DINTERI_ENABLE_ASSERTS=0` as a nightly job, so UBSan and ASan watch the asserts-off paths.
- **Verification:** CI shows green `*-shipping` jobs on Linux and Windows. Deliberately breaking an `#if !INTERI_ENABLE_ASSERTS` branch fails CI.
- **Risks / trade-offs:** CI time increases by two jobs. Tests that implicitly depend on assert side effects will surface, which is the point.

## CPP-005 — Non-trivially-copyable module components leave function pointers into unloaded code
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Record the registering DSO for each `ComponentInfo` that has non-null `relocate`/`moveAssign`/`destroy`. Resolve the module from the function address with `dladdr`, or `GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS)` on Windows, and compare it with the engine's own image.
  2. In `ModuleHost`, after each module's `Register` and after each of its scheduler runs in debug builds:
     - Scan `ComponentInfoOf(id)` for ids added since the module loaded (`ComponentTypeCount()` before and after).
     - Refuse the load (`ErrorCode::InvalidArgument`, "component '{}' from module '{}' is not trivially copyable") when any such entry has non-null function pointers resolving into the module's image.
  3. Also refuse to unload a module while any live archetype column's `ComponentInfo` points into its image. This turns a later crash into an error at reload time.
  4. Document in `docs/` (module authoring) that `world.Add<T>` with a module-defined non-trivial `T` is unsupported, and why.
- **Verification:**
  - Add a test module variant (`tests/hotreload/modules/TestModule.cpp` adds variant 8) that adds a component holding a `std::string` without `DeclareComponent`. Check that `ModuleHost::Load` or `Reload` fails with the new error and that the world is untouched.
  - The run must stay clean under ASan and TSan.
- **Risks / trade-offs:** Address-to-module lookup is platform-specific, and costs nothing at run time if it runs only at load and unload. A stricter alternative is to make the registry refuse non-trivial types registered after the host enters "module load" mode. CLAUDE.md notes that such a mode existed and was removed, so check why before reinstating it.

## CPP-006 — `ArchiveReader` allocation amplification; `bad_alloc` escapes `Result` readers
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `ArchiveReader::Process` for `std::vector<T>`, bound the count by the minimum encoded size of an element. For `ArchivePrimitive` `T` this is `sizeof(ArchiveBits<T>)`; otherwise it is 1.
     - Reject `count > Remaining() / minEncoded`.
     - For non-primitive `T`, grow incrementally instead of `resize(count)`: `reserve(min(count, Remaining()))`, then `emplace_back` and `Process` per element, stopping on the first failure. Allocation then tracks bytes actually consumed.
  2. Do the same for `std::string`. It is already 1:1, so no change is needed.
  3. Add an optional `ArchiveReader::SetMaxAllocation(bytes)` (default, e.g., 1 GiB) checked before each `reserve`, failing sticky with `Fail()`.
  4. Catch `std::bad_alloc` in `detail::ReadHashed` (`engine/assets/src/AssetIo.h:37`) and in the save and cooked-data readers, converting it to `ErrorCode::OutOfMemory`. This guarantees the `Result` contract.
  5. Extend the fuzz targets (`tests/asset_import/fuzz/FuzzTargets.*`) to the `.itex`/`.iskel`/`.ianim`/`.iskin`/`ISAV`/`IVAL` readers if they are not already covered, with ASan's `allocator_may_return_null=1` and `-rss_limit_mb=2048` in the libFuzzer harness.
- **Illustrative change (not applied):**
  ```cpp
  else if constexpr (detail::IsStdVector<T>::value)
  {
      const std::size_t count = ReadCount();
      value.clear();
      value.reserve(std::min(count, Remaining())); // never more than the bytes that remain
      for (std::size_t i = 0; i < count && m_ok; ++i)
      {
          Process(value.emplace_back());
      }
  }
  ```
- **Verification:**
  - Add a unit test with a 64-byte archive claiming `count = 60` `std::vector<std::vector<std::byte>>` elements. Peak allocation, measured with `TrackedAllocator` or a counting `StlAllocator`, stays below 1 KiB, and the result is `Corrupt`.
  - The fuzz corpus runs clean under `-rss_limit_mb`.
  - `Values_RoundTrip_BitExact` and the asset round-trip tests are unchanged.
- **Risks / trade-offs:** Element-by-element growth is slightly slower for large valid arrays. Keep the `resize` fast path for primitive `T`, where the bound is exact.

## CPP-007 — Enums deserialized from raw bits with no range check
- **Severity / Effort:** Low / S
- **Depends on:** CPP-006 (same file)
- **Steps:**
  1. Add an opt-in trait `template <class E> constexpr auto kEnumCount = ...;` (or a `kEnumMax` member via a concept). In `ArchiveReader::Process`, call `Fail()` when an enum with the trait reads a value `>= kEnumCount<E>`.
  2. Declare the trait for every enum that is serialized: `TextureFormat`, `ColorSpace`, `ChannelPath`, `Interpolation`, `ValueType`, and the mesh and material enums.
  3. Keep the existing validator checks, or replace their magic bounds with the trait.
- **Verification:** Unit test: a hand-built archive with `format = 0xFF` makes `ReadTextureAsset` return `Corrupt` because the reader failed, not only the validator. Grep shows no remaining `static_cast<std::uint8_t>(...) > N` magic bounds.
- **Risks / trade-offs:** None for valid data. Enums without the trait keep today's behaviour.

## CPP-008 — JSON finiteness checked on `double`, then narrowed to `float`
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add one helper in a shared private header, or reuse `values::detail::FloatFrom`: `Result<float> ReadFiniteFloat(const json&)`. It checks `is_number()`, converts `get<double>()`, and rejects values where `std::abs(d) > std::numeric_limits<float>::max()` or the converted float is not finite.
  2. Use it in `HudDocument.cpp` (`IsNumber`, `ReadPair`, `ReadColor`, `ReadNumber`, `referenceSize`), `CameraRig.cpp` (`ReadFloat`, `ReadVec3`), `Animator.cpp` (`speed`, `duration`) and `Animation.cpp` (`ReadVec3`, `ReadQuat`, key times, clip `duration`).
- **Verification:** Parser tests feeding `1e39` to each field return an error naming the field. Existing HUD, camera and anim tests pass.
- **Risks / trade-offs:** Documents that relied on huge values being clamped to `inf` start failing. None are expected.

## CPP-009 — `SmallVector` growth leak and unchecked size multiplication
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `AllocateBuffer`, throw `std::bad_array_new_length` when `capacity > std::numeric_limits<std::size_t>::max() / sizeof(T)`, mirroring `StlAllocator::allocate`.
  2. In the `emplace_back` growth path, guard the placement-new: free `newData` on exception and rethrow. Do it with a small scope guard, or a `try`/`catch(...)` that frees and rethrows, the pattern already used in `Allocator.h:57-69`.
  3. Apply the same review to `FixedVector` (no allocation, so N/A) and to `HashMap::Allocate`, whose `entryBytes + capacity * sizeof(uint16_t)` multiplication is also unchecked.
- **Verification:**
  - Add a unit test with an element type whose constructor throws on the Nth construction, wrapped in a `TrackedAllocator`; the live byte count returns to 0.
  - Add a test that `reserve(SIZE_MAX / 2)` throws `bad_array_new_length`.
  - Benchmarks (`interi_core_benchmarks`) show no regression.
- **Risks / trade-offs:** None meaningful.

## CPP-010 — clang-tidy omits the static analyzer and several guideline checks
- **Severity / Effort:** Low / M (mostly triage of new diagnostics)
- **Depends on:** Ideally after CPP-002 and CPP-009, which fix what the analyzer would report first
- **Steps:**
  1. Add the following to `.clang-tidy`:
     - `clang-analyzer-*` (keep `-clang-analyzer-optin.*` initially)
     - `concurrency-*`
     - `cppcoreguidelines-pro-type-member-init`, `cppcoreguidelines-prefer-member-initializer`, `cppcoreguidelines-missing-std-forward`, `cppcoreguidelines-rvalue-reference-param-not-moved`
     - `misc-const-correctness` (it may be noisy; start with `AnalyzeValues: false`)
  2. Run `cmake --preset linux-clang-tidy && cmake --build --preset linux-clang-tidy`. Fix or `NOLINT`-justify each diagnostic in the same PR. Third-party headers are already excluded by `HeaderFilterRegex`.
  3. Optionally add an MSVC `/analyze` job (`windows-msvc-analyze` preset, no `/WX` at first) and track its warning count.
- **Verification:** The `linux-clang-tidy` CI job is green with the extended check list. `git grep -c NOLINT engine` grows only by justified, commented suppressions.
- **Risks / trade-offs:** `clang-analyzer-*` slows the tidy job, perhaps 1.5–2x. Some analyzer checks produce false positives on lock-free code, so suppress them locally with justification.

## CPP-011, CPP-012 (Info)
No remediation required. Optional:
- **CPP-011:** Replace `using namespace detail;` with explicit `detail::` qualification in `MathKernels.h`.
- **CPP-012:** Add "destroy the logger last, after `JobSystem` and `ModuleHost`" to the shutdown-order comments in `tools/editor` and `GameSession`, or make `SetGlobalLogger(nullptr)` wait on a reader count.
