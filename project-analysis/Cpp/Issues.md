# Cpp — Issues

**Project:** Interi (repository root), branch `autoflow/003-goal-implement-milestone-21-8367f0` @ 4631f35
**Analyzed:** 2026-10-19
**Scope:** First-party C++23 under `engine/` (public headers `engine/*/include/interi/**`, sources `engine/*/src`), plus build and tooling config that governs it (`cmake/InteriWarnings.cmake`, `cmake/InteriSanitizers.cmake`, `.clang-tidy`, `CMakePresets.json`, `.github/workflows/ci.yml`). Reviewed in depth: core (Archive, Assert, Config, Log, PoolAllocator, StlAllocator, SmallVector, HashMap), ecs (Component, World, CommandBuffer), jobs (JobSystem, WorkStealingDeque), sdk (Hooks, ContributionRegistry), hotreload (ModuleApi, ModuleHost), assets (AssetIo, Texture, Skeleton), chain (Expression evaluator), anim (Animation, Animator JSON), hud (HudDocument), runtime (CameraRig), values (Value JSON), audio (AudioEngine queues), rhi_vulkan (CheckVk, Submit, bindless slots), platform/platform_sdl RAII wrappers. Out of scope: `external/`, `build*/`, `cmake-build-*/`, `modules/` (separate projects, not indexed), `project-analysis/`. Tests were read only to confirm coverage gaps.
**Summary:** 12 issues — Critical: 0 · High: 3 · Medium: 3 · Low: 4 · Info: 2

**Verified as sound (no finding):**
- `PoolAllocator`'s versioned (ABA-tagged) free-list head.
- The Logger's MPSC ring (sequence-numbered slots with acquire/release).
- `JobSystem::Submit`'s inline 48-byte storage (static_asserts on size and alignment, `std::launder`, rollback on a throwing copy).
- Chase-Lev deque memory orders.
- The chain `Expression` evaluator's integer semantics:
  - `INT_MIN / -1`, `% 0` and `% -1` are handled.
  - Arithmetic wraps through unsigned.
  - `abs(INT_MIN)` wraps.
  - `int()` saturates.
- `ModuleHost`'s reload commit order: hooks, then instances, then library close.
- Rule-of-5 and RAII on every resource class checked (68 destructors surveyed: copy and move are deleted or defined).
- `nlohmann::json::parse(..., allow_exceptions=false)` everywhere, with every `.get<T>()` preceded by a type check and every `const operator[]` by a size or `contains` check.
- Asset validators range-check enums read from archives.
- No mutable globals in public headers (only the per-DSO `ComponentId<T>` function-local static, which the registry de-duplicates by name).
- Warning flags (`-Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast ...`, `/W4 /WX /permissive-`).
- ASan+UBSan and TSan in CI.
- Every public header reaches a `HeaderCheck.cpp` (6 transitively).

## Findings

### CPP-001 — Hook signature is checked only on Override and only against the lowest entry; Declare and calls are unchecked (type confusion)
- **Severity:** High
- **Confidence:** High
- **Category:** Undefined behavior / type erasure
- **Location:**
  - `engine/sdk/include/interi/sdk/Hooks.h:158-161` (`Declare`)
  - `engine/sdk/include/interi/sdk/Hooks.h:196-208` (`CheckSignature`)
  - `engine/sdk/include/interi/sdk/Hooks.h:135-137` (`HookNext::operator()`)
  - `engine/sdk/include/interi/sdk/Hooks.h:224-229` (`HookPoint::operator()`)
  - `engine/sdk/include/interi/sdk/ContributionRegistry.h:194-197`, `:309-333`
- **Evidence:**
  - `Declare<Signature>` forwards straight to `m_registry.Provide(...)` with no `CheckSignature`. `Override` does call it (`:168`).
  - `CheckSignature` compares only `stack.back()`, the lowest-ranked entry (`Stack` is "winner first", `ContributionRegistry.h:226`).
  - At call time, `HookPoint` starts at `stack[0]` (the winner) and `HookNext` walks down, casting every entry blindly:
    ```cpp
    const auto& implementation =
        *static_cast<const HookImpl<R(Args...)>*>(m_stack[m_index].value.implementation.get());
    ```
  - Shared ids legitimately accept several `Declare`s, one per provider. `tests/sdk/HooksTests.cpp:132` (`Shared_HigherProviderWinsAndChains`) does exactly that. No test declares a shared id with a different signature.
  - A `HookPoint` re-resolves the stack on every call ("stays valid while its registry lives", `:214`) but never re-checks the signature. A module that changes a hook's signature across a hot reload therefore leaves earlier-`Find`ed handles calling a `std::function` of another type.
  - Secondary: `HookPoint` holds a `std::span` into the registry's `std::vector` for the duration of the call. An implementation that `Declare`s or `Override`s on the same id re-entrantly reallocates the vector under the span.
