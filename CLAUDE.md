# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Interi is an MIT-licensed C++23 game engine built only for large-scale, multithreaded structural destruction (Red Faction: Guerrilla style). PC targets are Linux (Clang) and Windows (MSVC). PS5/Xbox backends live in separate private repos. Development follows a milestone plan, one milestone per branch (`milestone-N`). `main` is protected and changes reach it through PRs that must pass CI.

## Starting a milestone

The milestone plan is `~/.claude/plans/cheeky-kindling-moth.md`; its M5 section starts with work carried over from M1. When asked to start a milestone:

1. **Plan first, with the `planning-with-files` skill.** Invoke it before writing any code, and give it that milestone's section of the plan as input. It produces a Manus-style plan in the repo root:
   - `task_plan.md`: phases (the milestone's sub-areas) with explicit verification steps.
   - `findings.md`: research and decisions, such as dependency licenses, port options and API choices.
   - `progress.md`: the session log.
   Ask the user about genuine product decisions while planning, not halfway through implementation.
2. **Execute from `task_plan.md`, one phase at a time.** Work test-first. Verify each phase with the checks below, then update `progress.md` and the phase status before moving on. After a `/clear` or a new session, read the three files first and resume from `task_plan.md`'s next step.
3. **Branch and commit.** Work on a `milestone-N` branch, with one commit per completed phase pushed so CI checks MSVC early. Open the PR to `main` only when the user asks.

The three plan files and `.planning/` are gitignored working state; never commit them. Anything worth keeping (e.g. carry-overs to a later milestone) goes into the milestone plan file or a commit message.

## Build and test

vcpkg is vendored as a submodule (`external/vcpkg`, manifest mode). Bootstrap it once with `external/vcpkg/bootstrap-vcpkg.sh -disableMetrics`. Everything else goes through presets:

```sh
cmake --preset linux-clang-debug
cmake --build --preset linux-clang-debug
ctest --preset linux-clang-debug                 # every label except `pending`
ctest --preset linux-clang-debug -L unit         # what CI runs on push (CI also adds stress|benchmark)
ctest --preset linux-clang-debug -R 'SlotMap\.'  # one suite (CTest name = Suite.Test)
build/linux-clang-debug/engine/core/interi_core_tests --gtest_filter='HashMap.Rehash*'  # run gtest directly
```

Presets:
- `linux-clang-{debug,release,asan,tsan,coverage,tidy,profile}`
- `windows-msvc-{debug,release,profile}`

Before committing, verify the following:
- **Presets:** run debug, release, asan, tsan and tidy. TSan matters for any concurrent code.
- **Format:** `git ls-files -- '*.h' '*.cpp' | xargs clang-format --dry-run --Werror` (Allman braces, 120 columns).
- **SPDX headers:** `python3 tools/license_check/check_spdx_headers.py`. Every source and CMake file starts with `SPDX-License-Identifier: MIT`.
- **Coverage:** use the `linux-clang-coverage` preset. Run `ctest`, then build the `coverage` target. The report lands in `build/linux-clang-coverage/coverage/`.
- **Benchmarks:** measure with the release preset, e.g. `build/linux-clang-release/benchmarks/core/interi_core_benchmarks`. Each benchmark executable also registers a `benchmark`-labelled `--benchmark_dry_run` smoke test.

Warnings are errors on both compilers: `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wdouble-promotion ...` on Clang, `/W4 /WX` on MSVC. clang-tidy (`.clang-tidy`) runs only on engine library targets, not on tests. Header-only code is linted because `engine/core/src/HeaderCheck.cpp` includes every public header and explicitly instantiates the container templates. Add new public headers there.

## Build-system conventions

- Declare modules with `interi_add_module(name SOURCES ... TEST_SOURCES ... DEPS ...)` from `cmake/InteriModule.cmake`. It creates `interi_<name>` (alias `interi::<name>`) and `interi_<name>_tests`. A module without `TEST_SOURCES` fails to configure, and a test enforces this.
- Test sources live in `tests/<module>/`, not next to the module.
- Tests get one CTest label per executable:
  - Long or concurrent tests go in a separate executable built with `interi_add_test_executable(... LABELS stress)`. `interi_core_stress_tests` is an example.
  - Labels: `unit`, `stress`, `integration`, `golden`, `benchmark`, `license`.
- TDD `pending` label: tests committed before their implementation can carry `pending`. Presets and blocking CI exclude it, and CI fails a PR or `main` that still contains pending tests.
- Test naming: `TEST(Suite, Condition_ExpectedResult)`, e.g. `TEST(SlotMap, StaleHandle_LookupFails)`.
- `interi_testing` (`testing/`) provides shared test helpers:
  - `ScopedTempDir`
  - `SeededTest`, a fixture with a deterministic SplitMix RNG. Override the seed with `INTERI_TEST_SEED`.
  - `INTERI_EXPECT_VEC_NEAR` / `INTERI_EXPECT_QUAT_NEAR`
  - `StateHasher`, an XXH3 hasher for determinism checks.

## Platform and determinism rules

- **CPU baseline is x86-64-v3** (AVX2/FMA, matching PS5/Xbox Series). `cmake/InteriArch.cmake` applies it engine-wide with `-march=x86-64-v3` or `/arch:AVX2`. It also sets `-ffp-contract=off`, so Clang and MSVC agree bit-for-bit.
- SIMD code must test the CMake-defined `INTERI_SIMD_SSE4` / `INTERI_SIMD_AVX2` macros, never `__SSE4_1__` (MSVC doesn't define it). `INTERI_SIMD=OFF` builds the scalar path.
- Don't use approximate intrinsics (`rcp`/`rsqrt`) in anything that can feed simulation.
- Cache-line padding uses `kCacheLineSize` (`Config.h`), not `hardware_destructive_interference_size`. Wrap `alignas` padding in `INTERI_MSVC_WARNING_PUSH_DISABLE(4324)`.
- `INTERI_SHIPPING` compiles out asserts, the StringId name table, and profiling. Asserts stay enabled in RelWithDebInfo. Tests that depend on asserts must `GTEST_SKIP` when `!interi::kAssertsEnabled`.

## Core architecture (`engine/core`, namespace `interi`)

- **Errors.** `INTERI_ASSERT`/`INTERI_VERIFY` cover programming errors. Their process-wide handler is replaceable: tests install a `ScopedAssertHandler` that returns `AssertAction::Continue`. `Result<T>` (`std::expected<T, Error>`) plus `INTERI_TRY` / `INTERI_TRY_ASSIGN` cover runtime failures (I/O, bad data, degenerate geometry).
- **Memory.**
  - Everything allocates through `IAllocator`. Its `Free(ptr, size)` takes the size, so wrappers need no headers.
  - Containers hold an `IAllocator*` that defaults to `GetDefaultAllocator()`. `StlAllocator<T>` adapts it for std containers.
  - Available allocators: `LinearAllocator`, `FrameAllocators` (per thread, double-buffered), the lock-free `PoolAllocator`, `TlsfAllocator` (not thread-safe), and `TrackedAllocator` (per-`MemoryTag` stats).
- **Containers.** `SlotMap` + `Handle<T>` (generational; slots retire instead of wrapping), `SmallVector`, `FixedVector`, the Robin Hood `HashMap` (transparent lookup), `MPMCQueue` (Vyukov, used by the job system), and `SPSCQueue`.
- **Math.**
  - Conventions are pinned by tests: right-handed, +Y up, column vectors (`A * B` applies B first), column-major `Mat4` with translation in column 3, quaternions as (x,y,z,w).
  - Headers are layered to avoid cycles: `Vec.h` → `MathTypes.h` (struct layouts) → `MathKernels.h` → `Quat.h`/`Mat4.h` (operators) → `Transform.h`.
  - Every SIMD kernel in `math::simd` has a reference in `math::scalar`, and both are compiled in all builds. `math::active` picks one. Parity tests in `SimdParityTests.cpp` are bit-exact where the operation order matches, so keep the scalar and SIMD summation orders identical.
- **Geometry.** `BuildConvexHull` (Quickhull) uses double-precision planes on centred input and an epsilon scaled to the cloud's extent. It returns `ErrorCode::Degenerate` for clouds thinner than about 1000·eps; this threshold is measured, so don't lower it without re-measuring.
- **Logging.** `Logger` uses a lock-free MPSC ring of fixed-size slots, so `Log()` never blocks or allocates. Sinks run on the flush thread or in `Flush()`. `INTERI_LOG_*` macros format with `std::format` through the global logger.
- **Serialization.** Each type gets a single `template <class Archive> void Serialize(Archive&, T&)`, found by ADL, that serves both reading and writing. Fields added in later versions are guarded with `archive.Version() >= N`. The format is little-endian and bit-exact.
- **Profiling.** `INTERI_ZONE*` macros map to Tracy only with `INTERI_PROFILING=ON` (`*-profile` presets), in on-demand mode. Otherwise they compile to nothing.

## Job system (`engine/jobs`, `engine/jobs_jolt`)

- **`JobSystem`** is a plain class; tests create many instances with different worker counts. Workers default to hardware threads − 1. Each worker, plus the thread that constructed the system, owns a Chase-Lev `WorkStealingDeque` per `JobPriority` (High/Normal/Low). Other threads submit through MPMC injection queues. Threads register by a unique system id, not by pointer.
- **Jobs.** `Submit(fn, &counter, priority)` stores up to 48 bytes of captures inline in a pooled 64-byte `Job`. Jobs must not throw. When the pool is exhausted, `Submit` runs the job inline.
- **Waiting.** `WaitFor(counter)` never idles: it runs other High/Normal jobs, but not Low ones (IO and cooking), unless there are zero workers. Waits nest up to `kMaxWaitNesting` (32). Past that, a waiter only pops its own deque, which keeps stack depth bounded (Windows threads have 1 MB stacks).
- **Sleeping.** Idle workers search (spin), then sleep. A submit wakes a sleeper only when more jobs are queued than workers are searching. A woken worker counts itself as searching, and the last searcher to find work wakes the next (the Go/Tokio protocol). The sleep predicate re-checks `m_queued` under the mutex, a seq_cst Dekker check against `m_sleepers`, so wake-ups are never lost. The code uses seq_cst operations rather than fences everywhere, because TSan doesn't model fences.
- **Shutdown.** The destructor drains every queued job.
- **`TaskGraph`.** Build it once and `Run()` it repeatedly. When asserts are enabled, `Run()` asserts on a cycle and runs nothing.
- **Parallel algorithms.** `ParallelFor`/`ParallelForChunks`/`ParallelReduce`/`ParallelPrefixSum`/`ParallelRadixSort` cut ranges into chunks from range and grain alone, and combine chunk results in order, so results (floats included) are bit-identical at any thread count. Keep it that way.
- **`interi::JoltJobSystem`** (`jobs_jolt`) implements `JPH::JobSystemWithBarrier` on the engine workers. Only code that needs Jolt links this module. Inside the class, write `interi::JobSystem`, because plain `JobSystem` names Jolt's base class.
- **TSan.** The `linux-clang-tsan` preset uses the overlay triplet `cmake/triplets/x64-linux-tsan.cmake`, which builds every vcpkg dependency with Clang and `-fsanitize=thread`. Without it, uninstrumented Jolt synchronization produces false race reports.

## Platform (`engine/platform`, `engine/platform_sdl`)

- **`interi::platform` never links SDL.** It holds the interfaces and everything testable without a window: `IFileSystem` (`NativeFileSystem`, `MemoryFileSystem`, `VirtualFileSystem`), `AsyncFileReader`, `DynamicLibrary`, `InputEvent`/`IEventSource`, `IWindow` and `InputMapper`. Tests drive input through `tests/platform/FakeEventSource.h` (which also has a gmock `MockWindow`).
- **Virtual paths** are normalised: `/` separators, and `..` or `:` are rejected. `VirtualFileSystem` tries mounts newest first and falls through when a mount lacks the file. Writes go to the newest mount whose prefix matches.
- **`AsyncFileReader`** reads on Low-priority jobs. Never wait for IO from inside a job, because `WaitFor` skips Low jobs while workers exist. Poll with `IsDone`/`TryTake`, or call `WaitAll` from a non-job thread.
- **`DynamicLibrary`** tests load a real `MODULE` library (`interi_test_dynlib`) whose path is baked in via `$<TARGET_FILE:...>`.
- **`interi::platform_sdl`** (SDL3) provides `SdlPlatform`, which is the `IEventSource` and opens `IWindow`s; only one may exist at a time. Event translation (`TranslateSdlEvent`, `KeyFromSdlScancode`) is pure and unit-tested.
  - Real SDL tests use the `dummy` video driver, so they run headless in CI.
  - SDL3 is pulled in with `default-features: false` plus linux-qualified `x11`/`wayland`. The Linux build needs the X11/Wayland `-devel` packages listed in `ci.yml`'s `FEDORA_PACKAGES`.

## ECS (`engine/ecs`)

- **Components** are any nothrow-movable types, registered on first use of `ComponentId<T>()` (at most 256 types). Empty types are storage-free tags.
  - Ids depend on first-use order, so they must never affect layout, iteration order or hashed state. Columns are ordered by type name.
- **Storage.** Each archetype (one component set) is a list of 16 KiB SoA chunks from the `World`'s `IAllocator`. Rows stay packed via swap-remove, and add/remove moves rows along cached archetype edges. `Entity` is a generational `Handle<EntityTag>`.
- **Change ticks** follow the Bevy model:
  - Each query or system run takes `World::BeginRun()` and stamps the columns it writes with that tick.
  - Direct `World` writes stamp `CurrentTick()`.
  - `Changed<T>` matches a column tick greater than the run's `lastRun`. Granularity is per chunk, and structural moves count as changes.
- **Structural changes** (create, destroy, add, remove) are not allowed while systems run. Record them in `CommandBuffers` instead: one buffer per job thread, keyed with `SystemContext::CommandKey(chunkOrdinal)`.
  - Playback sorts by (key, record order), so the result is identical at any thread count. Tests compare `HashWorld` (`tests/ecs/EcsTestUtil.h`) across 1/4/32 threads.
  - Commands that target dead entities are skipped, and `Add` on an existing component assigns.
- **`Scheduler`.** Systems declare `SystemAccess` reads and writes. Conflicting pairs run in registration order unless `RunAfter` already orders them. A cycle in the `RunAfter` orders is a `Result` error from `Build()`, not an assert.
  - A query's `Changed` filter must be declared as a read. `With`/`Without` only test archetype membership.
  - With asserts on, `ChunkView`/`SystemContext` assert on undeclared accesses.
  - Iterate with `ParallelForEachChunk` (`ParallelForChunks` underneath) rather than submitting many small jobs.

## Runtime (`engine/runtime`, `game/sandbox`)

- **`FixedStepClock`** takes the elapsed time from the caller and never reads a clock. It uses integer nanoseconds, clamps substeps after a hitch (dropping whole steps but keeping the fraction), and exposes `Alpha()` for `Interpolate(previous, current, alpha)`.
- **`FramePipeline<Snapshot>`** double-buffers: render prep of frame N runs as a job while frame N+1 simulates on the calling thread. Neither side writes what the other reads. The TSan stress test lives in `interi_runtime_stress_tests`.
- **`interi_sandbox`** (`game/sandbox`) is the window + input + ECS + rendering demo. Run `--video-driver offscreen --frames N --auto-spawn 1 --validation 1` for a headless rendering smoke run on lavapipe. `--video-driver dummy` has no Vulkan, so it runs without drawing. CI builds it but doesn't run it.

## RHI (`engine/rhi`, `engine/rhi_vulkan`, namespace `interi::rhi`)

- **`interi::rhi` is backend-neutral and never includes Vulkan.**
  - It holds `IDevice`, `ICommandList` and `ISwapchain` with typed handles, plus CPU-side helpers tested against `tests/rhi/MockDevice.h` (gmock):
    - `ResourceManager`: deferred destroy.
    - `CommandListPool`: one pool per thread per frame in flight.
    - `UploadQueue`: a chunked staging ring.
  - `tests/rhi/NullDevice.h` is a do-nothing device for benchmarks.
  - Every new `IDevice`/`ICommandList` virtual needs a `MOCK_METHOD` and a `NullDevice` override in the same change.
- **Timeline.**
  - The device has one queue. Each `Submit` signals the next timeline value; `CompletedValue`/`WaitForValue` track it.
  - A resource released while frame N is recorded retires at `LastSubmittedValue() + 1`.
  - `ResourceManager`'s destructor waits only for what was actually submitted.
- **Bindless model.** One descriptor set is shared by every pipeline, plus 128 bytes of push constants. Indices come from `IDevice::BindlessIndex` / `BindlessStorageIndex`. The tables are declared in `engine/shaders/include/interi/Bindless.hlsli` (aliases share a binding; DXC accepts this):
  - binding 0: raw `RWByteAddressBuffer` storage buffers;
  - binding 1: sampled textures, as `Texture2D` and `Texture2DArray`;
  - binding 2: samplers, including comparison samplers;
  - binding 3: R32Float storage images, one slot per mip.
  `ReflectSpirv` rejects resources outside these tables.
- **Conventions,** pinned by `tests/render/gpu/ConventionTests.cpp` and `tests/rhi_vulkan/ComputeTests.cpp`:
  - Clip space is +Y up with depth 0..1 (the Vulkan backend flips the viewport). Counter-clockwise triangles are front faces.
  - Depth is reverse-Z: clear 0, compare `Greater`.
  - HLSL `LoadMat4` returns M with `mul(M, v)` == engine `M * v`.
  - Texture-to-buffer row pitch is aligned to 256 bytes.
  - Vertices are pulled from buffers, so there is no vertex input layout.
- **`interi::rhi_vulkan`** implements the RHI on Vulkan 1.3: dynamic rendering, sync2, timeline semaphores, descriptor indexing and BDA.
  - volk loads the system loader at runtime, so there is no vulkan-loader port, and `Create` returns `Unsupported` without a driver.
  - VMA handles memory, including heaps and aliasing (placed) resources.
  - Window surfaces come from the caller as opaque `VkSurfaceKHR` values (`SdlPlatform::CreateVulkanSurface`, or `CreateHeadlessSurface` for tests), so this module never links SDL.
  - `engine/rhi_vulkan/.clang-tidy` turns off `bugprone-invalid-enum-default-initialization`, because zero-initialised C structs are the Vulkan idiom.
- **Shaders.** `interi_add_shaders(target SOURCES <Name>.<cs|vs|ps>.hlsl ...)` (`cmake/InteriShaders.cmake`) runs DXC from vcpkg (host-only `directx-dxc`) to produce SPIR-V (SM 6.6, Vulkan 1.3, scalar layout). It embeds the result as `kNameStage` in `<interi/shaders/<Name>.<stage>.h>`. Vertex shaders read the GPU-driven object index through `[[vk::builtin("BaseInstance")]]`.
- **GPU tests** use the `interi::testing::VulkanTest` fixture (`tests/rhi_vulkan/VulkanTestDevice.h`) and labels `integration`/`golden`.
  - They run on lavapipe when it is installed: `VK_DRIVER_FILES` is set in-process.
  - Validation plus synchronization validation is on; any validation error fails the test.
  - They skip without a device unless `INTERI_REQUIRE_GPU` is set, which Linux CI does.
  - Locally, install `vulkan-validation-layers`, or nothing is validated.
  - `tests/rhi_vulkan/VulkanTestSupport.cpp` holds the LSan/TSan suppressions for Mesa, LLVM, the loader and the validation layer. Lavapipe stays loaded so LSan can symbolize its frames.

## Renderer (`engine/render`, namespace `interi::render`)

- **`FrameGraph`** is rebuilt every frame: `Reset`, declare, `Compile`, `Execute`.
  - Passes declare versioned `Read`/`Write` accesses with a `ResourceState`. Compile culls passes whose output reaches no imported resource or `SideEffect` pass.
  - Execute inserts the barriers. Shader writes followed by shader writes get a write-after-write barrier.
  - Transients are placed first-fit in one heap by lifetime, so they alias. An `AliasingBarrier` is emitted where memory changes hands, and once per frame when last frame's placed resources are reused (frames in flight).
  - Barriers are only inserted between passes. A pass that needs one inside itself (e.g. per Hi-Z mip) records it.
- **`ForwardRenderer`** is GPU-driven. Each frame it runs:
  1. Object culling per view (camera, shadow cascades, occluder prepass) into indirect commands.
  2. Hi-Z occlusion from the `occluder` objects' depth.
  3. Clustered light culling (16x9x24 grid).
  4. Cascaded sun shadows.
  5. One PBR forward pass.
  All draws are `DrawIndexedIndirectCount` over a `MeshPool`, which holds all geometry in one vertex buffer and one index buffer.
  - Scene data has one persistently mapped slot per frame in flight. The C++ and HLSL layouts (`ForwardRenderer.cpp`, `shaders/ForwardCommon.hlsli`) must match.
  - CPU references are tested against the GPU: `CascadeSplits`/`FitCascade` (texel-snapped, stable) and `ClusterGrid`/`AssignLights`.
- **Golden images.** `tests/render/golden/*.ppm` are 128x128 images rendered on lavapipe and compared with a per-channel tolerance plus a cap on the fraction of differing pixels.
  - Regenerate with `INTERI_UPDATE_GOLDEN=1`, then look at the image before committing.
  - Mismatches write the actual and diff images to `build/<preset>/golden-output`, which CI uploads on failure.
  - Commit goldens only after CI's lavapipe agrees.

## Dependencies and licensing

Interi is MIT open source. Every dependency must be permissive (allowlist in `tools/license_check/license_policy.json`; no GPL/LGPL/AGPL). Add dependencies to `vcpkg.json` in the milestone that first uses them. After changing dependencies:
- Run `ctest -L license`.
- Regenerate notices: `python3 tools/license_check/gen_notices.py --installed-dir build/<preset>/vcpkg_installed/x64-linux --output THIRD_PARTY_NOTICES.md`.

The `ThirdPartyNotices_UpToDate` test runs on both Linux and Windows, so the notices must match on both platforms. Port names come from vcpkg's SPDX documents, and empty packages are skipped. Host-only build tools such as DXC go under `build_tools` in `license_policy.json`. They are still license-checked (the host triplet too, via `LicenseCheck_HostTools_OnAllowlist`), but they are kept out of the notices, because they ship nothing and appear in the target triplet only when host == target.

<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **Interi** (70024 symbols, 96045 relationships, 481 execution flows).

> Index stale? Run `node .gitnexus/run.cjs analyze --index-only` from the project root — it auto-selects an available runner. No `.gitnexus/run.cjs` yet? Bootstrap with `npx`, `bunx`, or `pnpm dlx` — e.g. `bunx gitnexus@latest analyze` (npm 11 npx crash; #1939).

## Always Do

- **MUST run impact before editing.** Use `impact({target: "symbolName", direction: "upstream"})` or `node .gitnexus/run.cjs impact "symbolName" --direction upstream --repo .`; report callers, processes, and risk. Never substitute grep for graph analysis.
- **MUST analyze graph changes before committing.** Use `detect_changes({scope: "all"})` (MCP) or `node .gitnexus/run.cjs detect-changes --scope all --repo .` (CLI fallback). `partial: true` or `truncated: true` is not a clean check — a zero means unseen, not unaffected; re-run it. For regression review: `detect_changes({scope: "compare", base_ref: "main"})` or `node .gitnexus/run.cjs detect-changes --scope compare --base-ref "main" --repo .`.
- MUST warn on HIGH/CRITICAL `risk` pre-edit; never use `riskSharedAxes` to waive a HIGH/CRITICAL `risk` warning. Compare File/symbol: MCP File omits axes; Graph-RAG expands File.
- **MUST treat `risk: UNKNOWN` as unresolved, not as low.** An empty caller set is not evidence the symbol is unused — it can also mean the callers are not resolvable by the index (plain-object property access, dynamic dispatch, cross-language calls). `impact` pairs `UNKNOWN` with a `riskNote` saying so. Confirm with a text search before treating the symbol as safe to change or delete; do not proceed on the strength of a zero.
- **MUST use `query({search_query: "concept"})` for concepts/flows, `context({name: "symbolName"})` for a named symbol, or `impact` for blast radius, on read-only callers, dependencies, imports, or execution flow.** Graph first; text search only for empty/`UNKNOWN`/literals.
- For security review, `explain({target: "fileOrSymbol"})` lists taint findings (source→sink flows; needs `analyze --pdg`).

## Never Do

- NEVER edit a function, class, or method before MCP/CLI impact analysis.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis, and never read `UNKNOWN` as an all-clear — it means the walk could not answer, which is the one verdict that requires confirming by other means.
- NEVER rename symbols with find-and-replace — use `rename` which understands the call graph.
- NEVER commit before MCP/CLI graph change analysis.

## Resources

| Resource | Use for |
| --- | --- |
| `gitnexus://repo/Interi/context` | Codebase overview, check index freshness |
| `gitnexus://repo/Interi/clusters` | All functional areas |
| `gitnexus://repo/Interi/processes` | All execution flows |
| `gitnexus://repo/Interi/process/{name}` | Step-by-step execution trace |

## CLI

| Task | Read this skill file |
| --- | --- |
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->

## Behavior
1. Think Before Coding

Don't assume. Don't hide confusion. Surface tradeoffs.

Before implementing:

    State your assumptions explicitly. If uncertain, ask.
    If multiple interpretations exist, present them - don't pick silently.
    If a simpler approach exists, say so. Push back when warranted.
    If something is unclear, stop. Name what's confusing. Ask.

2. Simplicity First

Minimum code that solves the problem. Nothing speculative.

    No features beyond what was asked.
    No abstractions for single-use code.
    No "flexibility" or "configurability" that wasn't requested.
    No error handling for impossible scenarios.
    If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.
3. Surgical Changes

Touch only what you must. Clean up only your own mess.

When editing existing code:

    Don't "improve" adjacent code, comments, or formatting.
    Don't refactor things that aren't broken.
    Match existing style, even if you'd do it differently.
    If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:

    Remove imports/variables/functions that YOUR changes made unused.
    Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.
4. Goal-Driven Execution

Define success criteria. Loop until verified.

Transform tasks into verifiable goals:

    "Add validation" → "Write tests for invalid inputs, then make them pass"
    "Fix the bug" → "Write a test that reproduces it, then make it pass"
    "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:

1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.
