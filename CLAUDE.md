# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Interi is an MIT-licensed C++23 modular game engine: the generic core (ECS, jobs, rendering, audio, scenes, chains, the module host and SDK, the editor) plus gameplay engines that live in **modules**. Physics and structural destruction (Red Faction: Guerrilla style) are no longer in the engine: the Jolt Physics Engine and the Destruction Module are gitignored projects in `modules/` (`modules/physics`, `modules/destruction`), each with its own nested git history, README.md and CLAUDE.md. PC targets are Linux (Clang) and Windows (MSVC). PS5/Xbox backends live in separate private repos. Development follows a milestone plan, one milestone per branch (`milestone-N`). `main` is protected and changes reach it through PRs that must pass CI.

## Starting a milestone

The milestone plan is `~/.claude/plans/create-a-plan-for-starry-dove.md` (modular engine and Chain Reaction, M13–M25). It supersedes the old plan `~/.claude/plans/cheeky-kindling-moth.md`, which still governs M12 (the editor); the old M13/M14 move to the Destruction Module's backlog. When asked to start a milestone:

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

- **Module projects** (`/modules/<name>/`, gitignored, each with its own nested git history): the root CMake adds every folder that has a `CMakeLists.txt` (`-DINTERI_MODULE_DIRS=` skips them all; the engine builds and passes without them, which is what CI sees). `modules/physics` is the Jolt Physics Engine and `modules/destruction` the Destruction Module (which declares Jolt as its Physics Engine); their README.md and CLAUDE.md describe them. The engine's vcpkg manifest has no Jolt, so in the engine tree they are skipped unless Jolt is installed; the supported path is the standalone build against an installed engine (`cmake --install`, then `find_package(Interi COMPONENTS Vulkan)` in the module; see their CLAUDE.md files). Verify a change to the engine's module API by building both modules standalone against it.

- Declare modules with `interi_add_module(name SOURCES ... TEST_SOURCES ... DEPS ...)` from `cmake/InteriModule.cmake`. It creates `interi_<name>` (alias `interi::<name>`) and `interi_<name>_tests`. A module without `TEST_SOURCES` fails to configure, and a test enforces this.
- **Shared engine** (`INTERI_BUILD_SHARED`, default ON in every preset): modules declared with `interi_add_module(name SHARED ...)` build as shared libraries. These are core, jobs, ecs, platform, rhi, render, audio, runtime, sdk, values, chain, scene and hotreload: everything the host and a gameplay module both link. The rest stay static.
  - Windows exports every function (`WINDOWS_EXPORT_ALL_SYMBOLS`) but no data. **Keep globals out of public headers**: put them in a `.cpp` behind a function.
  - Each library also gets `<interi/<name>/Export.h>` (`INTERI_<NAME>_API`) for a symbol that needs explicit export.
  - MSVC C4251/C4275 don't arise, because there is no class-level `dllexport`. STL members in exported classes are fine because the toolchain fingerprint guarantees one compiler and standard library.
  - On Windows, executables and DLLs go to `build/<preset>/bin`.
- **Packaging:** `cmake --install` installs the shared libraries, headers, `interi_module_check` and the `Interi` CMake package (`cmake/InteriPackage.cmake`).
  - `find_package(Interi)` provides `interi::<module>` targets, `interi::editor_api` (the header-only editor extension API) and `interi_add_gameplay_module` / `interi_add_module_load_test` (`cmake/InteriGameplayModule.cmake`). `find_package(Interi COMPONENTS Vulkan)` adds the static `interi::rhi_vulkan` and `interi::platform_sdl` (their volk, VMA, SPIRV-Reflect and SDL3 dependencies are found by the config) and `interi::vulkan_testing` (the `VulkanTest` fixture, the golden-image helpers and the sanitizer suppressions; `cmake/InteriVulkanTesting.cmake`) for modules that draw, open windows or run GPU tests. `interi_add_module(... COMPONENT Vulkan)` marks such a static library; `tests/package/vulkan_consumer` builds against it.
  - `interi::build_settings` carries the ABI-relevant global flags (CPU baseline, `-ffp-contract=off`, `INTERI_SIMD_*`, sanitizers) to installed consumers only.
  - `tools/new_module/new_module.py` scaffolds a Module, Extension or Plugin. `samples/modules/starter` is one.
  - The `integration` tests `Package_*` install the engine and build the starter plus a scaffold of each kind out of tree.
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

## Job system (`engine/jobs`)

- **`JobSystem`** is a plain class; tests create many instances with different worker counts. Workers default to hardware threads − 1. Each worker, plus the thread that constructed the system, owns a Chase-Lev `WorkStealingDeque` per `JobPriority` (High/Normal/Low). Other threads submit through MPMC injection queues. Threads register by a unique system id, not by pointer. Thread state is reached through an accessor stored in the object by its constructor (`m_threadState`), so a hot-reloaded module's copy of this code sees the host's workers. `Logger` stamps records the same way.
- **Jobs.** `Submit(fn, &counter, priority)` stores up to 48 bytes of captures inline in a pooled 64-byte `Job`. Jobs must not throw. When the pool is exhausted, `Submit` runs the job inline.
- **Waiting.** `WaitFor(counter)` never idles: it runs other High/Normal jobs, but not Low ones (IO and cooking), unless there are zero workers. Waits nest up to `kMaxWaitNesting` (32). Past that, a waiter only pops its own deque, which keeps stack depth bounded (Windows threads have 1 MB stacks).
- **Sleeping.** Idle workers search (spin), then sleep. A submit wakes a sleeper only when more jobs are queued than workers are searching. A woken worker counts itself as searching, and the last searcher to find work wakes the next (the Go/Tokio protocol). The sleep predicate re-checks `m_queued` under the mutex, a seq_cst Dekker check against `m_sleepers`, so wake-ups are never lost. The code uses seq_cst operations rather than fences everywhere, because TSan doesn't model fences.
- **Shutdown.** The destructor drains every queued job.
- **`TaskGraph`.** Build it once and `Run()` it repeatedly. When asserts are enabled, `Run()` asserts on a cycle and runs nothing.
- **Parallel algorithms.** `ParallelFor`/`ParallelForChunks`/`ParallelReduce`/`ParallelPrefixSum`/`ParallelRadixSort` cut ranges into chunks from range and grain alone, and combine chunk results in order, so results (floats included) are bit-identical at any thread count. Keep it that way.
- **TSan.** The `linux-clang-tsan` preset uses the overlay triplet `cmake/triplets/x64-linux-tsan.cmake`, which builds every vcpkg dependency with Clang and `-fsanitize=thread`. A library with synchronization of its own (the physics engine module's) needs the same triplet, or its uninstrumented primitives produce false race reports; the module carries a copy.

## Platform (`engine/platform`, `engine/platform_sdl`)

- **`interi::platform` never links SDL.** It holds the interfaces and everything testable without a window: `IFileSystem` (`NativeFileSystem`, `MemoryFileSystem`, `VirtualFileSystem`), `AsyncFileReader`, `DynamicLibrary`, `InputEvent`/`IEventSource`, `IWindow` and `InputMapper`. Tests drive input through `tests/platform/FakeEventSource.h` (which also has a gmock `MockWindow`).
- **Virtual paths** are normalised: `/` separators, and `..` or `:` are rejected. `VirtualFileSystem` tries mounts newest first and falls through when a mount lacks the file. Writes go to the newest mount whose prefix matches.
- **`AsyncFileReader`** reads on Low-priority jobs. Never wait for IO from inside a job, because `WaitFor` skips Low jobs while workers exist. Poll with `IsDone`/`TryTake`, or call `WaitAll` from a non-job thread.
- **Input config** (`InputConfig.h`, namespace `interi`): `ApplyInputConfig(json, mapper, InputMerge::Add|Replace)` binds actions and axes by input name ("Space", "Mouse.Left", "Gamepad.South", "Gamepad.LeftX"), all or nothing. `Replace` drops an action's (axis') earlier bindings first, so a project's or the player's config rebinds a module's defaults. `InputMapper` can be inspected and cleared (`FindAction`, `ActionBindings`, `ClearAction`...); `CaptureInputConfig` writes the bindings back as a config, `RebindAction` replaces one binding by name and `FirstPressedInput` is what a "press a key" prompt waits for. `RebindPrompt` is that flow for a settings menu: `BeginFromAction("input.rebind:<action>")` arms it, `Update(events, mapper)` rebinds on the next press (Escape cancels), then the game saves `CaptureInputConfig`.
- **`DynamicLibrary`** tests load a real `MODULE` library (`interi_test_dynlib`) whose path is baked in via `$<TARGET_FILE:...>`.
- **`interi::platform_sdl`** (SDL3) provides `SdlPlatform`, which is the `IEventSource` and opens `IWindow`s; only one may exist at a time. Event translation (`TranslateSdlEvent`, `KeyFromSdlScancode`, `TranslateText`) is pure and unit-tested. Typed text arrives as `TextInputEvent`s (UTF-8, cut at a character boundary) once `SetTextInput(window, true)` is on.
  - Real SDL tests use the `dummy` video driver, so they run headless in CI.
  - SDL3 is pulled in with `default-features: false` plus linux-qualified `x11`/`wayland`. The Linux build needs the X11/Wayland `-devel` packages listed in `ci.yml`'s `FEDORA_PACKAGES`.