- **Impact:** Two Extensions, or an Extension and a higher module, that declare the same shared hook with different signatures cause the caller to invoke a `std::function<A>` object as `std::function<B>`. That is type confusion: arbitrary-code-pointer UB inside the engine host, triggered by module code that passes every load-time check. The ABI fingerprint does not help, because both sides are built by the same toolchain. The same happens after a hot reload that changes a hook signature.

### CPP-002 — `INTERI_VERIFY`/`INTERI_ASSERT` are the only guard on runtime-reachable conditions; with asserts off the code continues into OOB writes and null dereferences
- **Severity:** High
- **Confidence:** High (code); Medium (reachability in a shipping title)
- **Category:** Error handling / undefined behavior
- **Location:**
  - `engine/core/include/interi/core/Assert.h:100-111`
  - `engine/ecs/src/Component.cpp:57-58`
  - `engine/ecs/src/World.cpp:159-163`
  - `engine/ecs/src/CommandBuffer.cpp:50-52`
  - `engine/hotreload/src/ModuleHost.cpp:269`, `:291-300`
  - `engine/rhi_vulkan/src/VulkanDevice.cpp:1092-1098`, `:1644-1649`
  - `engine/core/include/interi/core/containers/HashMap.h:247`, `:283-287`
  - `engine/audio/src/AudioEngine.cpp:152-156`
- **Evidence:**
  - With `INTERI_ENABLE_ASSERTS == 0` (any `INTERI_SHIPPING` build, `Config.h:10-16`), `INTERI_VERIFY(c)` becomes `(void)(c)` and execution simply continues. The same happens in any build when a test-style handler returns `AssertAction::Continue`.
  - The affected sites:
    - `Component.cpp:57-58`: `INTERI_VERIFY(count < kMaxComponentTypes, ...); registry.infos[count] = std::move(info);`. This writes past the end of `std::array<ComponentInfo, 1024>` once the 1025th type registers. The header comment (`Component.h:19-21`) says module counts and layout-changing reloads "add up" towards this limit.
    - `World.cpp:159-163` and `CommandBuffer.cpp:50-52`: a null result from `IAllocator::Allocate` is only VERIFYed. The chunk or block is then used for placement-new and `push_back`.
    - `ModuleHost.cpp:269`: alignment above 64 is only VERIFYed. Migrated values are then written at 64-aligned offsets (`:291-300`).
    - `VulkanDevice.cpp:1092-1098`: a full storage-image table gives `slot == kInvalidBindlessIndex`, which is written as `dstArrayElement`.
    - `VulkanDevice.cpp:1644-1649`: `FindPool` dereferences `**found` after VERIFYing it non-null.
    - `HashMap.h:283-287`: the probe distance is stored as `uint16_t`, and `distance == 0x10000` wraps to the "empty" marker. Only reachable with a degenerate hasher.
    - `AudioEngine.cpp:152-156`: an `ends` queue overflow is only asserted. The retained clip then leaks.
- **Impact:**
  - Memory corruption (OOB write into the component registry), null-pointer writes on allocation failure, invalid Vulkan descriptor writes (driver UB), and misaligned object access.
  - All of it happens in exactly the configuration that ships, and silently.
  - The project rule in CLAUDE.md is "`INTERI_ASSERT`/`INTERI_VERIFY` cover programming errors; `Result<T>` covers runtime failures". Allocation failure, table exhaustion and registry exhaustion are runtime conditions.

### CPP-003 — `CheckVk` treats device loss and out-of-memory as programmer errors: abort in development, ignored in shipping
- **Severity:** High
- **Confidence:** High
- **Category:** Error handling (GPU)
- **Location:**
  - `engine/rhi_vulkan/src/VulkanImpl.h:23-24`
  - `engine/rhi_vulkan/src/VulkanDevice.cpp:359-362`, `:1732`, `:1773`, `:1783`
