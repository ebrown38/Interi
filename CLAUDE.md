# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Interi is an MIT-licensed C++23 game engine built only for large-scale, multithreaded structural destruction (Red Faction: Guerrilla style). PC targets are Linux (Clang) and Windows (MSVC). PS5/Xbox backends live in separate private repos. Development follows a milestone plan, one milestone per branch (`milestone-N`). `main` is protected and changes reach it through PRs that must pass CI.

## Starting a milestone

The milestone plan is `~/.claude/plans/create-a-plan-for-starry-dove.md` (modular engine and Chain Reaction, M13–M24). It supersedes the old plan `~/.claude/plans/cheeky-kindling-moth.md`, which still governs M12 (the editor); the old M13/M14 move to the Destruction Module's backlog. When asked to start a milestone:

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
- **Shared engine** (`INTERI_BUILD_SHARED`, default ON in every preset): modules declared with `interi_add_module(name SHARED ...)` build as shared libraries. These are core, jobs, ecs, platform, rhi, render, audio, runtime, sdk, values, chain and hotreload: everything the host and a gameplay module both link. The rest stay static.
  - Windows exports every function (`WINDOWS_EXPORT_ALL_SYMBOLS`) but no data. **Keep globals out of public headers**: put them in a `.cpp` behind a function.
  - Each library also gets `<interi/<name>/Export.h>` (`INTERI_<NAME>_API`) for a symbol that needs explicit export.
  - MSVC C4251/C4275 don't arise, because there is no class-level `dllexport`. STL members in exported classes are fine because the toolchain fingerprint guarantees one compiler and standard library.
  - On Windows, executables and DLLs go to `build/<preset>/bin`.
- **Packaging:** `cmake --install` installs the shared libraries, headers, `interi_module_check` and the `Interi` CMake package (`cmake/InteriPackage.cmake`).
  - `find_package(Interi)` provides `interi::<module>` targets, `interi_add_gameplay_module` and `interi_add_module_load_test` (`cmake/InteriGameplayModule.cmake`).
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

## Job system (`engine/jobs`, `engine/jobs_jolt`)

- **`JobSystem`** is a plain class; tests create many instances with different worker counts. Workers default to hardware threads − 1. Each worker, plus the thread that constructed the system, owns a Chase-Lev `WorkStealingDeque` per `JobPriority` (High/Normal/Low). Other threads submit through MPMC injection queues. Threads register by a unique system id, not by pointer. Thread state is reached through an accessor stored in the object by its constructor (`m_threadState`), so a hot-reloaded module's copy of this code sees the host's workers. `Logger` stamps records the same way.
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

## Runtime (`engine/runtime`, `game/sandbox`)

- **`FixedStepClock`** takes the elapsed time from the caller and never reads a clock. It uses integer nanoseconds, clamps substeps after a hitch (dropping whole steps but keeping the fraction), and exposes `Alpha()` for `Interpolate(previous, current, alpha)`.
- **`FramePipeline<Snapshot>`** double-buffers: render prep of frame N runs as a job while frame N+1 simulates on the calling thread. Neither side writes what the other reads. The TSan stress test lives in `interi_runtime_stress_tests`.
- **`interi_sandbox`** (`game/sandbox`) is the playable demo: a third-person character with the sledgehammer, remote charges and rockets, and a truck (E enters and exits it, or brings a new one after a wreck) in `data/levels/sandbox.json` (ground, pillars, three pre-fractured walls generated at startup and served from a `MemoryFileSystem` mounted at `generated/`, and `player`/`vehicle` spawn points). Sounds are synthesized at startup; `--audio-driver default|null|none` (default falls back to null).
  - Bindings are in `data/gameplay/input.json`: F5 saves the destruction state, F9 reloads the level and restores it, F6 reloads the module.
  - Debris expiry lives in the hot-reloadable `game/sandbox_module` (`interi_sandbox_module`). Components shared with the module are in `SandboxComponents.h`.
  - `--hot-reload 1` rebuilds the module (`cmake --build` on the build dir) when its sources change. The module library is always watched.
  - Smoke run: `--video-driver offscreen --audio-driver null --frames N --auto-spawn 1 --validation 1` (auto-spawn fires rockets and charges at the walls and drives the truck into the middle one). `--video-driver dummy` has no Vulkan, so it runs without drawing. CI builds it but doesn't run it.