## ECS (`engine/ecs`)

- **Components** are any nothrow-movable types, registered on first use of `ComponentId<T>()` (at most `kMaxComponentTypes` = 1024 types). Empty types are storage-free tags.
  - Ids depend on first-use order, so they must never affect layout, iteration order or hashed state. Columns are ordered by name.
  - Identity is the name: `T::kComponentName` if declared (stable across compilers and hot-reloaded modules), else the compiler's type name.
    - Re-registering a name with the same size, alignment and `kComponentVersion` returns the existing id.
    - A changed layout appends a new id under the same name. The registry is append-only.
    - Names in an anonymous namespace (both compilers' spellings) are never shared.
  - Trivially copyable components have null relocate/moveAssign/destroy. Call them through `RelocateComponent`/`MoveAssignComponent`/`DestroyComponent` (memcpy / nothing), so no registry entry points into module code.
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

## Runtime (`engine/runtime`)

- **`FixedStepClock`** takes the elapsed time from the caller and never reads a clock. It uses integer nanoseconds, clamps substeps after a hitch (dropping whole steps but keeping the fraction), and exposes `Alpha()` for `Interpolate(previous, current, alpha)`.
- **`FramePipeline<Snapshot>`** double-buffers: render prep of frame N runs as a job while frame N+1 simulates on the calling thread. Neither side writes what the other reads. The TSan stress test lives in `interi_runtime_stress_tests`.
- **`FollowCamera`** is the orbit and aim math (yaw 0 looks along -Z). **`CameraRig.h`**: camera rigs as data (`{"rigs": [{"name", "kind": orbit|firstPerson|fixed|topDown, ...}]}`, `ParseCameraRigs`; unknown fields warn, anything malformed is an error), `EvaluateRig` for one pose and `CameraController` to switch between them (blends over a fixed time from the caller's dt, `TakeEvents()` reports `CameraSwitchEvent`s).
- **`TriggerVolumes.h`**: physics-free boxes (axis-aligned) and spheres against actors (points with a radius, layer masks). `OverlapTracker::Update` returns only the changes, by (volume id, actor id) with ends before begins and independent of input order, so simple games need no Physics Engine. It is O(volumes x actors).
- **Save games** (`SaveGame.h`): an `ISAV` container (payload hash + versioned archive) of sections sorted by id (`<module id>:<name>`, or the engine's without a colon), each with the version its owner wrote. `SaveRegistry` holds the owners' capture/restore functions (`Add(provider, def)`, `RemoveProvider`, `ExtractProvider`/`Restore`); `Capture(label, previous)` keeps sections nothing registered (a module that is not enabled) when given the previous save; `Validate`/`Apply` refuse the whole save when a section is newer than its owner knows, otherwise restore in id order and report failures per section. `SaveSlots` are numbered files on an `IFileSystem` (`List()` flags corrupt files). The Destruction Module registers its IDSS state as the section `com.interi.destruction:state`.

## Module SDK (`engine/sdk`, namespace `interi::sdk`)

- **Manifests** (`Manifest.h`): `module.json` holds metadata (id `com.author.name`, semver version, author, SPDX license, engine range), `kind` (Module, Extension, Plugin, PhysicsEngine), `library`, `toolchain`, `dependencies`, `conflicts`, `contracts` (Extensions only) and `overrides`. `ParseManifest` follows `ParseMaterials`: missing metadata and unknown sections are warnings, anything malformed is an error.
  - `physicsEngines` (Module, Extension, Plugin) lists the Physics Engines a module supports: `{ id, version range, default, library }` entries, exactly one `default` in a list of several (a single entry is it). Entries may name an adapter `library`. A module without the section is physics-agnostic. A `PhysicsEngine` module declares `extends: { id, version }` (one base) and neither `dependencies` nor `physicsEngines`; it is never listed in a project or depended on (the error says to use `physicsEngines`).
  - `SemVer`/`VersionRange` follow semver 2.0 and npm range syntax (`^`, `~`, comparators, `||`, `*`). A pre-release matches only a range that names one on the same major.minor.patch.
  - `Toolchain.h` builds the fingerprint in the preprocessor: compiler, standard library, debug mode, C++ standard, `INTERI_MODULE_ABI_VERSION`, CPU baseline, shipping, profiling and shared/static engine. Bump the ABI version when `ModuleApi.h`, the registrar, the hook types or the chain registry change.
- **`ResolveModuleGraph`** turns the project's ordered list (top = highest priority) into a `ModuleGraph`.
  - Required dependencies are pulled in, and each implicit Extension goes directly below its lowest dependent. Optional dependencies are used only when the project includes them.
  - Load order puts dependencies first, otherwise lowest priority first.
  - All or nothing: unknown or duplicate modules, missing dependencies, a dependency on a non-Extension, version mismatches, declared conflicts, host incompatibility and cycles each fail the project.
- **Physics Engine selection** (`ModuleGraph.h`, `SelectPhysicsEngine`): a project has one Physics Engine or none. `project.json` may pin one (`physicsEngine: { id, version }`, `sdk::Project::physicsEngine`); otherwise the candidates are the engines named by declaring modules in priority order (a module's default first). The first candidate every declaring module supports wins, at the highest installed version inside every applicable range (several versions of one engine id may be installed; other ids must be unique). An engine that `extends` another satisfies entries naming its bases (bases chosen highest version first, then the first chain that satisfies everyone, `Satisfies(chain, entry)`). Nothing works: `ResolveModuleGraph` fails with `VersionMismatch` (`NotFound` when no candidate is installed), naming each candidate's blockers. A pin with no declaring module still selects its engine.
  - The selected engine joins the graph as an implicit module with its bases directly below it, below the lowest-priority declaring module. `ModuleGraph::physicsEngine`/`physicsEngineChain`/`physicsEngineReason` (Pinned, Default, Fallback)/`physicsEngineChosenBy` record it; `ResolvedModule::loadAfter` holds the effective load edges (dependencies plus declaring module -> engine, engine -> base), which the loader and `ModuleHost::Reload` follow.
  - `CanAddModule(project, available, candidate, host, position)` resolves the project with the candidate added (top by default) and returns why it would be refused; the project is unchanged. The editor's Project menu uses it (`AddProjectModule`).
  - `service.Physics*` contract ids (`kPhysicsContractPrefix`; `PhysicsContracts()` are declared by every host) are reserved: only a `PhysicsEngine` provider may provide them. An extender ranks above its base, so it overrides the base's items one by one.
- **`service.PhysicsQueries`** has a typed signature (`PhysicsQueries.h`): the engine provides a hook `const IPhysicsQueries*()` (raycast, sphere cast, overlap by layer-bit mask) so physics-agnostic modules never name an engine.
- **`ContributionRegistry<T>`** implements the identity rule. Each id holds a stack ordered by rank (engine 0, a module's priority + 1). `Resolve` returns the top; `Provenance` lists the winner and every shadowed provider.
  - Local ids (`<module id>:<name>`) are provided only by their owner. A higher module or Plugin replaces one only with `Override`, declared in its manifest's `overrides` with a range of the owner's version.
  - Shared ids (no `:`) must be declared by the engine (`ModuleHostDesc::engineContracts`) or an Extension's `contracts`. The highest provider wins outright.
  - Plugins only override: an existing shared id, or a declared override.
  - `Subscribe` reports ids whose winner changed. `ExtractProvider`/`Restore` let the host roll back a reload.
- **Hooks** (`Hooks.h`): `HookRegistry` keeps type-erased implementations in a `ContributionRegistry`. `HookPoint<R(Args...)>` calls the winner, and each implementation receives `HookNext`, the one it replaced.
  - `ClassFactory<T>` is a hook returning `std::unique_ptr<T>`.
  - Modules use their `ModuleScope` (`DeclareHook`, `DeclareFactory`, `INTERI_OVERRIDE(scope, targetId, impl)`).
  - Signatures are compared by their compiler spelling.
- **Tests:** the Physics Engine tests (`tests/sdk/PhysicsEngineTests.cpp`) build manifests in code; the host tests (`tests/hotreload/PhysicsEngineHostTests.cpp`) use `tests/hotreload/physics` (engines alpha, beta, alpha_plus extends alpha, with several versions, and modules declaring different combinations, one with adapter libraries; libraries from `modules/PhysicsTestModule.cpp`). `interi_module_check` pins a PhysicsEngine module given on its command line. Writing an engine: `docs/writing-a-physics-engine.md`.
- **Tests** share `tests/sdk/modules`: Extension `com.test.ext`, modules `com.test.a` and `com.test.b`, and Plugin `com.test.plugin`, with conflicting shared ids and overrides. The hotreload tests build their libraries from `tests/hotreload/modules/SdkTestModule.cpp`.

## Hot reload and module host (`engine/hotreload`, namespace `interi::hotreload`)

- **Module ABI v7** (`ModuleApi.h`): a Module, Extension or Plugin is a shared library that links the shared engine and exports `InteriGetModuleApi` via `INTERI_GAMEPLAY_MODULE(Register)`. The host and every module share one component registry, logger, assert handler and job thread state.
  - The loader rejects a library whose `ModuleApi::toolchain` differs from `sdk::EngineToolchainFingerprint()`. Modules are built from source against the engine, which is what makes C++ types safe across the boundary.
  - Rules that keep this safe:
    - Module components are trivially copyable and have a `kComponentName`; `DeclareComponent<T>` checks both. Nothing else enforces it (there is no registry "module mode" any more).
    - Never leave the host anything from module code that outlives the module. Systems, hooks and the state saver are destroyed before the library closes.
    - Dependents reach an Extension through hooks and registry items resolved at run time, not by linking its library: a shadow-copied DLL can't satisfy a link-time import.
    - Physics only through `service.Physics*` (the typed `sdk::IPhysicsQueries` contract) or the selected Physics Engine's own API: the engine has no physics code, and a module never links Jolt on its own.
  - `ModuleContext::physicsEngine` and `registrar.PhysicsEngine()` give the selected engine's id and version (empty id: none).
  - `registrar.Saves()` is the host's `SaveRegistry` (register `<module id>:<name>` sections; see Save games). `registrar.Service(name)` / `ServiceAs<T>` reach host services (`ModuleHostDesc::services`, `IModuleService`): the editor offers `editor.extensions`. The host takes a module's service entries (`Extract`) and save sections back before its library unloads, and restores them if a reload fails.
  - `registrar.Scope()` registers hooks. `registrar.Chains()` is the host's `chain::ChainRegistry` (provide events and functions as `Scope().Provider()`). `registrar.Data()` is the host's `values::DataRegistry` (module data is declared in module.json, not code; a system may keep the reference). `DeclareState(registrar, g_state, {values::Field(...)})` carries a module global's reflected fields across a reload by name (a field whose type changed keeps its new initial value); `SetStateSaver`/`SavedState` carry raw bytes.
- **`ModuleHost`**:
  - `LoadProject(order, manifests)` resolves the graph, finds each `library` in the manifest's folder and then `librarySearchPaths`, and registers in load order.
  - `LoadProject(order, manifests, pin)` loads the selected Physics Engine and its bases first (they are modules in the graph). A declaring module's adapter `library` (the `physicsEngines` entry the engine chain satisfies, the most specific engine first) loads after its main library, into the same scope, scheduler and registrar, and unloads before it; reloads reopen both.
  - `Reload(id)` reopens the module and every loaded module that depends on it (`loadAfter`; reloading an engine reloads the modules built against it).
  - `Load(path)` is a single manifest-less module (`local.module`), for quick experiments and tests.
  - Each transaction:
    1. Copies every library to a unique shadow file and opens it.
    2. Extracts the outgoing modules' hook entries and data.
    3. Registers the new instances next to the old ones: each module's module.json data (re-read on reload) first, then its code.
    4. Migrates components with `ComponentMigrator`s.
    5. Only then destroys the old entries and instances, dependents first, migrates `AttributeBlock`s to their archetypes' new layouts and logs project data that no longer fits (`Revalidate`).
  - Any failure restores what was loaded and leaves the world untouched: unresolvable project, missing library or entry point, ABI or toolchain mismatch, invalid module data, refused registration, a layout change without a migrator, or a migrator refusing.
  - The `HookRegistry` lives as long as the host, so `HookPoint`s survive loads. Each module's systems run in its own Scheduler, in load order. After a reload, `Changed` matches everything once.
  - `ModuleHost::Supported()` is false for a static engine (`INTERI_BUILD_SHARED=OFF`). Loads then fail with Unsupported and the module tests aren't built.
- **`FileWatcher`** polls size and mtime of files or directory trees.
- Tests load seven variants of `tests/hotreload/modules/TestModule.cpp` (7: wrong toolchain) plus the SDK test project. `ModuleHostTest` and `ModuleProjectTest` must stay clean under TSan.
- **Tracy:** a process must have one Tracy instance. On Windows the overlay triplet `cmake/triplets/x64-windows-static-md.cmake` builds only the tracy port as a DLL. A static TracyClient in each module DLL deadlocks under the loader lock: the profile preset hung in `ModuleHostTest`.
- **`kMaxComponentTypes` is 1024**: ids are never recycled. `Query::MatchesArchetype` tests the queried ids rather than whole masks (`BM_QueryMatch_ManyArchetypes`).

## Values (`engine/values`, namespace `interi::values`)

- **One value system** for reflected component fields, module Values, character attributes and table cells.
  - `ValueType` (index = `Value`'s variant index; append only): Bool, U32, Float, Vec3, Quat, String, Int (int32), Color (linear RGBA), Enum (option name), AssetRef (normalised virtual path), EntityRef (stable u64 id, never an ECS handle), List (scalars only).
  - `ValueSchema` = type, list element, enum options, `ValueMeta` (min/max, step, unit, category, tooltip, readOnly, shared), default. `ValidateSchema`/`ValidateValue`; floats must be finite.
  - JSON is schema-directed (`ParseValueJson`, `ValueToJsonText`; floats round-trip bit-exactly); `Serialize(Archive&, Value&)` is bit-exact and rejects unknown types, nested lists and impossible counts (`ArchiveReader::Fail`).
  - Reflection (`Reflection.h`): `registry.Register<T>({Field("x", &T::x, meta)})`, offsets measured on a value-initialised T.
- **Module data** lives in module.json sections (`ModuleContent.h`, `Characters.h`, `DataTables.h` document the formats): `values`, `attributes`, `attributeSets`, `archetypes`, `dataTables`. An `"id"` is a local name (`<module>:<name>`) unless `"shared": true` (a declared contract id); `"overrides"` names a lower module's local item (declared in the manifest) and must keep its type.
- **`DataRegistry`** = `ValueRegistry` + `CharacterRegistry` + `DataTableRegistry`, each a `ContributionRegistry` per item kind plus a **project layer** on top (project > higher module > lower module). The project layer is not a provider: entries are kept even when they stop fitting (inactive: reads fall back to the default; `Revalidate` reports them). `Provenance` lists `project` first.
- **Characters.** An archetype's `AttributeLayout` = its sets' attributes (first wins), defaults: attribute < archetype < project. `AttributeBlock` (engine component, trivially copyable, at most `kMaxAttributes` = 32 slots of key + 32-bit bits + type) is self-describing, so `MigrateAttributeBlock` remaps by attribute key (convert bool/int/float, clamp, new = default, removed = dropped).
- **Tables.** Modules declare columns (`renamedFrom` for renames); the project supplies rows by id. Invalid cells read as the default; `Validate` locates issues by table, row and column (`TableIssue::Text`).
- **Project data** (`ProjectData.h`): JSON with sorted keys and a `schemas` object (per section: values, characters, tables) of the version each item was written against. Same version + invalid data fails the load (all or nothing); another version migrates with warnings. Unknown ids are kept and written back. Items that don't fit are written with their old version so they migrate next time.
- **Cooked data** (`IVAL`: payload hash + versioned Archive): `CookData` resolves values, archetype layouts and table rows for a shipped game; `ReadCookedData` rejects corrupt files.
- **Tests** use the shared SDK test project's data (`tests/sdk/modules`; `tests/values/*` headers describe it). `Values_RoundTrip_BitExact` pins JSON and binary round trips.

## Chain Reaction (`engine/chain`, namespace `interi::chain`)

- **Chains are data** (`ChainAsset.h`): `.chain.json` = event nodes plus links from `<node>.<output>` to a node, with an optional bool `condition` (the "if"). Input bindings are `{"const"|"payload"|"value"|"expr": ...}`; an unbound input takes its schema default. `ParseChain`/`LoadChain`/`WriteChain` (sorted keys).
- **Expressions** (`Expression.h`): lexer + Pratt parser with byte-span errors, typed once at compile. int/u32 wrap, int/u32 mixed with float promote, `x / 0` and `x % 0` are 0, `int()` saturates (NaN → 0). Built-ins min/max/abs/clamp/sqrt/floor/int/float/vec3/length/dot; other calls resolve in the `FunctionRegistry`. **No trigonometry**: chain results must be bit-identical across standard libraries. Watch raw strings in tests: `value(\"id\")"` contains `)"`, so use `R"json(...)json"`.
- **Events** (`Event.h`): `EventDefinition` = kind (Action/Trigger), inputs (`EventParam`; `typeFrom` types an input by the Value another constant input names), outputs with payloads, latent, `Authority`. `EventRegistry`/`FunctionRegistry` are `ContributionRegistry`s; an override must keep the signature. `ChainRegistry` holds both (`ExtractProvider`/`Restore`/`Subscribe`).
  - A native implementation runs during a wave (possibly on a worker) against the pre-step state. It reads inputs, the World and Values, and records outputs (`Fire`, `FireAfter(ns)`) and effects (`Defer`). It must not write shared state, throw, or depend on anything but its inputs and `Seed()`.
- **Core library** (`CoreLibrary.h`): triggers (Start, Tick, InputAction, Timer, ValueChanged, Spawned, Destroyed; a trigger's `key` input matches the raised key), flow (Delay, Timer, Branch, Sequence, Random) and world (SetValue, ModifyValue, Destroy, Move, Rotate, Show, Hide, PlaySound, SpawnPrefab). All are engine shared contract ids (`CoreContracts()`); `ModuleHost` declares them and registers the library.
- **Compiler** (`Compiler.h`): resolves ids through the registries (overrides apply), type-checks every binding per incoming link (payload fields come from the upstream output), validates constants with the input schema, promotes int to float inputs, drops unreachable nodes with a warning. `Diagnostic`s locate node, link, input and span.
- **`ChainExecutor`** (`Executor.h`), a sync point called once per fixed step:
  - Wave 0 is the raised triggers, Start for new chains, Timer/Tick and due continuations, sorted by (chain priority, entity id, sequence). Each wave's link activations are truncated to the step's budget by queue index, then evaluated in parallel (`ParallelFor`, fixed grain) and merged in activation order. A depth limit caps waves; overruns log, drop and set `LastStats()`.
  - Nothing shared is written during waves. Effects run serially at step end, in activation order. Deltas (ModifyValue, Move, Rotate) resolve then, so two hits in one step both land; SetValue is last writer wins. Writes that fail `ValidateValue` are dropped and traced. Structural changes go through `CommandBuffers` (ordered keys), transforms through an overlay flushed as commands.
  - `RuntimeValues` is the run-time layer (run time > project > module). Triggers raised by effects (ValueChanged, Destroyed) fire next step. Random is `HashCombine(seed, step, sequence, link)` per activation.
  - Entities are named by `StableIdComponent` (u64); a chain is bound to an entity or global (0). `HiddenComponent` is the hide tag; `Sounds()` and `Spawns()` are requests for the host (a spawn's root id is allocated in effect order from `ExecutorDesc::firstSpawnId`, 2^62).
  - Programs hold module code, so the executor drops them on any event/function/Value registry change and recompiles at the next step. Continuations survive by node id; a chain that no longer compiles is inactive with diagnostics.
- **Tests:** `tests/chain/ChainTestUtil.h` (a small project with Values and test actions), `ChainHash.h` (binary hashing of trace, Values and world). `interi_chain_stress_tests` (label `stress`) compares 1/4/32 threads and pins `ChainDeterminism.MatchesCrossPlatformGoldenHash`; any change to evaluation, ordering, the library, Random or the scenario changes it. Rotate (sin/cos) stays out of it.

## Scenes (`engine/scene`, namespace `interi::scene`)

- **Objects.** A scene object is an entity with `chain::StableIdComponent` (its id in files and chains), `NameComponent`, `ParentComponent` (an `EntityRef`; 0 = root) and `LocalTransformComponent` (authored, relative to the parent). `interi::Transform` stays the world transform everything else reads; `PropagateTransforms` derives it parents first in id order, treating missing parents and loops as roots (`HierarchyIndex::broken`). Children are ordered by id.
- **Scene v2** (`SceneFile.h`, `.scene.json`): objects by id, components keyed by `kComponentName` (never compiler type names: `interi::Transform` has none and is not saved), fields as values-module JSON, sorted keys. The document keeps field text, so components no loaded module reflects survive a load and save (`KeptComponents`). `InstantiateScene` is all or nothing and fills missing fields with defaults; `CaptureScene` is a fixed point.
- **Primitives** (`Primitives.h`): box, sphere, cylinder, cone, capsule, plane, wedge, stairs, torus as welded `TriangleMesh`es (per-corner normals, a material slot per face group). Every shape but the plane is a closed 2-manifold. Generation uses sin/cos, so test them by properties (`IsClosedManifold`, Euler characteristic, analytic volume), never by bits. `PrimitiveComponent`/`MeshComponent` have a geometry `offset` (pivot editing) and per-slot `colors`.
- **Mesh assets** (`Mesh.h`, `.imesh`): IMSH header, payload hash, versioned archive; `ValidateMesh` on read.
- **Prefabs** (`Prefab.h`, `.prefab.json` = a scene with one root). A saved instance is its root only: own name, parent and placement plus `PrefabInstanceComponent{prefab, overrides}` (`"<source>/<component>/<field>=<json>"`). `ExpandPrefabs` rebuilds the other objects with derived ids `PrefabObjectId(root, source)` (bit 63 set; never allocated by editors) and a `PrefabLinkComponent`; `CollapsePrefabs` turns differences from the prefab (resolved through reflection, so defaults aren't overrides) back into overrides. Not recorded: deleting an expanded object, removing one of its components, entity references other than parent links. Prefabs don't nest.
- **Triggers** (`Triggers.h`): `TriggerVolumeComponent` (shape Box|Sphere, halfExtents, radius, `tag`, layer `mask`) and `TriggerActorComponent` (radius, layers) on scene objects, positioned by their world transform (scale multiplies the size). `SceneRuntime::Step` scans them (sorted by id: chunk order changes as entities are removed) and raises `trigger.Overlap` / `trigger.OverlapEnd` (core library contracts; the volume is the entity, `key` matches its tag, payload `other`) for the next step.
- **Scene state in saves** (`SceneSave.h`): the engine's `scene.state` section holds the run-time Values and each object's local transform and hidden flag. Restore it into a freshly created runtime of the same scene: objects the save doesn't list are destroyed (a collected coin), listed objects the scene lacks are skipped and logged. Chain timers, continuations and the step counter are not saved.
- **`SceneRuntime`** runs a scene: `ChainsComponent{chains, global}` chains in a `ChainExecutor`; each `Step` runs the executor, adopts world transforms chains changed into local ones (relative to the parent's pre-step transform), propagates, then expands `Spawns()` (`SpawnPrefabs`) and raises `trigger.Spawned`, then raises overlap triggers. `SceneRuntimeDesc::world` runs it on a World the caller owns (a game whose module systems share it); `Commands()` are the buffers its Step plays back. Tests compare 1/4/32 workers.
- **Projects** (`sdk/Project.h`): `project.json` = name, ordered modules, module dirs, content folders, startup scene, data file. A module.json `templates` section lists starter project folders.

## Chain editor (`engine/editor`, `engine/editor_ui`, M22)

- **Format additions** (`ChainAsset.h`, still v1; the keys are omitted when empty, so existing files round-trip byte-identical): a node's canvas `pos: [x, y]`, and an `editor` object with `comments` and `groups`. `ChainAsset::operator==` covers them.
- **Chain Functions** (`ChainFunction.h`, `.chainfn.json`): a chain body plus typed `inputs` and `outputs` (each with payload fields). `RegisterChainFunction(registry, provider, fn)` provides three events under `<provider>:<name>`: the call node, `.Entry` (the body's first node; its output payload carries the inputs) and `.Return.<output>` (the exits, typed like the output's payload). `ExpandChainFunctions` inlines calls (nested up to `kMaxFunctionDepth`) into plain nodes before compiling, so the compiler, the executor and the determinism golden hash never see a call. Function inputs are read as `payload.<input>` by the nodes linked from Entry. `SceneRuntimeDesc::functionProvider` (a provider in the registries' ModuleSet) turns this on in `SceneRuntime`: a call event `<provider>:<Name>` that nothing provides is loaded from `<functionFolder>/<Name>.chainfn.json` (default `content/functions`), registered and inlined before the chain is added.
- **Model** (`ChainEditing.h`, tested headless): undoable snapshot edits (`AddChainNode`, `AddChainLink`, `SetChainLinkCondition`, `BindChainInput`, move, rename, remove, comments, groups, copy and paste). An edit that can break typing is checked by compiling the asset before and after (nodes no link reaches hang off a probe Start trigger): the edit is refused when it introduces a compile error, so the compiler's rules decide (int promotes to float, float doesn't narrow). `ValidateChain` + `DiagnosticsForNode/Link` map compile diagnostics onto the canvas. `BuildPalette` lists every event with its module, category and "provided by A, overrides B" indicator; `FilterPalette` searches. `PayloadChoices`/`ValueChoices`/`FilterChoices` are the type-filtered binding pickers. `AutoLayoutChain` places a hand-written file's nodes.
- **Debugger** (`ChainDebugger.h`): reads `ChainExecutor::Trace()` after each step (`ScenePlaySession::Start(..., trace = true)` + `SetStepObserver`). Heat for firing nodes and links, breakpoints on nodes and links, an event timeline, watches. Breakpoints work at step granularity: the step finishes, then the session pauses; nothing pauses mid-wave. `SceneRuntime::ChainPath(id)` maps a trace record's chain to its asset path.
- **Canvas** (`editor_ui/ChainEditor.h`): drawn with `ImDrawList` only (no node-editor dependency): pan (middle drag), zoom (wheel), minimap, links as beziers (conditional ones dashed with an "if" badge), palette with drag and drop, an inspector with the condition editor and binding pickers, a Problems tab, and the debugger tabs. `level_editor --chain <file> [--chain-breakpoint <node>]` opens it (with a breakpoint, `--play 1` pauses at the step that hits it); the dock layout is `interi_editor_layout_v9.ini`.

## Module Manager (`engine/editor`, `engine/editor_ui`, M23; `docs/module-manager.md`)

- **Model** (`ModuleManager.h`, headless): `BuildModuleManagerModel` lists installed modules as Enabled (project order), Implicit, Engine (a Physics Engine: never enabled by hand), Available or Incompatible (with `CanAddModule`'s reason). `EnableModule` / `DisableModule` / `MoveModule` resolve the changed project first and return a `ModuleChange` (the project, modules added and removed, Physics Engine before and after, `needsTrust`) or the refusal; disabling a module a loaded module requires is refused ("required by A, B"). The input project is never changed.
- **Trust**: native code (a manifest with a `library`) runs with full trust and no sandbox. `TrustStore` (`trusted_modules.json` beside the preferences) keys a module by id + author + license; `NeedsTrust`. Every enable path is confirmed, including implicit Extensions and a newly selected Physics Engine, and the Project menu's "Add module" queues an Enable into the Module Manager. Opening a project that already lists modules does not ask.
- **Views** (`ModuleConflicts.h`): `BuildDependencyGraph` (layers by load order, Requires/Optional/Engine edges) and `CollectConflicts` (items with more than one real provider, from the Chain, Data and Hook registries, project overrides excluded; plus each manifest `overrides` entry's applied state).
- **Plugin authoring** (`PluginAuthoring.h`): `GeneratePluginStub` for a hook or Event (local ids get an `overrides` declaration and `INTERI_OVERRIDE` / `Events().Override`; shared ids are provided; an Event stub always replaces the copied definition's implementation). `sdk::ExtractHookSignature` / `ParseFunctionType` (`sdk/HookSignature.h`) read the hook type out of Clang, GCC and MSVC spellings. The generated stubs for fixed inputs are committed in `tests/editor/plugin_stubs` and built and loaded over the starter in CI (`PluginStub_*_LoadsOverStarter`); `INTERI_UPDATE_GOLDEN=1` rewrites them (they must stay clang-format clean). `RunDevLoop` (callbacks: stop play, build, reload, replay; stops at the first failure) keeps shells out of the library: `tools/editor` runs the build command with popen. `LibraryWatcher` reloads rebuilt libraries.
- **Docs**: `hotreload::GenerateModuleDocs(manifest, chains, hooks)` (Markdown: metadata, Events, Functions, hook points, Values, Tables); `interi_module_check --docs <dir>` writes a page per loaded module and prints events as `event <id>: <providers>`.
- **Panel** (`editor_ui/ModuleManagerPanel.h`): tabs Modules, Dependency graph, Conflicts, Plugin, Docs; it asks `ModuleManagerView::plan` for a `ModuleChange` and returns requests (`apply`, `trust`, `createPlugin`, `generateDocs`, `saveDocs`, `runLoop`); `EditorApp` applies them after the frame is drawn. `state.Queue(action)` / `ConfirmPending()` drive it headless. The chain palette's right-click "Create Plugin override..." sets `ChainEditorResult::overrideEvent`. `level_editor --module-manager 1` opens it.

## HUD and menus (`engine/hud`, namespace `interi::hud`)

- **Documents** (`.hud.json`, `HudDocument.h`): `{"version": 1, "referenceSize": [w, h], "widgets": [...]}` with widgets text, bar, image, button and list, each with an `anchor`, `offset`, `size` (reference pixels, scaled by the smaller screen ratio), `color`/`background`, and optional `visibleIf` (a bool Value). Text and button labels substitute Values with `{valueId}` or `{valueId|2}` (decimals; `|` because Value ids contain `:`), `{{` is a literal brace. A bar binds a numeric Value, a list a List Value. `ParseHud` warns on unknown fields and fails on anything malformed (duplicate ids, a bar without `max > min`, a button without an `action`); `WriteHud` is canonical and round-trips; `BoundValues` lists what a document reads.
- **Layout** (`HudLayout.h`): `BuildFrame(document, width, height, values, highlight)` is pure and produces a draw list (`DrawItem`: Rect, Text, Image) plus the visible buttons in focus order. Values come through `chain::IValueReader` (`RuntimeValues` or a registry). Nothing here draws: the editor draws it with ImGui (`DrawHudFrame`), a game with its own renderer.
- **Interaction** (`HudController.h`): hover, press/release (a click needs the release on the pressed button), keyboard/gamepad focus that wraps over visible buttons; `TakeActions()` returns the activated buttons' action strings. **`MenuStack`** keeps menus (main, pause, settings are just documents) on a stack and handles `menu.push:<name>`, `menu.replace:<name>` and `menu.pop` itself; every other action (`game.start`, `game.resume`, `app.quit`, `input.rebind:moveRight`) goes back to the game. A menu keeps its focus when you return to it.

## Game sessions (`engine/game`, namespace `interi::game`)

- **`ProjectModules`** loads a project's modules: manifests from the project's `moduleDirs` (`LoadProjectManifests`), a `ModuleHost` on a World, and the project's data file. All or nothing. The editor uses it for play-in-editor (`editor::EditorModules`, on a scratch World); `GameSession` uses it for games.
- **`GameSession`** opens a project folder (`project.json`): modules and data, the startup scene in a `SceneRuntime` on the modules' World, project files mounted at the root (paths are project-relative, `content/chains/x.chain.json`). `Step()` runs the modules' systems then the scene; `RaiseInputAction` feeds `trigger.InputAction`. `Restart()` loads the scene again (the running one stays if the new one doesn't parse). `SaveToSlot`/`LoadFromSlot` use `ModuleHost::Saves()` (the engine's `scene.state` plus the modules' sections); loading checks versions first, restarts the scene, then applies, so a bad slot changes nothing. Tests compare results at 0/1/4/16 workers.
- **Starter sample** (`samples/modules/starter`, committed): "Coin Run", a top-down collect-the-coins game. The module declares Values (`coins`, `total`, `score`, `won`), a `CoinKinds` data table, a chain function (`CoinValue`, reads the table), a native action (`LevelComplete`, counts levels in module state that survives reloads) and a save section. `templates/coins` is the project (scene with trigger-volume coins, chains, HUD and menus, input and camera rigs, table rows). It builds in tree (root CMake) and out of tree (`Package_Starter_BuildsOutOfTree`); `interi_starter_sample_tests` (label `integration`, run by CI) plays it headless: collects every coin, wins, saves and loads, lays out the HUD and menus, rebinds a key, switches cameras.

## Audio (`engine/audio`, namespace `interi::audio`)

- **miniaudio** (header-only, Unlicense OR MIT-0) only drives the device and decodes WAV/FLAC/MP3 (`DecodeClip`, `LoadClip`). Its implementation is `src/MiniaudioImplementation.cpp`, and every audio TU includes it through the private `src/Miniaudio.h` (one set of `MA_NO_*` defines). No public header includes it.
- **Pure, tested parts:**
  - `Spatializer`: inverse-distance attenuation, offset so it reaches zero at `maxDistance` without a step; equal-power pan along the listener's right (forward x up).
  - `VoiceLimiter`: fixed slots. A newcomer evicts the lowest priority only if it ranks strictly higher (ties never churn); among equal lowest the oldest goes.
  - `Mixer`: mono clips into interleaved stereo float, gain ramps over one block, stops fade out over one block.
  - `Synthesize`: procedural impact, explosion, rumble and creak clips (integer `Random`), so no recorded audio ships.
- **`AudioEngine`** (`Default`, `Null` or `Offline` backend). Every call comes from one game thread. Commands reach the audio thread through an SPSC queue, and ended voices come back through a second one.
  - The game side keeps each playing clip's `shared_ptr`, plus a retired list for cut voices, until the audio thread reports the voice ended. A clip is never freed on the audio thread.
  - Voices are ranked by priority x audible gain. Inaudible plays are culled.
  - Tests use `Offline` (`Render` mixes on the caller). The TSan test `AudioEngineStress` (`interi_audio_stress_tests`, label `stress`) runs on the null device. Never open a real device in tests: CI has no sound card.
- **Designated initializers:** clang's `-Wmissing-designated-field-initializers` and tidy's `readability-redundant-member-init` conflict over class-type members without initializers. Engine code fills every field; tests assign fields instead.

## RHI (`engine/rhi`, `engine/rhi_vulkan`, namespace `interi::rhi`)

- **`interi::rhi` is backend-neutral and never includes Vulkan.**
  - It holds `IDevice`, `ICommandList` and `ISwapchain` with typed handles, plus CPU-side helpers tested against `tests/rhi/MockDevice.h` (gmock):
    - `ResourceManager`: deferred destroy.
    - `CommandListPool`: one pool per thread per frame in flight.
    - `UploadQueue`: a chunked staging ring. `UploadTexture` stages one mip at the 256-byte row pitch for `CopyBufferToTexture`.
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
  - Texture-to-buffer and buffer-to-texture row pitch is aligned to 256 bytes.
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
  - Locally, install `vulkan-validation-layers`, or nothing is validated. Without root, build the vcpkg port `vulkan-validationlayers` in classic mode into a scratch directory and set `VK_ADD_LAYER_PATH` (its `share/vulkan/explicit_layer.d`) and `LD_LIBRARY_PATH` (its `lib`).
  - HLSL `discard` compiles to demote-to-helper (SM 6.6), so the device requires `shaderDemoteToHelperInvocation`.
  - `tests/rhi_vulkan/VulkanTestSupport.cpp` holds the LSan/TSan suppressions for Mesa, LLVM, the loader and the validation layer. Lavapipe stays loaded so LSan can symbolize its frames. A process has one TSan list: an executable that needs another library's suppression defines `INTERI_EXTRA_TSAN_SUPPRESSIONS` (a string literal; `interi::physics_testing` sets it for Jolt) and `VulkanTestSupport.cpp` appends it.

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
  - Scene data has one persistently mapped slot per frame in flight, sized by `ForwardSettings::maxObjects`. The C++ and HLSL layouts (`ForwardRenderer.cpp`, `shaders/ForwardCommon.hlsli`) must match.
  - `Material` is constant per object. Meshes have no UVs, so an `albedoTexture` is mapped triplanar in world space. `opacity` < 1 is a 4x4 screen-door dither (fading debris) in the opaque pass. `ForwardOutputs::depth` is sampleable by later passes.
- **Textures** (`Texture.h`): `GenerateMips` (exact integer box filter) and `CreateTexture` (upload plus transitions). Pipelines pick a `BlendMode` (Opaque, Alpha, Additive).
- **`GpuParticleSystem`** (`Particles.h`): a ring of 64-byte particles in a GPU buffer. Each frame a compute pass copies the emitted particles in and a second integrates every live one; `IntegrateParticles` is the CPU reference (same operations and order; GPU tests compare with a tolerance, since GPU compilers may fuse). Quads are drawn alpha, then additive, as soft particles tested against the forward depth in the pixel shader (no depth attachment).
- **`DecalRenderer`** (`Decals.h`): deferred box decals after the forward pass. The pixel shader rebuilds the surface position from the depth, keeps what is inside the box and blends a crack (along local z = 0) or scorch pattern. `DecalBox` orients a box along a normal.
  - CPU references are tested against the GPU: `CascadeSplits`/`FitCascade` (texel-snapped, stable) and `ClusterGrid`/`AssignLights`.
- **Golden images.** `tests/render/golden/*.ppm` are 128x128 images rendered on lavapipe and compared with a per-channel tolerance plus a cap on the fraction of differing pixels.
  - Regenerate with `INTERI_UPDATE_GOLDEN=1`, then look at the image before committing.
  - Mismatches write the actual and diff images to `build/<preset>/golden-output`, which CI uploads on failure.
  - Commit goldens only after CI's lavapipe agrees.

## Editor (`engine/editor`, `engine/editor_ui`, `tools/editor`, namespace `interi::editor`)

- **Module split.** `interi::editor` is the editor without UI (tested headless): reflection, the scene document,
  undo, modeling, prefabs, camera and gizmo math, picking, projects and editor services. `interi::modeling` (static, editor-only) does CSG through Manifold (Apache-2.0; its
  `.clang-tidy` drops `bugprone-macro-parentheses` for Manifold's command-line `MANIFOLD_PAR`). `interi::editor_ui` is the Dear
  ImGui layer (imgui 1.92 docking, no SDL/Vulkan bindings: input comes from `InputEvent`s, drawing goes through the
  RHI). `tools/editor` builds `level_editor` (the `interi_editor` target name is the core library).
- **Reflection** is `interi::values`' (`editor/Reflection.h` aliases it: `FieldType` = `values::ValueType`,
  `FieldValue` = `values::Value`). `RegisterEngineReflection` covers the scene components (not the world Transform),
  primitives, meshes, prefabs, chains and triggers; modules add theirs through `IEditorExtensions::Reflection()`. Enum
  fields carry their options (`values::EnumField`). Field widgets for every value type are `ValueWidget` (`editor_ui/ValueWidgets.h`).
- **Document.** `EditorScene` holds a scene v2 document as ECS entities (generic objects, ids = stable ids) and keeps
  world transforms and the `HierarchyIndex` current. Snapshots and `Restore` cover reflected components plus kept
  (unknown) ones; prefab-derived ids never advance `NextId`. `EditorDocument` opens `.scene.json` only (expanding prefab
  instances; InvalidArgument for any other path). Saving collapses prefab instances. Multi-selection:
  `SelectedIds`, `SelectedRoots` (no selected ancestor; what operations act on), `ToggleSelected`.
- **Undo.** `UndoStack` executes `ICommand`s; commands with the same non-zero merge key merge until `Seal()` (the UI
  seals on widget release), a capacity drops the oldest, `MarkClean`/`IsDirty` track the save point. Edits go through
  `EditField` / `EditTransform` (world target, stored local) / `Reparent` / `AddComponent` / `RemoveComponent` /
  `EditorDocument::Add` / `DeleteSelection` (subtrees, one `MacroCommand`), and project data edits through
  `DataEditing.h` (`EditValue`, `EditAttributeDefault`, `EditCell`, `AddTableRow`, `RemoveTableRow`; checked before
  they are recorded).
- **Project data.** `ProjectData::Create(graph)` builds a `DataRegistry` from manifests (no libraries). The Values,
  Characters and Data Tables panels (`editor_ui/DataPanels.h`) show provenance badges (`ProvenanceBadge`/`Tooltip`).
  `level_editor --modules <dir> [--project a,b] [--project-data file.json]` edits them; the file is saved with the
  level.
- **ImGui renderer.** Implements `ImGuiBackendFlags_RendererHasTextures` (atlas re-uploaded synchronously when it
  changes). `ImTextureID` = bindless index + 1 (0 is invalid). Vertex colours are linearised for sRGB targets.
  Headless tests use `tests/editor_ui/ImGuiTestContext.h` (acknowledges texture requests; set
  `io.ConfigInputTrickleEventQueue = false` to apply queued input in one frame).
- **Viewport.** The forward renderer draws into an offscreen texture shown with `ImGui::Image`; overlays (gizmos,
  module overlays) are draw-list lines projected with the same `OrbitCamera`. The `MeshPool` only appends, so when it
  fills the editor waits for the GPU, `Clear`s it and re-adds meshes as they are drawn (call `EditorRenderer::AddMesh`
  every frame for drawn meshes). Objects whose `MeshComponent` asset is not an `.imesh` are left to the module that
  owns that asset kind (its render feature).
- **Modeling** (`Modeling.h`): group/ungroup, duplicate and array (subtree copies, entity references inside remapped),
  align, distribute, mirror (negative scale on the local axis nearest the mirror axis, rotation `M R M_k`: exact),
  snap, pivot (origin moves; geometry offset and children compensate) and CSG (result baked to `meshes/csg_<n>.imesh`;
  the file is not removed by undo). Every operation is one undo step.
- **Prefabs** (`Prefabs.h`): create from the selected object (writes `prefabs/<name>.prefab.json`, the object becomes
  an instance), instance (drag a prefab asset), revert (undoable) and apply (rewrites the prefab, reloads the scene,
  clears undo).
- **Play-in-editor** (`ScenePlay.h`): Play / Pause / Step / Stop (F5 / F6 / F10) run the scene in a `SceneRuntime`
  of its own (collapsed then re-expanded, so unsaved instance edits play), physics-free; the document is never
  touched. On Stop (`EndPlay`), the "keep changes made during play" preference either keeps the Values chains set
  (`KeepRuntimeValues`, undoable) and your edits, or undoes the edits made during play. Modules' native
  events aren't available yet (no libraries load in the editor): chains use the core library.
- **Modules and extensions.** `EditorModules` (`EditorModules.h`) loads the open project's modules into a `ModuleHost` on a scratch World (`--module-libs <dir>[,<dir>]` adds library search paths; a project whose modules don't load falls back to the core chain library with a warning). Play-in-editor then plays with the host's `ChainRegistry` and Values, so a module's native events and functions work; module systems don't run in the editor yet. The data panels edit the host's `DataRegistry`. The extension API is `editor_ui/EditorExtensionsApi.h`: abstract and header-only (`IEditorExtensions`, the `editor.extensions` host service; `IEditorDocument`, `IViewportCanvas`, `DebugDraw`, no ImGui or editor includes), so a module library links no editor symbol and everything crosses by virtual calls (`EditorExtensions` in `EditorExtensions.h` is the editor's implementation; a test module library, `interi_editor_extension_test_module`, registers through it). Modules register panels, viewport overlays, tools (an active tool owns the viewport mouse; `ActivateTool` switches it), per-frame updates (`AddUpdate`), component reflection (`Reflection()`) and render features with ids `<module id>:<name>`; the entries go back to the editor before the library unloads. A render feature contributes world-space `DebugDraw` lines and boxes (`contribute`) and/or scene geometry (`render`: `RenderObject`s and `Decal`s into the forward pass, uploaded through the `RenderFeatureContext` into the editor's `MeshPool`; `OutOfMemory` makes the editor clear the pool, raise `generation` and run every feature again; `renderer` is a weak token that expires with the editor's renderer, so a feature releases its GPU resources only while it is alive; `replacesScene` asks the editor to draw only the features' output, e.g. a preview). `IEditorDocument` gives a module the document's World, selection, files, data directory, jobs, scene path, camera focus and `AddMeshObject`. Panels draw with ImGui, so a module links its own copy of ImGui (same version as the editor's) and adopts the editor's context from `PanelContext` (`ImGui::SetCurrentContext`, `SetAllocatorFunctions`); overlays, tools, updates and render features need no ImGui. The Destruction Module is the reference extension (`modules/destruction/libs/destruction_editor`).
- **HUD designer** (`HudEditing.h`, `editor_ui/HudDesigner.h`): undoable, validated edits of a `HudDocument` (an edit that wouldn't re-parse is refused, drags merge into one undo step) and the panel with the widget list, fields, a live preview and warnings for Values the project doesn't define. The app's HUD menu loads and saves a `.hud.json` through the project files.
- **Services** (`EditorServices.h`, `editor_ui/ServicePanels.h`): preferences (`editor.json` in the user config
  folder: recent projects, autosave, play, snapping, Tracy frame marks), `AutosaveTimer` (writes
  `<scene>.autosave` without marking clean), `LogBuffer`/`LogBufferSink` (console panel; thread-safe, the sink runs
  on the logger's flush thread). Projects (`Projects.h`): `CreateProject` (empty or from a module template),
  `LoadProjectModules`; the Project menu opens, creates and lists recent projects, adds installed modules (`AddProjectModule`, refused with the reason in the console and the menu when no Physics Engine fits them all), the Project Settings panel shows the selected Physics Engine, its version and why (`DescribePhysicsEngine`) with a pin dropdown (`PinProjectPhysicsEngine`), and `--project-file <dir>` opens
  one at startup (the folder is mounted over the data tree: its paths are project-relative and writes go there).
- **Smoke run:** `level_editor --video-driver offscreen --frames 120 --screenshot out.ppm` (writes the last frame).
  `--project-file <dir> --play 1` opens a project and plays its startup scene (prints `Played N steps`);
  `--module-libs <dir>` adds library search paths for the project's modules. The dock layout is saved in the temp directory
  (`interi_editor_layout_v9.ini`; bump the version when windows are added).

## Animation, navigation, localization and AI (M24; `docs/animation-navigation-localization.md`)

- **`engine/anim`** (`interi::anim`, shared): `.skeleton.json` / `.clip.json` assets (M21's importers are not on this base; anim defines the formats), `SampleClip` / `BlendPoses` / `ComputeSkinMatrices` (nlerp, no trig: bit-identical), `.animstates.json` graphs in `AnimationSet`s bound to character archetype ids (`AnimationLibrary`), `AnimatorComponent` stepped by `StepAnimator`. `anim.Play` / `anim.Trigger` chain events only record a request on the component. Sets come from module.json `animationSets` (`LoadModuleAnimations`).
- **`engine/nav`** (`interi::nav`, shared): Recast bake and Detour queries behind `NavMesh` (vcpkg `recastnavigation`, zlib, private; one `dtNavMeshQuery` per call so queries are thread-safe; `.inav` cook). Recast float math is reproducible per toolchain, not across platforms: ship the cooked mesh. `NavAgentComponent` + `StepNavAgents` (stable-id order, re-plans each step) and `RaiseNavTriggers` (`nav.Arrived` / `nav.Failed`); events `nav.FindPath`, `nav.MoveTo`, `nav.Stop` read the mesh from `ModuleHost::Navigation()` (`NavSource`, swap it between steps only).
- **`ModuleHost`** declares the anim, nav and AI contract ids and registers the anim and nav events itself (as the engine). So does the editor's own registry.
- **AI** (`sdk/Ai.h`): module.json `behaviours` and `computerPlayers` -> `AiCatalog` (full ids, validated). The engine spawns nothing: a game module provides the hook `ai.SpawnComputerPlayer` and `SpawnComputerPlayers` calls it with slots.
- **`engine/loc`** (`interi::loc`, shared): `.strings.json` per language, `Localization` (fallback language, later table wins per key, `RemoveProvider`, `MissingKeys`, `language.set:<code>` via `ApplyLanguageAction`), module.json `localization` (`LoadModuleStrings`). The HUD binds `{@key}` (translation, whose `{valueId}` placeholders are filled from Values; keys inside translations are not expanded again); `BuildFrame` / `HudController::SetLocalization` take the `Localization`, and `BoundValues` skips keys (`LocalizationKeys` lists them).

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