- **Evidence:**
  - The comment reads "Aborts through INTERI_VERIFY when a Vulkan call that cannot fail in a correct program fails". The implementation is `INTERI_VERIFY(result == VK_SUCCESS, call);`.
  - It wraps `vkQueueSubmit2` (`:1732`), `vkGetSemaphoreCounterValue` (`:1773`) and `vkWaitSemaphores(..., UINT64_MAX)` (`:1783`), plus 11 other call sites. All of these can return `VK_ERROR_DEVICE_LOST` or `VK_ERROR_OUT_OF_*_MEMORY` in a correct program (driver reset, TDR, VRAM pressure).
  - The swapchain already handles its failures through `Result` (`VulkanSwapchain.cpp:239-285`), and the device has a `VkError` → `ErrorCode::OutOfMemory` mapper (`VulkanDevice.cpp:350-355`) that these calls bypass.
- **Impact:** In development builds a GPU reset aborts the process (editor included) with no chance to save. In shipping builds the failure is discarded: `lastSubmitted` has already been incremented, so `ResourceManager`'s deferred destroys and `WaitForValue` wait on a timeline value that will never be signalled, and the frame loop continues against a lost device. Neither path can be recovered or reported.

### CPP-004 — The asserts-off (`INTERI_SHIPPING`) configuration is never built or tested
- **Severity:** Medium
- **Confidence:** High
- **Category:** Tooling / build configurations
- **Location:**
  - `engine/core/include/interi/core/Config.h:10-16`
  - `engine/core/include/interi/core/Assert.h:102-111`
  - `CMakePresets.json` (configure presets `linux-clang-*`, `windows-msvc-*`)
  - `.github/workflows/ci.yml:48`, `:97`, `:152`
- **Evidence:**
  - `git grep 'INTERI_SHIPPING\|INTERI_ENABLE_ASSERTS'` over `CMakeLists.txt`, `*.cmake`, `CMakePresets.json` and `.github` returns nothing.
  - No preset and no CI matrix entry (debug, release, asan, tidy, profile, msvc debug/release/profile, asan/tsan) defines `INTERI_SHIPPING` or `INTERI_ENABLE_ASSERTS=0`. CLAUDE.md states asserts stay on in RelWithDebInfo.
  - With asserts off, `INTERI_ASSERT` compiles to `(void)sizeof(!(condition))`, so different code is compiled. The `#else` branches gated on `kAssertsEnabled` and `INTERI_SHIPPING` (StringId name table, profiling) are never compiled in CI.
- **Impact:** The configuration in which CPP-002 and CPP-003 become silent corruption has never been compiled, unit-tested or sanitizer-run. Unused-variable warnings-as-errors, shipping-only compile errors, and behaviour that differs once asserts disappear are discovered only at release time.

### CPP-005 — Non-trivially-copyable components registered from module code leave function pointers into unloaded libraries
- **Severity:** Medium
- **Confidence:** High (mechanism); Medium (likelihood)
- **Category:** Ownership across the shared-library boundary
- **Location:**
  - `engine/ecs/include/interi/ecs/Component.h:50-55`, `:171-182`, `:189-194`
  - `engine/ecs/src/Component.cpp:41-60`
  - `engine/hotreload/include/interi/hotreload/ModuleApi.h:14-17`, `:141-146`
- **Evidence:**
  - `MakeComponentInfo<T>` stores `relocate`, `moveAssign` and `destroy` lambdas for any non-trivially-copyable `T`. These are function pointers into the DSO that first instantiated `ComponentId<T>()`.
  - The process-wide registry is append-only and keeps them forever.
  - The only enforcement that module components are trivially copyable is a `static_assert` in `hotreload::DeclareComponent<T>`. ModuleApi.h:14-17 states the rule, and CLAUDE.md says "Nothing else enforces it".
  - A module that calls `world.Add<T>()`, `CommandBuffer::Add<T>()` or `ComponentId<T>()` directly with its own `std::string`- or `std::vector`-holding struct registers entries that point into module code.
  - On reload or unload the library is closed (`ModuleHost.cpp:76-85`). The next `RemoveRow`, `World` destructor or `CommandBuffer::Clear` then calls `info.relocate`/`destroy` (`World.cpp:184-188`, `CommandBuffer.cpp:62-68`) into unmapped memory.
  - Registration happens through templates, so the graph shows no resolved callers (`impact RegisterComponent`: UNKNOWN). Every `World`/`Query` template instantiation in module code is a potential registration site.
