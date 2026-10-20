# Game-Platforms — Goal State

## Vision
One Interi codebase builds and runs on Linux and Windows desktop today, and it builds as a static, console-shaped configuration that the private PS5 and Xbox repos extend by adding files, not patching shared ones. Gameplay modules hot-reload as shared libraries during development and link statically into shipped or console executables. The shader pipeline emits a bytecode per backend from the same HLSL. Saves go through an atomic, quota-aware storage interface. Lifecycle, user, input-pairing and platform-services events reach games through small engine interfaces. A shipping preset, a player executable and per-store packaging are produced and tested in CI.

## Acceptance criteria
- [ ] AC-1 A CI job configures with `-DINTERI_BUILD_SHARED=OFF`, links the starter module statically into a player or test executable, and the starter scenario (collect, win, save, load) passes. `ModuleHost` loads statically linked modules without `DynamicLibrary` or a shadow copy. (resolves PLAT-001)
- [ ] AC-2 Every shader target in `interi_add_shaders` builds for at least `spirv` and `dxil` in CI. No engine `.hlsl`/`.hlsli` file uses a raw `[[vk::` attribute outside the backend-macro header. `render` and `editor_ui` choose bytecode through the device's shader format, never a SPIR-V-only symbol. (resolves PLAT-002)
- [ ] AC-3 `NativeFileSystem::WriteFile` writes through a temp file plus flush plus atomic replace. A fault-injection test that aborts mid-write leaves the previous save slot readable. `SaveSlots` keeps a `.bak` and loads it when the primary fails its hash. Saves go through an `ISaveStorage` with a size quota and asynchronous commit. (resolves PLAT-003)
- [ ] AC-4 A Windows CI test loads a module from a directory whose name contains non-ASCII characters (shadow and search paths). `DynamicLibrary::Open` accepts `std::filesystem::path`. No `path.string()` result is passed to a UTF-8 API in `engine/`. (resolves PLAT-004)
- [ ] AC-5 `InputEvent` includes focus, minimise, suspend/resume, display-scale and locale events, translated from SDL3 with unit tests. Focus loss clears held input. `GameSession` pauses on suspend. Gamepad connections carry a user id. A `PlatformServices` header (users, achievements, presence, cloud saves, network status) exists with a null implementation, offered to modules as a host service. (resolves PLAT-005)
- [ ] AC-6 `IWindow` supports windowed, borderless and exclusive modes, display selection and `DisplayScale()`. Mouse events are in pixels, as is `Size()`, verified by a unit test with pixel density 2.0. (resolves PLAT-006)
- [ ] AC-7 `linux-clang-shipping` and `windows-msvc-shipping` presets define `INTERI_SHIPPING` (propagated through `interi::build_settings`) and build and pass `-L unit` in CI. (resolves PLAT-007)
- [ ] AC-8 An installed player executable (from `cmake --install` into a clean prefix) runs the starter project from another working directory, writes saves under the per-user data directory, and the editor finds its data relative to its executable. Windows `.rc` and manifest, Linux `.desktop`/AppStream, and at least one store or container package recipe (Steam Runtime depot or Flatpak) are tracked. (resolves PLAT-008)
- [ ] AC-9 CI has a Steam Runtime (or older-glibc) Linux build whose binaries run on a stock Ubuntu LTS image, Windows GPU and golden tests running on a software Vulkan driver, and jobs for `INTERI_BUILD_SHARED=OFF` and `INTERI_SIMD=OFF`. (resolves PLAT-009, PLAT-001)
- [ ] AC-10 `NormalizeVirtualPath` rejects Windows reserved names and trailing dots or spaces on every platform. Development builds report case-only mismatches, and a content lint flags them. (resolves PLAT-010)
- [ ] AC-11 Every shipped executable checks AVX2/FMA/BMI2 support before running engine code and shows a readable message on unsupported CPUs. (resolves PLAT-011)
- [ ] AC-12 `interi_jobs` and `interi_core` cross-compile for `aarch64` in a CI job, and `JobSystem.cpp` contains no raw x86 intrinsic. (resolves PLAT-012)
- [ ] AC-13 Platform and compiler detection lives in one `Platform.h` with an `#error` for unknown targets. A CI literal check finds no `_WIN32`, `windows.h`, `dlfcn.h` or `pthread.h` outside `engine/core/include/interi/core/Platform.h` and the platform-layer source folders. (resolves PLAT-013)
- [ ] AC-14 Job workers are named. Default worker count and pinning honour the process affinity mask. `JobSystemDesc` accepts an explicit core list. The jobs tests pass under `taskset -c 0,1` with pinning on. (resolves PLAT-014)
- [ ] AC-15 The audio engine recovers from a device stop or reroute notification without a restart (test with the null backend). (resolves PLAT-015)
- [ ] AC-16 SDL3 on Linux is built with IBus (after license review), or the missing IME is documented as a known limitation in README. (resolves PLAT-016)
- [ ] AC-17 Windows executables embed a manifest (per-monitor-v2 DPI, UTF-8 code page, long paths). A deliberate crash in a test executable produces a minidump on Windows and a logged backtrace on Linux. (resolves PLAT-017)
- [ ] AC-18 The asset cook key includes a cook target, and products are written per target, so two targets' cooks coexist. (resolves PLAT-018)
- [ ] AC-19 No editor or game state is written to `temp_directory_path()` except shadow copies and true scratch data. Layout and preferences live in the per-user config folder. (resolves PLAT-019)
- [ ] AC-20 `tools/platform_audit` is either removed or replaced by the AC-13 CI check. The layering in PLAT-021 (no SDL in `platform`, no Vulkan in `rhi`/`render`, all allocation through `IAllocator`, no NDA material) still holds. (resolves PLAT-020, PLAT-021)

## Guardrails to keep it that way
- CI literal check (a script like `tools/license_check/check_spdx_headers.py`): OS headers and `_WIN32`/`__linux__` appear only in `Platform.h` and the platform-layer source folders, and `[[vk::` appears only in the shader backend-macro header.
- CI jobs: static engine, scalar SIMD, shipping presets, Steam Runtime Linux, Windows software-Vulkan goldens, and an aarch64 cross-compile of core and jobs. All are required checks on `main`.
- A fault-injection save test and a non-ASCII-path Windows module-load test kept in the `unit` label.
- Review checklist item for new engine code: does it write to disk outside `ISaveStorage` or user-data paths, does it call an OS API outside `engine/platform`, and does it add an event the console backends must map?
- `CMakePresets.json` review: every new preset has both a Linux and a Windows twin, or a stated reason why not.

## Out of scope
- Implementing the PS5 and Xbox backends, their SDK integrations, certification-specific requirement IDs, or any NDA API. Those stay in the private repos. This discipline only requires the public engine to be structurally ready for them.
- macOS, iOS, Android and Switch-class ARM targets as supported platforms (only the cheap ARM64 compile hygiene in AC-12 is required).
- Choosing a specific storefront SDK (Steamworks, EOS, GDK). Only the services interface and a null implementation are required.
- Performance tuning per platform (covered by the performance discipline).
