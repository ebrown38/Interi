# Game-Platforms — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. PLAT-003 and PLAT-004: real user data loss and real Windows failures today, each with a small, local fix.
2. PLAT-001, then PLAT-009 (static CI job): decide the console and shipping code-loading model before more gameplay moves into modules, and keep it green in CI.
3. PLAT-002: design the per-backend shader target before more HLSL accumulates `vk::` constructs. The private console repos depend on it.
4. PLAT-005 and PLAT-006: lifecycle, services and window abstractions that console backends and a PC store build both need.
5. PLAT-007, PLAT-008 and PLAT-011: the shipping configuration, a player executable and packaging.
6. PLAT-010, PLAT-018 and PLAT-013: content and platform-define hygiene that gets more expensive as content grows.
7. Low items (PLAT-012, PLAT-014 to PLAT-017, PLAT-019) as polish.

## PLAT-001 — Gameplay code runs only as shadow-copied, runtime-loaded shared libraries
- **Severity / Effort:** High / XL
- **Depends on:** None (PLAT-009 adds the CI guard)
- **Steps:**
  1. Split "how a module's code gets into the process" from "how the host registers it". Introduce a module-provider interface in `engine/hotreload`: `DynamicModuleProvider` (today's shadow-copy and `DynamicLibrary`) and `StaticModuleProvider` (a table of `{id, const ModuleApi* (*)()}` linked into the executable).
  2. Add a static registration macro next to `INTERI_GAMEPLAY_MODULE` (`ModuleApi.h:200-215`). In static builds it emits a uniquely named entry function plus a registration object, or a generated table in a CMake-produced `.cpp`, so the linker keeps it.
  3. In `ModuleHost::LoadProject`, resolve each manifest's `library` against the static table first. When the engine is static, return `Supported()==true` for statically linked modules and refuse only dynamic ones and `Reload`.
  4. Add an `interi_link_gameplay_modules(target MODULES ...)` CMake helper (`cmake/InteriGameplayModule.cmake`) that builds module sources as static or object libraries into the player executable and generates the table.
  5. In shipping (non-editor) builds, skip the shadow copy and load from the install directory, or link statically. Make `shadowDirectory` per-user and non-shared (see the Security cross-note).
- **Illustrative change (not applied):**
  ```cpp
  // ModuleApi.h (static build)
  #define INTERI_GAMEPLAY_MODULE_STATIC(id, registerFunction) \
      static ::interi::hotreload::StaticModuleRegistration s_reg_##registerFunction{id, &Api_##registerFunction};
  // ModuleHost.cpp
  if (const ModuleApi* api = StaticModules().Find(manifest.library)) { /* register without DynamicLibrary */ }
  ```
- **Verification:** A new CI job configures `-DINTERI_BUILD_SHARED=OFF`, links the starter module statically into a test player, and runs the `interi_starter_sample_tests` scenario (collect coins, win, save, load) green. `ModuleHost::Supported()` tests are updated.
- **Risks / trade-offs:** Hot reload stays a development-only feature. Two code paths need parity tests. The toolchain fingerprint and the ABI check are moot for static modules but must not be bypassed for dynamic ones.

## PLAT-002 — The shader toolchain and the renderer are locked to SPIR-V with Vulkan-specific HLSL
- **Severity / Effort:** High / XL
- **Depends on:** None
- **Steps:**
  1. Extend `interi_add_shaders` (`cmake/InteriShaders.cmake`) with a `TARGETS` list (`spirv` today, `dxil` and private console targets later). Each target gets its own DXC flags, and the output is a per-target embedded blob.
  2. Generate a `ShaderBlob { std::span<const std::byte> spirv, dxil, ... }` per shader, or a lookup by `IDevice::ShaderFormat()`. Change the `render` and `editor_ui` pipeline-creation sites to pick `blob.For(device.ShaderFormat())`, never a raw SPIR-V symbol.
  3. Move Vulkan attributes behind macros in `engine/shaders/include/interi/Bindless.hlsli`, `ForwardPush.hlsli`, `ParticlePush.hlsli` and the vertex shaders (`INTERI_BINDING(slot)`, `INTERI_PUSH_CONSTANTS`, `INTERI_DRAW_ID`). The macros expand to `[[vk::...]]` under `__spirv__` and to register spaces, root constants or `SV_StartInstanceLocation` (SM 6.8) / indirect-signature root constants for DXIL.
  4. Replace aliased bindings (two resource types on one binding) with SM 6.6 `ResourceDescriptorHeap[]` / `SamplerDescriptorHeap[]` indexing under DXIL, or with separate unbounded arrays per type.
  5. Document the draw-ID contract in `IDevice` (the object index comes from a backend-provided draw ID) so the D3D12 and console backends know what to supply.
- **Illustrative change (not applied):**
  ```hlsl
  #if defined(__spirv__)
  #define INTERI_DRAW_ID(name) [[vk::builtin("BaseInstance")]] uint name : BASE_INSTANCE
  #else
  #define INTERI_DRAW_ID(name) uint name : SV_StartInstanceLocation
  #endif
  ```
- **Verification:** CI compiles every shader for both `spirv` and `dxil` (DXC from vcpkg can already emit DXIL). A unit test asserts that every embedded shader has a blob for each enabled target. The Vulkan goldens stay bit-identical.
- **Risks / trade-offs:** SM 6.8 for start-instance limits D3D12 hardware. An indirect-command root constant avoids that but needs a command-signature design. Keep the Vulkan path identical to protect the goldens.

## PLAT-003 — Saves are written in place with truncate-then-write
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Implement `NativeFileSystem::WriteFile` atomically. Write to `<file>.tmp` in the same directory, flush (`fsync` / `FlushFileBuffers`), then replace (`std::filesystem::rename` on POSIX, `ReplaceFileW` or `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows). On POSIX, also `fsync` the directory.
  2. In `SaveSlots::Write`, keep the previous slot as `slotN.bak` (rotate before the replace). Make `Read`/`List` fall back to `.bak` when the primary fails the payload hash.
  3. Add an `ISaveStorage` interface (begin, write, commit, with a size quota and async completion) under `engine/platform`. Implement it over `IFileSystem` for PC, so console backends can map it to the platform save container. Move `SaveSlots` onto it and do the write off the game thread (a Low-priority job, polled).
  4. Use the same atomic write for `SavePreferences` (`EditorServices.cpp:109`).
- **Illustrative change (not applied):**
  ```cpp
  const auto temp = resolved; temp += ".tmp";
  { std::ofstream s(temp, std::ios::binary | std::ios::trunc); s.write(...); s.flush(); /* + platform fsync */ }
  std::filesystem::rename(temp, resolved, error); // ReplaceFileW on Windows
  ```
- **Verification:** A unit test uses a fault-injecting `IFileSystem` that fails after N bytes, and the previous slot still reads. A test with a corrupted primary loads from `.bak`. `GameSessionTests` stay green at 0/1/4/16 workers.
- **Risks / trade-offs:** Rename semantics differ on network shares. `.bak` doubles disk use per slot. Async saves need UI for "saving..." and must block quitting until committed.

## PLAT-004 — Windows: non-ASCII paths break module loading
- **Severity / Effort:** High / S
- **Depends on:** None
- **Steps:**
  1. Replace `shadow.string()` / `library.string()` in `ModuleHost.cpp` (and any `path.string()` fed to UTF-8 APIs or `std::format`) with a helper `ToUtf8(const std::filesystem::path&)` that uses `u8string()`. Better, give `DynamicLibrary::Open` an overload taking `std::filesystem::path` that calls `LoadLibraryW(path.c_str())` directly.
  2. Convert `level_editor`'s arguments via `wmain` / `CommandLineToArgvW`, or embed an application manifest with `<activeCodePage>UTF-8</activeCodePage>` (see PLAT-017) so narrow APIs are UTF-8.
  3. Audit `.string()` on paths in `engine/` with a literal search and convert the ones that reach logs, JSON or OS calls.
- **Illustrative change (not applied):**
  ```cpp
  static Result<DynamicLibrary> Open(const std::filesystem::path& path); // Windows: LoadLibraryW(path.c_str())
  ```
- **Verification:** A Windows CI test creates a temp directory named with non-ASCII characters (e.g. `"modül_测试"`), sets `ModuleHostDesc::shadowDirectory` and `librarySearchPaths` to it, and loads the test module.
- **Risks / trade-offs:** The UTF-8 manifest only applies on Windows 10 1903 and later. The `path` overload is the robust fix.

## PLAT-005 — No application lifecycle, user or platform-services abstraction
- **Severity / Effort:** Medium / L
- **Depends on:** PLAT-006 (shares the window event work)
- **Steps:**
  1. Add `WindowFocusEvent{bool focused}`, `WindowMinimizedEvent`, `AppLifecycleEvent{Suspending, Resumed, LowMemory}`, `DisplayScaleChangedEvent` and `LocaleChangedEvent` to `InputEvent`. Translate the corresponding SDL3 events in `TranslateSdlEvent`. On focus loss, clear held keys and buttons in `InputMapper`.
  2. Add a `UserId` and a `GamepadConnectionEvent::user` (0 = unassigned) so backends can pair controllers to users. Keep a mapping API in `InputMapper` or a `PlayerInputs` helper.
  3. Define `engine/platform/include/interi/platform/PlatformServices.h` with small interfaces: `IUserService` (sign-in/out, primary user), `IAchievements` (unlock/progress), `IPresence`, `ICloudSaves` (or fold into `ISaveStorage` from PLAT-003), and `INetworkStatus`. Provide a `NullPlatformServices` for PC and tests, and offer it to modules as a host service (`registrar.Service`).
  4. Have `GameSession` pause stepping on `Suspending` and flush pending saves.
- **Verification:** Unit tests feed fake SDL events through `TranslateSdlEvent` for every new event. A `GameSession` test raises Suspending and checks that the step count doesn't advance and the save commits. `FakeEventSource` supports the new events.
- **Risks / trade-offs:** Keep the interfaces minimal and public-API-only. Console specifics stay in the private repos.

## PLAT-006 — The window layer has no fullscreen or display control and mixes coordinate spaces
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Extend `WindowDesc` and `IWindow` with `WindowMode {Windowed, Borderless, Exclusive}`, `display` index, `SetMode`, `DisplayScale()` and `IsMinimized()`. Implement them with `SDL_SetWindowFullscreen`, `SDL_SetWindowFullscreenMode` and `SDL_GetWindowDisplayScale`.
  2. Convert mouse coordinates to pixels in `TranslateSdlEvent` by multiplying by `SDL_GetWindowPixelDensity(window)` (look up the window from `event.motion.windowID`), so every engine coordinate is in pixels. Document that in `InputEvents.h`.
  3. Have the editor scale ImGui fonts and style by `DisplayScale()`.
- **Verification:** A unit test checks the translated coordinates with a fake density of 2.0. A manual check on a 150% Wayland session confirms picking hits the cursor.
- **Risks / trade-offs:** Exclusive fullscreen on Wayland is emulated. Test on Steam Deck game mode (gamescope) separately.

## PLAT-007 — No shipping build configuration
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add `option(INTERI_SHIPPING "Final release build" OFF)` in the root `CMakeLists.txt`. When it's on, add `INTERI_SHIPPING` to `INTERI_ABI_DEFINITIONS` (`cmake/InteriArch.cmake` or `InteriBuildSettings.cmake`) so installed consumers inherit it.
  2. Add `linux-clang-shipping` and `windows-msvc-shipping` presets (`CMAKE_BUILD_TYPE=Release`, `INTERI_SHIPPING=ON`, LTO optional, tests that need asserts skip).
  3. Add both to CI (build plus `-L unit`).
- **Verification:** `cmake --preset windows-msvc-shipping && cmake --build ...` succeeds in CI. `EngineToolchainFingerprint()` contains `shipping`. `ProfilingShippingTests` passes in that preset.
- **Risks / trade-offs:** Tests that rely on asserts must `GTEST_SKIP` (already the documented rule).

## PLAT-008 — No runnable game executable or package; the editor bakes an absolute source path
- **Severity / Effort:** Medium / L
- **Depends on:** PLAT-001, PLAT-007
- **Steps:**
  1. Add a `tools/player` (or `engine/launcher`) executable: SDL window, Vulkan device, `GameSession` with the forward renderer, input config, HUD, and the project folder found relative to the executable.
  2. Add `interi::platform::UserDataPaths()` (saves, config, logs, cache) using `SDL_GetPrefPath` or `%APPDATA%` / `XDG_DATA_HOME` / `XDG_CONFIG_HOME`. Pass it as `GameSessionDesc::saveDirectory` by default.
  3. Make `level_editor` find `data/` relative to its executable (`SDL_GetBasePath()`), with `INTERI_EDITOR_DATA_DIR` only as a development fallback. Install it.
  4. Add packaging: a CPack or `cmake --install` layout for the player plus the project. A Windows `.rc` (icon, version info) and manifest. A Linux `.desktop` and AppStream file, and a Flatpak manifest or Steam Runtime depot script.
- **Verification:** `cmake --install` into a clean prefix, then running the installed player from another directory, plays the starter project to "won" headless (`--video-driver offscreen`), and saves land under the per-user directory.
- **Risks / trade-offs:** Rendering a game outside the editor adds a new integration surface. Start with the starter sample.

## PLAT-009 — The CI platform matrix misses the Linux runtime baseline, Windows GPU coverage and the static engine
- **Severity / Effort:** Medium / M
- **Depends on:** PLAT-001 (for a meaningful static job)
- **Steps:**
  1. Add a Linux release build inside the Steam Runtime "sniper" SDK container (or an older-glibc distribution) with a recent Clang toolchain. Run `-L unit` there, and run the binaries on a stock Ubuntu LTS image.
  2. Install a Vulkan software driver on the Windows runner (Mesa lavapipe for Windows or SwiftShader) and set `INTERI_REQUIRE_GPU` there too, so the goldens run on MSVC builds.
  3. Add a configure-and-build job with `-DINTERI_BUILD_SHARED=OFF` and one with `-DINTERI_SIMD=OFF`.
- **Verification:** The new jobs are required checks on `main`. `objdump -T`/`readelf` on the Linux artefacts shows no `GLIBC_` symbol version newer than the runtime's.
- **Risks / trade-offs:** CI time goes up. Software Vulkan on Windows can differ slightly from lavapipe on Linux, so use the existing tolerance-based goldens.

## PLAT-010 — Virtual paths are case-preserving with no case or reserved-name checks
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Reject Windows reserved names (`CON`, `PRN`, `AUX`, `NUL`, `COM1`-`COM9`, `LPT1`-`LPT9`, with any extension) and components with trailing `.` or space in `NormalizeVirtualPath`, on every platform.
  2. Add a case check in development builds: on a miss, or on case-insensitive hosts, compare the requested path against the directory listing and log or return an error when only the case differs. Alternatively, enforce lowercase content paths in the asset database and the editor.
  3. Add a content lint (`interi_module_check --lint-paths`, or the asset `Scan`) that flags references whose case doesn't match the file on disk.
- **Verification:** Unit tests for reserved names and a case-mismatch test on `NativeFileSystem` (run on Windows CI too).
- **Risks / trade-offs:** Checking case on every read costs directory listings, so keep it a development-only check.

## PLAT-011 — The x86-64-v3 baseline has no runtime CPU check
- **Severity / Effort:** Medium / S
- **Depends on:** PLAT-008 (the player is where the check runs first)
- **Steps:**
  1. In each executable's `main`, before any engine static constructor runs AVX2 code, check `__builtin_cpu_supports("avx2") && ("fma") && ("bmi2")` (Clang) or `__cpuid`/`IsProcessorFeaturePresent` (MSVC). Compile that translation unit without `-march`.
  2. On failure, show `SDL_ShowSimpleMessageBox` or print to stderr: "This game requires a CPU with AVX2 (2013 or newer)". Exit cleanly.
  3. Optionally ship an `INTERI_SIMD=OFF` build for a fallback launcher.
- **Verification:** A unit test for the CPU-check function. A manual run under `qemu-x86_64 -cpu Nehalem` or the Intel SDE prints the message instead of SIGILL.
- **Risks / trade-offs:** Static initialisers in engine DLLs could still run AVX2 code before `main`. Keep the check in a separately compiled launcher stub, or make sure no global constructor uses SIMD.

## PLAT-012 — x86 intrinsics are outside the SIMD gate in the job system
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Replace the unconditional `<immintrin.h>` / `_mm_pause()` in `JobSystem.cpp` with a `CpuRelax()` in `engine/core` that uses `_mm_pause` on x86, `__yield()` / `asm volatile("yield")` on ARM64, and `std::this_thread::yield()` otherwise.
  2. Make `kCacheLineSize` per-architecture (128 on Apple ARM64, 64 elsewhere), with a static_assert test.
- **Verification:** Cross-compile `interi_jobs` with `--target=aarch64-linux-gnu` in a CI job.
- **Risks / trade-offs:** None significant.

## PLAT-013 — Platform conditionals fall through to POSIX silently
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Add `engine/core/include/interi/core/Platform.h` that defines `INTERI_PLATFORM_WINDOWS`, `INTERI_PLATFORM_LINUX` and a private hook `INTERI_PLATFORM_EXTERNAL` (set by private console toolchains), with an `#error "unsupported platform"` otherwise. Do the same for `INTERI_COMPILER_*`.
  2. Rewrite the 14 files' `#if defined(_WIN32) / #else` to test these macros explicitly with an `#error` final branch, or move each platform's implementation into `src/<platform>/` files selected by CMake, so private backends add files instead of patching shared ones.
- **Verification:** Configuring with a fake platform macro fails at compile time with the `#error`. A literal-search CI check (see Goal guardrails) forbids raw `_WIN32` outside `Platform.h` and `src/win32/`.
- **Risks / trade-offs:** Churn across about 14 files. Do it before the console repos diverge further.

## PLAT-014 — Thread affinity and naming are naive
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Let `JobSystemDesc` take an explicit core list (`std::span<const std::uint32_t> cores`) that console backends fill. On Linux, intersect with `sched_getaffinity`. On Windows, use `SetThreadGroupAffinity` / CPU sets, and don't `VERIFY` on failure; log once instead.
  2. Base `DefaultWorkerCount` on the affinity-mask count (Linux `sched_getaffinity`, Windows `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`).
  3. Name worker threads ("Interi Worker N") with `SetThreadDescription` and `pthread_setname_np`, and name the audio and logger threads too.
- **Verification:** Run the jobs tests under `taskset -c 0,1` in CI with `pinThreads=true`. They pass, with no VERIFY.
- **Risks / trade-offs:** None significant.

## PLAT-015 — The audio device has no device-change or device-lost handling and only stereo output
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Set `config.notificationCallback` and, on `ma_device_notification_type_stopped`/`rerouted`, post a message to the game thread that re-initialises the device (or fall back to the Null backend and retry periodically).
  2. Optionally query the default device's channel count and add a stereo-to-5.1/7.1 upmix in `Mixer`.
- **Verification:** A test with miniaudio's null backend simulating a stop notification reopens the device. A manual unplug test on Windows and Linux.
- **Risks / trade-offs:** Re-init must not free clips on the audio thread (keep the existing ownership rule).

## PLAT-016 — SDL3 on Linux is built without IBus/D-Bus
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `{"name": "ibus", "platform": "linux"}` (which brings `dbus`) to the `sdl3` features in `vcpkg.json`. Add the needed `-devel` packages to `FEDORA_PACKAGES` in `ci.yml`.
  2. Run `ctest -L license` and regenerate `THIRD_PARTY_NOTICES.md` (dbus is AFL-2.1 OR GPL-2.0-or-later, so check the allowlist).
- **Verification:** A CJK IME composes into an editor text field on a Linux desktop. The license tests stay green.
- **Risks / trade-offs:** dbus's dual license needs review against `license_policy.json`. If it isn't acceptable, document the missing IME instead.

## PLAT-017 — No crash reporting and no Windows application manifest
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Add a Windows application manifest to `level_editor` and the player (per-monitor-v2 DPI awareness, `activeCodePage` UTF-8, `longPathAware`, supportedOS).
  2. Add a minimal crash handler in `engine/platform` (`SetUnhandledExceptionFilter` plus `MiniDumpWriteDump` on Windows; `sigaction` with a backtrace to the log on Linux) writing to the per-user logs folder from PLAT-008.
- **Verification:** A test executable that deliberately faults leaves a `.dmp` file (Windows) or a log backtrace (Linux) in a temp directory.
- **Risks / trade-offs:** Keep the handler async-signal-safe on Linux. Make it optional when a store SDK provides its own crash reporting.

## PLAT-018 — Asset cooking has no target-platform dimension
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. Add a `CookTarget` (id string such as `pc`, plus format capabilities) to the `AssetDatabase` options. Hash it into `ComputeKey`, and write outputs under `imported/<target>/...`.
  2. Let texture import settings choose formats per target (BC1/4/5 for `pc`, with room for BC7 and console-specific formats provided by private repos).
- **Verification:** Cooking the same source for two targets produces two product sets with different keys, and re-cooking one doesn't invalidate the other.
- **Risks / trade-offs:** The `imported/` layout changes, so editor and runtime paths need a migration.

## PLAT-019 — Editor state and fallback preferences go to the shared temp directory
- **Severity / Effort:** Low / S
- **Depends on:** PLAT-008 (the user-data path helper)
- **Steps:**
  1. Store the ImGui layout next to `editor.json` in the per-user config folder.
  2. When no config directory is found, fall back to a per-user location (`SDL_GetPrefPath`) instead of `temp_directory_path()`.
- **Verification:** A unit test that `DefaultPreferencesPath` never returns a path under the temp directory when `HOME` is set.
- **Risks / trade-offs:** Users lose their current layout once (the layout file is versioned anyway).

## PLAT-020 — tools/platform_audit holds only untracked bytecode caches
Info. No remediation required. Optionally delete the stale `__pycache__` folders by hand, or turn the idea into the CI guardrail in Goal.md (a literal check that OS headers and `_WIN32` appear only in the platform layer).

## PLAT-021 — Positive observations
Info. No remediation required. Keep the layering intact (see Goal.md guardrails).