- **Impact:** A module author's ordinary-looking ECS code compiles, loads, passes every host check, and crashes the host only later: after a hot reload, or at shutdown. It is a use-after-unload of code, and nothing reports it at the point of the mistake.

### CPP-006 — `ArchiveReader` bounds counts by remaining bytes, not by bytes × element size: allocation amplification, and `bad_alloc` escapes `Result` readers
- **Severity:** Medium
- **Confidence:** High (code); Medium (amplification factors are inferred from element sizes)
- **Category:** Binary deserialization / error handling
- **Location:**
  - `engine/core/include/interi/core/Archive.h:193-195`, `:219-235`
  - `engine/core/src/Archive.cpp:65-75`
  - `engine/assets/src/AssetIo.h:37-55`
  - `engine/assets/include/interi/assets/Texture.h:72`
- **Evidence:**
  - `ReadCount` accepts any `count <= Remaining()` ("every element takes at least one byte"). `Process` then runs `value.clear(); value.resize(count);` before reading a single element.
  - For `std::vector<T>` that allocates `count * sizeof(T)`. Examples: `TextureAsset::mips` is a `std::vector<std::vector<std::byte>>`, about 24× per input byte. `Skeleton::joints` holds `Joint`s with a string, transforms and a `Mat4`; inferred at more than 100× per input byte.
  - Nested vectors repeat the over-allocation at each level before the truncation is noticed.
  - The XXH3 payload hash in `ReadHashed` is not authentication, so a crafted `.itex`, `.iskel`, `.ianim`, `.imesh`, save or `IVAL` file passes it.
  - `resize` throws `std::bad_alloc`, which escapes `Read*Asset`, `ReadCookedData` and `SaveGame` reading, all of which promise `Result`. Inside a job (`AsyncFileReader`, whose jobs are `noexcept`) it reaches `std::terminate`.
- **Impact:** A few megabytes of hostile or corrupt content (mods, downloaded saves, a damaged cache) can demand gigabytes of memory and crash the game or editor instead of returning `ErrorCode::Corrupt`. This contradicts the documented "importers return `Result`, never assert; allocations are capped" rule.

### CPP-007 — Enums are deserialized from raw bits with no range check in the reader
- **Severity:** Low
- **Confidence:** High
- **Category:** Binary deserialization
- **Location:**
  - `engine/core/include/interi/core/Archive.h:210-213`
  - Validators that compensate today: `engine/assets/src/Texture.cpp:384-385`, `engine/assets/src/Skeleton.cpp:129-132`
- **Evidence:** `value = static_cast<T>(static_cast<std::underlying_type_t<T>>(bits));` accepts any bit pattern. For scoped or fixed-underlying enums this is well-defined, but it yields values outside the enumerator set. Correctness then depends on every `Validate*` remembering to cast back and compare against a hand-maintained bound (`> 2`, `> 1`, `>= kTextureFormatCount`). A `switch` without a default on an unvalidated value falls through silently.
- **Impact:** This is a latent bug pattern: the first new serialized enum whose validator forgets the check flows an invalid value into `switch`es, table lookups (`MipByteSize` returns 0) and GPU format mappings.

### CPP-008 — JSON finiteness is checked on the `double`, then the value is narrowed to `float`
- **Severity:** Low
- **Confidence:** High
- **Category:** Numeric conversions
- **Location:**
  - `engine/hud/src/HudDocument.cpp:29-32`, `:40`, `:49`, `:67`, `:250-254`
  - `engine/runtime/src/CameraRig.cpp:26-32`, `:35-42`
  - `engine/anim/src/Animator.cpp:175`, `:205`
  - `engine/anim/src/Animation.cpp:91`, `:342`
- **Evidence:**
  - `IsNumber` checks `std::isfinite(value.get<double>())`, then `value.get<float>()` narrows it. Any `|x| > FLT_MAX` (e.g. `1e39`) passes the check and becomes `±inf`.
  - Under a strict reading of [conv.double] the conversion is undefined. In practice IEEE implementations produce `±inf`.
  - `CameraRig::ReadVec3` (`:35-42`), anim `speed` and `duration`, and the clip `duration` have no finiteness check at all.
