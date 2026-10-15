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
- **`interi_sandbox`** (`game/sandbox`) is the window + input + ECS + physics + rendering demo. Its debris are Jolt bodies that fall onto a static ground and pillars matching the rendered scene. Run `--video-driver offscreen --frames N --auto-spawn 1 --validation 1` for a headless rendering smoke run on lavapipe. `--video-driver dummy` has no Vulkan, so it runs without drawing. CI builds it but doesn't run it.

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