## Module SDK (`engine/sdk`, namespace `interi::sdk`)

- **Manifests** (`Manifest.h`): `module.json` holds metadata (id `com.author.name`, semver version, author, SPDX license, engine range), `kind` (Module, Extension, Plugin), `library`, `toolchain`, `dependencies`, `conflicts`, `contracts` (Extensions only) and `overrides`. `ParseManifest` follows `ParseMaterials`: missing metadata and unknown sections are warnings, anything malformed is an error.
  - `SemVer`/`VersionRange` follow semver 2.0 and npm range syntax (`^`, `~`, comparators, `||`, `*`). A pre-release matches only a range that names one on the same major.minor.patch.
  - `Toolchain.h` builds the fingerprint in the preprocessor: compiler, standard library, debug mode, C++ standard, `INTERI_MODULE_ABI_VERSION`, CPU baseline, shipping, profiling and shared/static engine. Bump the ABI version when `ModuleApi.h`, the registrar or the hook types change.
- **`ResolveModuleGraph`** turns the project's ordered list (top = highest priority) into a `ModuleGraph`.
  - Required dependencies are pulled in, and each implicit Extension goes directly below its lowest dependent. Optional dependencies are used only when the project includes them.
  - Load order puts dependencies first, otherwise lowest priority first.
  - All or nothing: unknown or duplicate modules, missing dependencies, a dependency on a non-Extension, version mismatches, declared conflicts, host incompatibility and cycles each fail the project.