- **Impact:** Documents that the parser reports as "finite number" yield `inf` sizes, speeds and durations. NaNs then propagate into layout, camera and animation math (e.g. `inf * 0`). This is not a crash on its own, but it defeats the validators' contract. Values (`values::FloatFrom` → `CheckFloat`) is not affected.

### CPP-009 — `SmallVector` growth leaks on a throwing constructor and does not check `capacity * sizeof(T)` for overflow
- **Severity:** Low
- **Confidence:** High
- **Category:** Exception safety / integer overflow
- **Location:**
  - `engine/core/include/interi/core/containers/SmallVector.h:95-103`, `:136-140`, `:197-205`
  - Compare `engine/core/include/interi/core/memory/StlAllocator.h:38-43`
- **Evidence:**
  - In `emplace_back`'s growth path, `newData = AllocateBuffer(newCapacity)` is followed by `::new (newData + m_size) T(std::forward<Args>(args)...)`. If that constructor throws, `newData` is never freed.
  - `AllocateBuffer` computes `capacity * sizeof(T)` unchecked. `reserve` and `resize` with a large count wrap to a small allocation followed by `uninitialized_move`/`value_construct` past its end.
  - `StlAllocator::allocate` guards the same multiplication with `bad_array_new_length`.
- **Impact:** A leak on an exceptional path, and a heap overflow if a caller ever passes an unvalidated size. No such caller was found today, so this is hardening.

### CPP-010 — The clang-tidy check set omits the static analyzer and several core-guideline and concurrency checks
- **Severity:** Low
- **Confidence:** High
- **Category:** Tooling readiness
- **Location:** `.clang-tidy:4-21`, `cmake/InteriWarnings.cmake:7-9`
- **Evidence:**
  - Checks start from `-*` and re-enable `bugprone-*`, `modernize-*`, `performance-*`, two `cppcoreguidelines-*` checks and a few `readability-*`/`misc-*`.
  - They do not re-enable:
    - `clang-analyzer-*` (path-sensitive null-deref and leak detection, which would flag CPP-002-style null uses and CPP-009's leak)
    - `concurrency-*`
    - `cppcoreguidelines-pro-type-member-init`, `-prefer-member-initializer`, `-rvalue-reference-param-not-moved`, `-missing-std-forward`
    - `misc-const-correctness`, `misc-misplaced-const`
  - MSVC has no `/analyze` job.
- **Impact:** The strongest automated UB detector available to the project is not run, so findings of this report's kind depend on manual review.

### CPP-011 — `using namespace detail;` in a public header (function scope)
- **Severity:** Info
- **Confidence:** High
- **Category:** Header hygiene
- **Location:** `engine/core/include/interi/core/math/MathKernels.h:199`, `:205`, `:220`, `:235`, `:270`, `:290`, `:302`
- **Evidence:** Each directive is inside an inline function body, so nothing leaks to includers.
- **Impact:** None today. Noted because the checklist flags `using namespace` in headers. Explicit `detail::` qualification would make the SIMD kernels greppable. No action required.

### CPP-012 — The global logger is a raw pointer whose lifetime is enforced only by documentation
- **Severity:** Info
- **Confidence:** Medium
- **Category:** Concurrency / lifetime
- **Location:** `engine/core/include/interi/core/Log.h:144-161`, `engine/core/src/Log.cpp:23`, `:300-309`
- **Evidence:** `LogFormat` loads `GetGlobalLogger()` and then calls `logger->Log(...)`. `SetGlobalLogger(nullptr)` followed by destroying the logger does not wait for threads that already loaded the pointer. The header states "must uninstall it before destroying it", which is necessary but not sufficient while job workers are still logging.
- **Impact:** A use-after-free is possible only on shutdown orderings that destroy the logger before the `JobSystem`/`ModuleHost`. No such ordering was observed. Recorded for the shutdown checklist.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD (fresh) |
| 2 | cypher | `MATCH (c:Community) RETURN c.label, c.symbolCount, c.cohesion ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 all third-party (Joystick 355, Hidapi, Profiler, Manifold, Tracy, Libbacktrace...) |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 example or video flows under external/vcpkg/buildtrees; no first-party flow |
| 4 | cypher | Communities by member count filtered `filePath STARTS WITH 'engine/'` | Editor 204, Math 81, Editor_ui 73, Scene 66, Chain 53, Platform 48, Render 43, Sdk 43, Values 36, Ecs 33, Memory 33, Containers 28, Jobs 21 ... |
| 5 | query | "relocate component memcpy chunk storage type erased" | World, CreateErased, ComponentInfo, RelocateComponent (Component.h:58), ChunkView.ColumnData, CommandBuffer.m_blocks |
| 6 | query | "job inline capture storage placement new" | JobSystem.Job (JobSystem.h:120), JobSystem::Submit (:165-199); rest third-party |
| 7 | query | "archive reader deserialize binary read count" | ArchiveReader/ArchiveWriter (Archive.h), ArchiveReader::Open (Archive.cpp:9) |
| 8 | context | RelocateComponent (Component.h) | 4 callers, all World.cpp: AddErased, CreateErased, RemoveRow, file scope; epistemic exact |
| 9 | context | JobSystem::Submit (uid) | Calls AllocateJob, Enqueue; accesses Job.counter, JobCounter.m_pending; static_asserts on size and alignment confirmed |
| 10 | impact | ArchiveReader (upstream, summaryOnly) | Ambiguous: class (Archive.h:158) and ctor (:186), 0 resolved callers each, risk UNKNOWN (template use is not resolved) |
| 11 | cypher | `(f:Function OR f:Method)` label disjunction | Parser error: label OR is unsupported; re-issued as #12 |
| 12 | cypher | Function named RegisterComponent / ComponentInfoOf / ComponentTypeCount in engine/ | Component.cpp:40-60, :62-67, :69-72 plus header declarations |
| 13 | cypher | Files under engine/hotreload/ and engine/sdk/include | 21 files: ModuleApi.h, ModuleHost.cpp, Hooks.h, ContributionRegistry.h, Toolchain.h ... |
| 14 | context | HookRegistry | Ambiguous: class Hooks.h:146 and ctor :150; read Hooks.h directly |
| 15 | cypher | Function in engine/assets/ named Read*/Validate*/Serialize* | 34 rows: ReadHashed (AssetIo.h:37), ValidateTexture (Texture.cpp:376), ValidateClip (Skeleton.cpp:115) ... |
| 16 | cypher | Class method counts for core containers and allocators | SmallVector 36, FixedVector 30, HashMap 22, SlotMap 17, Tlsf 15, Logger 11, Pool 11, SPSC 10, MPMC 10 |
| 17 | impact | RegisterComponent (Component.cpp, upstream) | 0 resolved callers, risk UNKNOWN: callers are `ComponentId<T>` template instantiations, so the graph cannot count registration sites (supports CPP-005) |

## Analysis limitations
- Static reading only. No build, no test run, no sanitizer run, and no `clang-tidy` run, as the read-only rules require. Severity for CPP-002/003 is based on code paths, not on a reproduced crash.
- BM25 full-text search was slow (about 58 s per query) and dominated by third-party symbols, so discovery leaned on filtered `cypher` and targeted reads. No PDG layer was available (`pdg=no`), so `pdg_query`/`explain` guard analysis was not possible.
- `modules/` (physics, destruction) is not indexed and was not reviewed. Their use of `DeclareComponent` and hooks may make CPP-001 and CPP-005 more or less likely.
- Windows/MSVC-specific behaviour (DLL boundary, `/analyze`) was inferred from the source and CMake only.
- 62 `reinterpret_cast` sites were counted but not individually audited. Those read (Archive, Job storage, ModuleHost migration buffer, SmallVector inline storage) are correct.

## Cross-discipline notes
- Security: the XXH3 payload hash on `.itex`/`.imesh`/`ISAV`/`IVAL` (`engine/assets/src/AssetIo.h:46`) is integrity only, not authentication. Native modules run with full trust (by design, `TrustStore`).
- Security: `tools/editor` runs a user-configured build command with `popen` (CLAUDE.md, Module Manager `RunDevLoop`); worth a command-injection review.
- Unit-Tests: there is no test for a shared hook declared twice with different signatures (`tests/sdk/HooksTests.cpp`), and no CI job builds or tests with asserts off.
- Rendering/GPU: `VK_ERROR_DEVICE_LOST` has no recovery path (see CPP-003).