- **`ContributionRegistry<T>`** implements the identity rule. Each id holds a stack ordered by rank (engine 0, a module's priority + 1). `Resolve` returns the top; `Provenance` lists the winner and every shadowed provider.
  - Local ids (`<module id>:<name>`) are provided only by their owner. A higher module or Plugin replaces one only with `Override`, declared in its manifest's `overrides` with a range of the owner's version.
  - Shared ids (no `:`) must be declared by the engine (`ModuleHostDesc::engineContracts`) or an Extension's `contracts`. The highest provider wins outright.
  - Plugins only override: an existing shared id, or a declared override.
  - `Subscribe` reports ids whose winner changed. `ExtractProvider`/`Restore` let the host roll back a reload.
- **Hooks** (`Hooks.h`): `HookRegistry` keeps type-erased implementations in a `ContributionRegistry`. `HookPoint<R(Args...)>` calls the winner, and each implementation receives `HookNext`, the one it replaced.
  - `ClassFactory<T>` is a hook returning `std::unique_ptr<T>`.
  - Modules use their `ModuleScope` (`DeclareHook`, `DeclareFactory`, `INTERI_OVERRIDE(scope, targetId, impl)`).
  - Signatures are compared by their compiler spelling.
- **Tests** share `tests/sdk/modules`: Extension `com.test.ext`, modules `com.test.a` and `com.test.b`, and Plugin `com.test.plugin`, with conflicting shared ids and overrides. The hotreload tests build their libraries from `tests/hotreload/modules/SdkTestModule.cpp`.

## Hot reload and module host (`engine/hotreload`, namespace `interi::hotreload`)

- **Module ABI v3** (`ModuleApi.h`): a Module, Extension or Plugin is a shared library that links the shared engine and exports `InteriGetModuleApi` via `INTERI_GAMEPLAY_MODULE(Register)`. The host and every module share one component registry, logger, assert handler and job thread state.
  - The loader rejects a library whose `ModuleApi::toolchain` differs from `sdk::EngineToolchainFingerprint()`. Modules are built from source against the engine, which is what makes C++ types safe across the boundary.
  - Rules that keep this safe:
    - Module components are trivially copyable and have a `kComponentName`; `DeclareComponent<T>` checks both. Nothing else enforces it (there is no registry "module mode" any more).
    - Never leave the host anything from module code that outlives the module. Systems, hooks and the state saver are destroyed before the library closes.
    - Dependents reach an Extension through hooks and registry items resolved at run time, not by linking its library: a shadow-copied DLL can't satisfy a link-time import.
    - Modules never call Jolt.
  - `registrar.Scope()` registers hooks. `registrar.Data()` is the host's `values::DataRegistry` (module data is declared in module.json, not code; a system may keep the reference). `DeclareState(registrar, g_state, {values::Field(...)})` carries a module global's reflected fields across a reload by name (a field whose type changed keeps its new initial value); `SetStateSaver`/`SavedState` carry raw bytes.
- **`ModuleHost`**:
  - `LoadProject(order, manifests)` resolves the graph, finds each `library` in the manifest's folder and then `librarySearchPaths`, and registers in load order.
  - `Reload(id)` reopens the module and every loaded module that depends on it.
  - `Load(path)` is a single manifest-less module (`local.module`), which the sandbox uses.
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

## Gameplay (`engine/gameplay`, namespace `interi::gameplay`)

- **`CharacterController`** wraps `JPH::CharacterVirtual`, a capsule whose position is its feet. It walks, jumps, steps up to `stepHeight`, sticks to the floor, and pushes dynamic bodies with at most `maxStrength` N.
  - Update after `PhysicsSync::Step` (sync point).
  - DebrisSmall doesn't collide with characters.
  - `FollowCamera` is the orbit and aim math: yaw 0 looks along -Z.
- **Weapons**: `WeaponDef` JSON (`data/gameplay/weapons.json`), parsed like the destruction materials.
  - `WeaponSystem` is a sync point between `PhysicsSync::Step` and `DestructionWorld::Update`:
    - `Swing`: a sphere cast, an impulse at the contact point, melee damage.
    - `PlaceCharge`: sticks to a destructible's chunk and follows it through splits (`DestructionWorld::ChunkPose`), or to an entity's Transform. `DetonateCharges` sets them off.
    - `FireRocket`: a per-step segment cast; it blasts on a hit or at its range.
  - Blasts queue explosion damage and push dynamic bodies, in body-ID order.
- **Vehicles** (`Vehicle.h`, `VehicleDef.h`, `data/gameplay/vehicles.json`): a Jolt `VehicleConstraint` with a `WheeledVehicleController` and a cylinder-cast tester, four wheels at the chassis box's bottom corners.
  - Vehicle space is Jolt's: +Z forward, +X left. The body is created by the `Vehicle` (not PhysicsSync); its user data is an entity with `VehicleComponent` + Transform (no RigidBodyComponent, or PhysicsSync would add a second body).
  - Like the character: `SetInput` between steps, `Update` after `PhysicsSync::Step`. Teardown is step listener, constraint, body.
  - The torque curve falls to zero at max RPM (a rev limiter; Jolt clamps RPM only after the solve) and the chassis has no linear damping, so top speed is max RPM through gear and differential. Tests pin it analytically.
  - Ramming uses the ordinary contact damage path: damage is linear in the impulse above `impactThreshold`, and the impulse is about mass x approach speed.
  - Hard contacts and blasts (`ApplyRadialDamage`) cost health. At zero it turns into wreck debris: the chassis plus four `ColliderComponent::Cylinder` wheels as Debris entities with `WreckComponent`, moving with the vehicle's point velocities and optionally adopted by the DebrisSystem.
- **Levels** (`Level.h`): JSON static boxes, destructibles and spawn points.
  - `SpawnLevel` reads every asset first (an error creates nothing), then creates everything in file order. Pass an `AssetCache` to reuse assets across reloads.
  - `ApplyInputConfig` binds actions and axes by input name ("Space", "Mouse.Left", "Gamepad.South", "Gamepad.LeftX"), all or nothing.
- **Saves** (`DestructionState.h`) record per instance:
  - bond health;
  - chunks released to the DebrisSystem;
  - falling pieces' poses and velocities, keyed by smallest chunk.
  - The format is IDSS: payload hash, then a versioned Archive.
  - Restore into a freshly loaded level: `DestructionWorld::RestoreDamage` only adds damage, and pieces are rebuilt without break events.
  - Not saved: DebrisSystem entities (debris, rubble) and the stress warm start, so a load continues equivalently, not bit-identically.

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

## Destruction audio (`engine/destruction_audio`, namespace `interi::destruction`)

- **`DestructionAudioConfig`** (`data/audio/destruction_audio.json`) holds impact banks by material pair (`*` wildcard; exact beats one wildcard beats none; ties go to file order) plus creak, rumble and explosion cues. Clip names resolve through a game-provided `ClipLibrary`. Volumes rise with log impulse and log mass.
- **Material identity** is a `StringId`: a destructible chunk's or debris/rubble source's DestructionMaterial name, else an `AudioMaterialComponent` (vehicles, ground), else none (matches only `*`).
- **`ImpactBatcher`** gives one voice per (bank, region cell) per step at the impulse-weighted centroid. Cells are centred on multiples of the size, so a floor at y = 0 sits mid-cell. Results are independent of insertion order.
- **`StressAudio`** computes bond utilization (`BondUtilization` in `StressSolver.h`: load over the health-scaled limit, as DestructionWorld checks), scanned round-robin within a bond budget, and only for converged solves. A region creaks once per crossing, then re-arms after relaxing below `rearmThreshold` and a cooldown.
- **`CollapseAudio`** sums the mass of newly created unanchored pieces per region. A new instance's initial pieces don't count.
- **`DestructionAudio::Update`** runs after `DestructionWorld::Update`. `PlayExplosion` is for blasts (the sandbox feeds `WeaponSystem::Blasts()`).

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

## Physics (`engine/physics`, namespace `interi::physics`)

- **Jolt build.** Jolt comes from the overlay port `cmake/ports/joltphysics`, a copy of vcpkg's port with `CROSS_PLATFORM_DETERMINISTIC=ON` (`-ffp-contract=off`, `/fp:precise`, no `JPH_USE_FMADD`). Simulations are bit-identical on Linux and Windows. `JoltDeterminism.Pile_MatchesCrossPlatformGoldenHash` pins this, and a Jolt version bump changes that constant. Re-sync the overlay when the vcpkg baseline moves.
- **`JoltRuntime`** does Jolt's process-wide setup (default allocator, Factory, types; traces go to the log, Jolt asserts to the Interi handler). Create exactly one for the process lifetime: in `main`, or a gtest Environment (`tests/physics/PhysicsTestEnvironment.cpp`). It is not ref-counted. Jolt keeps its own allocator, because its free hooks don't pass a size.
- **`JoltMath.h`** converts math types by plain copy. Both libraries use column vectors, translation in column 3 and (x,y,z,w) quaternions. Jolt is single precision, so `RVec3` is `Vec3`.
- **Layers** (`PhysicsLayers.h`): Static, Structure, Debris, DebrisSmall, Character, Vehicle, Projectile, all from one `ShouldCollide` table.
  - DebrisSmall collides with neither itself, characters nor projectiles.
  - Broadphase trees: Static+Structure, Debris+DebrisSmall, and Moving.
  - Jolt's three table filters are built from that one table, so they can't disagree.
- **`PhysicsWorld`** owns the `JPH::PhysicsSystem` and steps it through `JoltJobSystem`. It requires a live `JoltRuntime`.
  - `Contacts()` returns the last step's new contacts, merged from per-thread buffers and fully sorted, so they are identical at any thread count.
  - `ContactEvent::impulse` is an estimate (effective mass × approach speed × (1+e)), because Jolt's callbacks run before the solver. `PhysicsWorldDesc::minContactImpulse` filters out weak contacts.
- **ECS sync.** `RigidBodyComponent` + `ColliderComponent` + `interi::Transform` give an entity a body, and `PhysicsSync` maintains it.
  - Every `PhysicsSync` call is a sync point, not a Scheduler system. Bodies are created and removed serially in query order, which keeps body IDs deterministic.
  - Only `PullTransforms` is parallel, over chunks. It writes only chunks that contain an active body, or a body that fell asleep in the last step (`PhysicsBodyComponent::wasActive`).
  - Kinematic bodies get `MoveKinematic` every step.
  - A body's user data is its entity (`PhysicsWorld::EntityOf`, `EntityFromBodyUserData`).
- **Queries** (`Queries.h`): `Raycast`, `SphereCast` and `OverlapSphere`, filtered by a `LayerMask`. They lock bodies, so they may run on any thread, but not during `Step`.
- **Determinism tests** hash `PhysicsSystem::SaveState` (`tests/physics/PhysicsTestUtil.h`) and compare it across 1/8/32 threads (stress label).

## Destruction (`engine/destruction`, `engine/destruction_cook`, `tools/fracture_cooker`, namespace `interi::destruction`)

- **Module split.**
  - `interi::destruction` is runtime-safe and depends only on core, jobs and nlohmann-json (private). It holds materials, mesh cutting, Voronoi, the hierarchy, bonds, hulls and the `.idest` format. M7 builds Jolt shapes from `AssetChunk::hulls`, and M13 reuses the cutter.
  - `interi::destruction_cook` is offline only. It holds glTF import (cgltf, implemented in `CgltfImplementation.cpp`), the sidecar config, meshoptimizer and the incremental `Cook`.
  - `fracture_cooker` is a thin CLI over `Cook`.
- **Materials** (`DestructionMaterial`): strengths are in Pa, so a bond's limits are area times the weaker material's strengths, in N.
  - `ParseMaterials` reads `{"materials": [...]}`. A missing field takes its default and adds a warning, and invalid values are errors.
  - The presets are in `data/destruction/materials.json`.
- **`FractureMesh`** has welded positions and per-corner normals and UVs. Each triangle carries a destruction `material`, a render `surfaceMaterial` and an `interior` flag (set on cut faces).
  - `CutMesh` keeps the part behind a plane. Each crossing edge gets exactly one new vertex, keyed by its (min, max) indices and interpolated low to high in double, so results are watertight by construction. Never weld cut output with a tolerance.
  - Caps are ear clipped with hole bridging. On-plane faces are kept if they face along the normal.
- **Voronoi.**
  - `GenerateSeeds` uses only integer randomness (`core/Random.h`): no `<random>` distributions and no `sin`/`log`.
  - `VoronoiFracture` computes one cell per job: the source is cut by the bisectors that clip the seed's convex region. Cells are merged in seed order, so output is identical at any thread count.
  - Disconnected cells split into pieces.
  - A cell below `minPieceVolume` drops its seed and its neighbours are recomputed, so the volume is absorbed exactly. Small fragments of larger cells are discarded and counted in `discardedVolume`.
- **Hierarchy.** Chunks are breadth-first: chunk 0 is the whole object and children are contiguous. Each chunk's seeds use `HashCombine(level seed, chunk index)`.
- **Bonds.**
  - Bonds connect leaf chunks only, across objects. `FindContacts` hashes triangles into a grid whose cells are about the mean triangle size, then measures the exact 2D overlap of opposite-facing coplanar triangles.
  - Anchors are leaves that touch the `groundHeight` plane or an anchor mesh (config `"anchor": true` objects).
- **Hulls.** `BuildCollisionHulls` cuts concave pieces through the centroid until each hull is within `maxConcavity` of its piece's volume.
- **`.idest`.**
  - Layout: magic `IDST`, then version, cook key, the XXH3 of the payload, and the payload, which is its own archive `IDSP`.
  - A flipped byte reads as `Corrupt`.
  - Bump `kDestructibleVersion` when the format changes, and `kCookerVersion` when cooking output changes for the same inputs.
- **Cooking.**
  - `Cook` reads `<input stem>.fracture.json` next to the glTF. Its format is documented in `CookConfig.h`, and `data/destruction/samples/building.*` is an example.
  - Settings are applied in order: defaults, then the entry for the object's glTF material, then the node entry.
  - The cook key hashes the glTF and its buffers, the config, the materials and both versions. A cook is skipped when the existing output is a valid asset (payload hash checked) with the same key.
  - `FractureCooker_SampleBuilding_Cooks` (label `integration`) cooks the sample.

## Runtime destruction (`engine/destruction` runtime parts, `engine/destruction_runtime`)

- **Pure algorithms** live in `interi::destruction` (no Jolt, no ECS); `interi::destruction_runtime` puts them on
  Jolt bodies and ECS entities.
- **`StressSolver`** is a beam lattice: one 6-DOF node per chunk, one elastic beam per bond (axial EA/L, shear GA/L,
  bending EI/L, torsion GJ/L through the face centroid; one Young's modulus, only ratios matter). K u = f (gravity +
  external forces) is solved with 6x6 block-Jacobi PCG in double, matrix-free, warm-started, only over nodes connected
  to an anchor (K is singular otherwise).
  - Sign convention: `BondForces::axial` > 0 is tension (B pulled away from A along the A->B normal).
  - `ToBondLoad` adds 6M/sqrt(A) of bending to both tension and compression. Overloads are checked against the
    limits scaled by health fraction, and only once a solve has converged.
  - Grains are fixed and reductions ordered, so results are bit-identical at any worker count. Keep it that way.
  - Warm start saves little on local changes (restarted CG); its value is zero-cost steady state and continuing a
    budgeted solve. `SolveWithBudget` spends bond-iterations in importance order.
- **Damage is energy (J).** Bond health = weaker material's fractureEnergy x area. `RadialDamage` falls off
  inverse-square outside `innerRadius`; contact impulses above `impactThreshold` become impact damage.
- **`LabelComponents`** is a lock-free union-find that links the larger root under the smaller, so labels (component
  minimum) are scheduling-independent.
- **`DestructionWorld`**: call `Update` after `PhysicsSync::Step`. Chunks connected to an anchor form one static
  compound piece; each other component is a dynamic piece (entity with Transform, RigidBodyComponent,
  ColliderComponent, DestructibleComponent). Every hull shape's Jolt user data is its chunk (`ChunkAt`).
  - Pieces are recreated (destroy + create) when their chunk set changes, in island order (smallest chunk), so body
    creation stays deterministic. New pieces inherit the parent's point velocity and angular velocity, plus
    `separationSpeed`; explosions and melee push pieces within their radius. `LastStats().injectedEnergy` counts the
    kinetic energy added.
  - Contacts between pieces of one instance are ignored for `spawnGraceSteps` (faces that just broke apart touch).
  - `maxBreakRounds` limits how far a cascade goes per frame; `stressWorkBudget` (bond-iterations) caps solve time.
- **Tests**: `tests/destruction_runtime/RuntimeTestUtil.h` builds grid assets of unit cubes; scenario tests
  (`interi_destruction_runtime_scenario_tests`, label `integration`) and a collapse golden hash
  (`CollapseDeterminism.MatchesCrossPlatformGoldenHash`, stress) pin behaviour. Any change to destruction output or a
  Jolt bump changes the golden hash.
- **Baselines**: `benchmarks/baselines/` (see its README); the nightly CI `benchmarks` job compares report-only via
  `tools/bench_compare/compare_benchmarks.py`.

## Debris (`destruction/Debris*.h`, `destruction_runtime/DebrisSystem.h`)

- **Pure parts** (`interi::destruction`):
  - `DebrisPool` holds fixed-capacity slots (a user value and a priority each). Its storage is allocated once, so acquire, evict and release never allocate.
  - When the pool is full, the lowest priority is evicted, and ties go to the lowest slot index. A newcomer that ranks lower than every live slot is rejected.
  - `Debris.h` holds `DebrisPolicy` and the decision functions: priority, fade opacity, `ChooseDebrisAction`, particle integration, sphere-vs-planes culling and `ClusterByGrid`.
- **`DebrisSystem`** owns debris entities: Transform, ColliderComponent, RigidBodyComponent and DebrisComponent.
  - Hard caps (`maxBodies`, `maxParticles`) evict the lowest priority, which is size up, age down, distance down.
  - A small, far piece becomes a particle: its RigidBodyComponent is removed, so PhysicsSync drops the body, and it falls ballistically until `particleLifetime` runs out.
  - Bodies up to `fadeMaxVolume` fade out and are then removed.
  - Sleeping bodies that share a grid cell merge into one static `RubbleComponent` compound once there are `rubbleMinPieces` of them.
  - `Update` runs after `DestructionWorld::Update`. Per-debris work runs in parallel over chunks; structural changes are applied serially in query order. `DebrisDeterminism` compares 1/4/32 workers.
- **Handoff is opt-in**: `DestructionWorld::SetDebrisSystem`. Pieces up to `smallPieceVolume` are adopted, and they leave the instance at that moment:
  - their chunks are excluded from islands and their internal bonds count as broken;
  - they get no DestructibleComponent, so they take no contact damage but can still deal it;
  - `Despawn` leaves them alive.
  Without a debris system the M7 behaviour and golden hash are unchanged.
- **Not in M8:** Jolt bodies are not recycled (see the churn benchmark). Debris handed over by `DestructionWorld` carries a `DebrisSourceComponent` (asset and leaf chunks), and rubble piles keep their members' chunks and poses in a `RubbleSourceComponent`, for rendering (M9).

## Destruction rendering (`engine/destruction_render`, namespace `interi::destruction`)

- **`ChunkRenderSet`** (in `interi::destruction`, pure) covers a set of leaves that move together with the highest chunks whose leaves all belong to it. Every cooked chunk, inner ones too, has a render mesh, so an intact structure draws its root mesh and only regions that broke apart fall back to smaller chunks.
- **`Submesh::interior`** (asset format v2) marks fracture faces: their `surfaceMaterial` is the DestructionMaterial's `interiorMaterialId`, a separate number space from glTF surface materials.
- **`ChunkMeshCache`** uploads every chunk of an asset into the `MeshPool` once (every hierarchy level, so size the pool for it). **`ChunkGatherer`** turns pieces (anchored ones as occluders), debris (with opacity, skipped when not visible) and rubble into `RenderObject`s, one per chunk submesh, with `ChunkMaterials` (surface and interior tables).
- **Interior materials** (`InteriorMaterials.h`): tileable procedural albedos (concrete aggregate, rusted steel, wood, glass, sheet metal) by `interiorMaterialId`, generated with integer arithmetic only (a test pins the concrete hash), mapped triplanar.
- **Effects:** `DestructionWorld::BreakEvents()` lists the bonds broken in the last Update (recording them does not change the simulation). `BreakEffects` spawns per-material particles from them (dust and chips, sparks, splinters, shards) with seeded integer randomness. `GatherCrackDecals` puts a crack decal on every damaged bond whose chunks still move together, with `CrackOverlayIntensity` (none until 10% of the bond's health is lost, full at 90%).
- **Tests and goldens:** `interi_destruction_render_gpu_tests` (integration) and `interi_destruction_render_golden_tests` (`tests/destruction_render/golden`) run on lavapipe with Jolt. The fractured-wall and decal goldens depend on the simulation, so a Jolt bump or a destruction change can move them. An executable linking both `physics_testing` and `vulkan_testing` gets one merged TSan suppression list.

## Editor (`engine/editor`, `engine/editor_ui`, `tools/editor`, namespace `interi::editor`)

- **Module split.** `interi::editor` is the editor without UI (tested headless): reflection, the level document,
  undo, camera and gizmo math, picking, fracture preview, play sessions and replays. `interi::editor_ui` is the Dear
  ImGui layer (imgui 1.92 docking, no SDL/Vulkan bindings: input comes from `InputEvent`s, drawing goes through the
  RHI). `tools/editor` builds `level_editor` (the `interi_editor` target name is the core library).
- **Reflection** is `interi::values`' (`editor/Reflection.h` aliases it: `FieldType` = `values::ValueType`,
  `FieldValue` = `values::Value`). `RegisterEngineReflection` covers Transform, the document components and
  VehicleComponent / WreckComponent. Field widgets for every value type are `ValueWidget` (`editor_ui/ValueWidgets.h`).
- **Document.** `EditorScene` holds the level JSON as ECS entities: `EditorIdComponent` (stable id across
  delete/undo, not reflected), Transform, and one kind component (`StaticBoxComponent`,
  `DestructiblePlacementComponent`, `SpawnPointComponent`; a spawn point's yaw is its rotation about +Y).
  `Load(LevelDef)` / `ToLevel()`; `gameplay::WriteLevel` writes what `ParseLevel` reads. Snapshots and `Restore` cover
  reflected components only.
- **Undo.** `UndoStack` executes `ICommand`s; commands with the same non-zero merge key merge until `Seal()` (the UI
  seals on widget release), a capacity drops the oldest, `MarkClean`/`IsDirty` track the save point. Edits go through
  `EditField` / `EditTransform` / `EditorDocument::Add` / `DeleteSelection`, and project data edits through
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
  stress lines, brush strokes) are draw-list lines projected with the same `OrbitCamera`. The `MeshPool` only
  appends, so when it fills the editor waits for the GPU, `Clear`s it and the `ChunkMeshCache`, and re-adds assets
  as they are drawn (call `EditorRenderer::AddAsset` every frame for drawn assets).
- **Fracture authoring.** `FracturePreview` cooks each opened glTF in memory with `destruction::CookScene` (what
  `Cook` writes) and recooks only entries whose settings key (hash of `WriteCookConfig`) changed; the app recooks
  when no widget is active. Painted seed density is `SeedPattern::Painted` (uniform base + strokes, one random
  stream per stroke); material volumes assign chunks by centroid after the hierarchy is built. Both live in the
  `.fracture.json` sidecar, so `fracture_cooker` reproduces the editor's result.
- **Play and replays.** `PlaySession` spawns `ToLevel()` into fresh physics/ECS/destruction worlds (needs a
  `JoltRuntime`) and steps at 60 Hz; `Detonate` queues an explosion for the next step and records it. `Replay` files
  (IRPL: payload hash, versioned archive) hold the level JSON, the step-indexed explosions, the step count and the
  final `StateHash` (Jolt SaveState + bond health). `RunReplay` reproduces it from a fresh spawn; tests compare 1/4/32
  workers (`interi_editor_stress_tests`).
- **Smoke run:** `level_editor --video-driver offscreen --frames 120 --demo 1 --screenshot out.ppm` (plays the level,
  detonates, saves and verifies a replay, writes the last frame). `--fracture destruction/samples/building.gltf`
  opens a source in the Fracture panel. The dock layout is saved in the temp directory
  (`interi_editor_layout_v3.ini`; bump the version when windows are added).

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
