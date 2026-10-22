# Game-Platforms — Issues

**Project:** Interi (repository root) @ 4631f35
**Analyzed:** 2026-10-19
**Scope:** The first-party platform surface: `engine/platform`, `engine/platform_sdl`, `engine/rhi`, `engine/rhi_vulkan`, `engine/render` (shader build and embedding), `engine/jobs`, `engine/core` (allocator, Config), `engine/hotreload` (module loading), `engine/game` and `engine/runtime` (sessions and saves), `engine/audio`, `engine/asset_import` (cook key), `engine/editor` (preferences), `tools/editor`, `cmake/*.cmake`, `cmake/triplets`, `CMakePresets.json`, `vcpkg.json`, `.github/workflows/ci.yml` and `README.md`. I searched every tracked first-party `.h`/`.cpp` for platform and compiler preprocessor conditionals (29 sites in 14 files). `external/`, `build*/`, `modules/` and `project-analysis/` were skipped.
**Summary:** 21 issues — Critical: 0 · High: 4 · Medium: 7 · Low: 8 · Info: 2

Claimed platforms (README.md:7, CLAUDE.md): Linux (Clang) and Windows (MSVC) on x86-64-v3, with PS5 and Xbox Series backends kept in private repos. Supported in this repo: Linux and Windows desktop on Vulkan 1.3 through SDL3. The findings below are about portability debt that the console backends and a shipped PC game will hit. Nothing claimed is unbuildable.

## Findings

### PLAT-001 — Gameplay code runs only as shadow-copied, runtime-loaded shared libraries; a static engine cannot run any project with modules
- **Severity:** High
- **Confidence:** High
- **Category:** Console readiness / code loading model
- **Location:** `engine/hotreload/src/ModuleHost.cpp:430-436`, `engine/hotreload/src/ModuleHost.cpp:723-726`, `engine/hotreload/src/ModuleHost.cpp:412`, `engine/hotreload/src/ModuleHost.cpp:447-458`, `engine/game/src/ProjectModules.cpp:47-50`, `CMakeLists.txt:24`, `CMakeLists.txt:56-58`
- **Evidence:** `ModuleHost::Supported()` returns `false` unless `INTERI_SHARED_ENGINE` is defined. `LoadProject` and `Load` then fail with `"gameplay modules need the shared engine (INTERI_BUILD_SHARED)"`. Every load copies the library to `temp_directory_path()/interi_modules/<name>.<n>.<ext>` and `dlopen`s / `LoadLibraryW`s that copy. `ProjectModules::Open`, which `GameSession` uses, always goes through `LoadProject` when `project.modules` is non-empty. The graph shows this is the only path: `cypher` finds exactly two callers of `Supported` (`LoadProject`, `Load`), and `ProjectModules::Open` is called only from `GameSession` and `EditorModules`. The sample starter is added only `if(INTERI_BUILD_SHARED)`. There is no way to link a module statically into the executable.
- **Impact:** Current-gen consoles restrict loading executable code at run time. Code generally has to be signed and packaged with the title, and can't be loaded from a writable temp directory. Static linking is the norm there. Interi's gameplay lives entirely in modules (CLAUDE.md "gameplay engines that live in modules"), so a static or console build can't run any real project. The private console backends will each have to fork the module host. On PC, a shipped game also copies DLLs into `%TEMP%` on every start, which antivirus and sandboxed stores (Flatpak, the Microsoft Store) treat badly.

### PLAT-002 — The shader toolchain and the renderer are locked to SPIR-V with Vulkan-specific HLSL
- **Severity:** High
- **Confidence:** High
- **Category:** Graphics backend abstraction
- **Location:** `cmake/InteriShaders.cmake:44-59`, `engine/rhi/include/interi/rhi/Types.h:309`, `engine/render/src/ForwardRenderer.cpp:9-11`, `engine/shaders/include/interi/Bindless.hlsli:7-12`, `engine/render/shaders/Forward.vs.hlsl:6`, `engine/render/shaders/ForwardPush.hlsli:19`
- **Evidence:** `interi_add_shaders` runs only `dxc -spirv -T <stage>_6_6 ... -fspv-target-env=vulkan1.3 -fvk-use-scalar-layout` and embeds the one blob as `k<Name><Stage>`. `ComputePipelineDesc::code` is documented as "SPIR-V for Vulkan". The backend-neutral `interi_render` and `interi_editor_ui` libraries `#include <interi/shaders/*.h>` and pass those bytes straight to `IDevice`. The HLSL depends on Vulkan-only constructs: `[[vk::binding]]` with two resource types aliased on one binding (`Texture2D` and `Texture2DArray` share binding 1, and the two sampler types share binding 2), `[[vk::push_constant]]`, `[[vk::image_format("r32f")]]`, and `[[vk::builtin("BaseInstance")]]` for the GPU-driven object index. 16 shader files use `vk::` attributes. The layering itself is clean: no engine library except `rhi_vulkan` includes Vulkan or SDL (only `tools/editor` does).
- **Impact:** Every non-Vulkan backend (D3D12 for Xbox and Windows, and the PS5's native API) needs a second shader target and a backend-selected bytecode path through every pipeline-creation site in `render` and `editor_ui`. Each of those backends also needs replacements for aliased bindings and the BaseInstance builtin. D3D12 doesn't fold the start instance into an instance index, and SV_StartInstanceLocation needs SM 6.8. The private console repos can't add this without patching the public renderer, so the RHI abstraction stops at the shader boundary.

### PLAT-003 — Saves are written in place with truncate-then-write: power loss or a crash corrupts the slot
- **Severity:** High
- **Confidence:** High
- **Category:** Save data integrity
- **Location:** `engine/platform/src/FileSystem.cpp:91-108`, `engine/runtime/src/SaveGame.cpp:252-260`, `engine/editor/src/EditorServices.cpp:109-120`
- **Evidence:** `NativeFileSystem::WriteFile` opens the target with `std::ofstream(resolved, std::ios::binary | std::ios::trunc)`, writes, and closes. There is no temp file, no flush to disk, no rename and no backup. `SaveSlots::Write` calls it directly on `saves/slotN.isav`, synchronously on the caller (game) thread. `SavePreferences` uses the same pattern for `editor.json`. `List()` can flag a corrupt slot afterwards, but the previous good save is already gone. Graph: `SaveSlots` has one owner (`GameSession::m_slots`), and `cypher` found that the runtime's only engine `WriteFile` caller is `SaveSlots::Write`.
- **Impact:** A crash, kill or power loss during a save loses both the new and the old save. That is data loss, and "save integrity on power loss" is a standard certification theme on every console. Consoles also expect saves through a platform save container with size limits and asynchronous commits. A synchronous `IFileSystem` write with no size budget doesn't map onto that.

### PLAT-004 — Windows: non-ASCII paths break module loading (UTF-8 versus ANSI code-page mix)
- **Severity:** High
- **Confidence:** High
- **Category:** Windows Unicode
- **Location:** `engine/hotreload/src/ModuleHost.cpp:412`, `engine/hotreload/src/ModuleHost.cpp:450-458`, `engine/platform/src/DynamicLibrary.cpp:43-47`, `engine/platform/include/interi/platform/DynamicLibrary.h:23-24`, `tools/editor/main.cpp:129-131`
- **Evidence:** `DynamicLibrary::Open(const std::string&)` is documented as UTF-8 and decodes with `MultiByteToWideChar(CP_UTF8, ...)`. `ModuleHost::OpenLibrary` passes `shadow.string()`. On Windows, `std::filesystem::path::string()` converts to the active ANSI code page, not UTF-8, and throws `std::system_error` for characters that page can't represent. The default shadow directory is `temp_directory_path()/interi_modules`, which is under `C:\Users\<name>\AppData\Local\Temp`. `std::format("...{}", library.string())` is used for error messages too. `level_editor` takes `char** argv` (ANSI) for project and module paths. The engine has no UTF-8 process code-page manifest.
- **Impact:** On Windows accounts with non-ASCII user names, which are common in European and Asian locales, every module load either fails with "LoadLibraryW failed" (a mis-decoded path) or throws through `noexcept`-free code that assumes no exceptions. No module loads, so the game or the editor's play mode doesn't work. Linux isn't affected.

### PLAT-005 — No application lifecycle, user or platform-services abstraction
- **Severity:** Medium
- **Confidence:** High
- **Category:** Console readiness / lifecycle and services
- **Location:** `engine/platform/include/interi/platform/InputEvents.h:146-147`, `engine/platform_sdl/src/SdlPlatform.cpp:255-300`, `engine/platform_sdl/include/interi/platform_sdl/SdlPlatform.h:33-73`
- **Evidence:** `InputEvent` is `variant<Key, MouseButton, MouseMotion, MouseWheel, GamepadConnection, GamepadButton, GamepadAxis, WindowResized, TextInput, Quit>`. `TranslateSdlEvent` drops focus gained/lost, minimise/restore, background/foreground (suspend/resume), display and display-scale changes, and locale changes. Gamepads are identified by the SDL joystick id only, with no player or user pairing. A search of `engine/` for achievement, suspend, resume, presence, user id, entitlement and cloud-save abstractions found none.
- **Impact:** Consoles require suspend/resume handling, user sign-in and sign-out, controller-to-user pairing, constrained mode, and achievements, trophies or activities through a services layer. A PC storefront build needs Steamworks, EOS or GDK behind an interface. Without focus-lost events, keys held during Alt-Tab stay "down" in `InputMapper`, and the game can't pause on focus loss. Each console backend would have to invent these interfaces privately, which forks gameplay code.

### PLAT-006 — The window layer has no fullscreen or display control and mixes mouse window coordinates with pixel sizes
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Windows / Linux desktop (DPI, display modes)
- **Location:** `engine/platform/include/interi/platform/Window.h:18-43`, `engine/platform_sdl/src/SdlPlatform.cpp:339-357`, `engine/platform_sdl/src/SdlPlatform.cpp:128-135`, `engine/platform_sdl/src/SdlPlatform.cpp:280-282`
- **Evidence:** `WindowDesc` has only title, size, resizable and vulkan. `IWindow` exposes `Size()` (pixels, `SDL_GetWindowSizeInPixels`), `SetTitle` and `NativeHandle`. There is no fullscreen, borderless, display selection, display scale or minimised state. The window is created with `SDL_WINDOW_HIGH_PIXEL_DENSITY`, but `MouseMotionEvent` copies `event.motion.x/y`, which SDL3 reports in window coordinates (points). On scaled Wayland or macOS-style backends these differ from the pixel size the renderer and ImGui use.
- **Impact:** A shipped PC game can't offer exclusive or borderless fullscreen or choose a monitor. On HiDPI or fractionally scaled Wayland desktops (including Steam Deck desktop mode), mouse picking and UI clicks are offset by the scale factor, and the editor UI isn't scaled.

### PLAT-007 — No shipping build configuration: INTERI_SHIPPING is never defined by any preset or option
- **Severity:** Medium
- **Confidence:** High
- **Category:** Toolchains and builds
- **Location:** `engine/core/include/interi/core/Config.h:6-11`, `CMakePresets.json` (all release presets use `CMAKE_BUILD_TYPE=RelWithDebInfo`), `engine/sdk/include/interi/sdk/Toolchain.h:69-73`
- **Evidence:** `INTERI_SHIPPING` compiles out asserts, the StringId name table and profiling, and it is part of the module toolchain fingerprint. A search of tracked files finds it defined only in `tests/core/ProfilingShippingTests.cpp:6`. No CMake option, preset or CI job sets it. No `Release`/`MinSizeRel` preset exists, and no `windows-*`/`linux-*` shipping preset exists.
- **Impact:** The configuration a store build or a console submission would ship has never been built or tested on either compiler. A module built in "shipping" mode would also need a matching shipping engine, which nobody builds.

### PLAT-008 — No runnable game executable or package; the editor bakes an absolute source path
- **Severity:** Medium
- **Confidence:** High
- **Category:** Packaging and storefronts
- **Location:** `tools/editor/CMakeLists.txt:4-8`, `cmake/InteriPackage.cmake:32-34`, `tools/editor/main.cpp:157`, `engine/game/src/GameSession.cpp:44-48`
- **Evidence:** `cypher` on `main` functions in first-party code finds only `tools/editor/main.cpp` and `tools/module_check/main.cpp`. `GameSession` is exercised only by tests. `level_editor` gets `INTERI_EDITOR_DATA_DIR="${PROJECT_SOURCE_DIR}/data"` and is not in the install set (`install(TARGETS ${_libraries} interi_build_settings interi_module_check ...)`). No app manifest, `.rc`, icon, `.desktop`/AppStream file, Flatpak/AppImage recipe or Steam depot script is tracked. `GameSession` saves only if the caller supplies `saveDirectory`, and the engine has no per-user save or config path helper for games (only the editor's `DefaultPreferencesPath`).
- **Impact:** No artefact can be put on a store or a Steam Deck today. A built editor works only on the machine that built it, because the data path points into the source tree. Each game has to reinvent the windowed player loop, per-user save locations (`%APPDATA%`/`XDG_DATA_HOME`) and packaging.

### PLAT-009 — The CI platform matrix misses the Linux runtime baseline, Windows GPU coverage and the static engine
- **Severity:** Medium
- **Confidence:** High
- **Category:** Toolchains and builds / Linux distribution
- **Location:** `.github/workflows/ci.yml:30-31`, `.github/workflows/ci.yml:43-48`, `.github/workflows/ci.yml:93-114`, `CMakeLists.txt:24`
- **Evidence:** All Linux jobs run in `container: fedora:44` (a very new glibc and libstdc++). Windows tests run with the comment `# GPU tests skip: no Vulkan driver`. No job configures `-DINTERI_BUILD_SHARED=OFF`, `-DINTERI_SIMD=OFF` or a Steam Runtime (sniper) container.
- **Impact:** Linux binaries built this way need Fedora 44's glibc and libstdc++, so they won't start on the Steam Runtime or older LTS distributions. The Windows Vulkan renderer, swapchain and goldens never run in CI. The static configuration a console port needs (see PLAT-001) and the scalar path can break without anyone noticing.

### PLAT-010 — Virtual paths are case-preserving with no case or reserved-name checks
- **Severity:** Medium
- **Confidence:** High
- **Category:** Portability / file systems
- **Location:** `engine/platform/src/FileSystem.cpp:14-46`, `engine/platform/src/FileSystem.cpp:55-59`
- **Evidence:** `NormalizeVirtualPath` rejects `..` and `:` and unifies separators, but it keeps case and accepts Windows reserved names (`CON`, `NUL`, `COM1`) and trailing dots or spaces. `NativeFileSystem::Resolve` appends the result to the root unchanged.
- **Impact:** Content authored on Windows (case-insensitive NTFS) that references `Content/Coin.png` while the file is `content/coin.png` loads on Windows and fails on Linux, the Steam Deck and consoles. A file named `aux.json` on Linux can't be checked out or written on Windows. These bugs show up only after content ships to another platform.

### PLAT-011 — The x86-64-v3 baseline has no runtime CPU check
- **Severity:** Medium
- **Confidence:** High
- **Category:** Compiler / ISA portability
- **Location:** `cmake/InteriArch.cmake:20-27`, `README.md:35`
- **Evidence:** Every target is compiled with `-march=x86-64-v3` or `/arch:AVX2`. A search of first-party code for `__builtin_cpu_supports`, `__cpuid` or `IsProcessorFeaturePresent` finds nothing.
- **Impact:** On a PC without AVX2/FMA/BMI2 (pre-Haswell Intel, some low-end Pentium/Celeron parts, older VMs), the process dies with an illegal-instruction fault, possibly during static initialisation, with no message. The baseline is right for PS5 and Xbox, but a PC store build needs a clear "unsupported CPU" message or a fallback.

### PLAT-012 — x86 intrinsics are outside the SIMD gate in the job system (ARM64 cannot compile)
- **Severity:** Low
- **Confidence:** High
- **Category:** Compiler / architecture portability
- **Location:** `engine/jobs/src/JobSystem.cpp:21-25`, `engine/jobs/src/JobSystem.cpp:65-68`, `engine/core/include/interi/core/Config.h:36`
- **Evidence:** `JobSystem.cpp` includes `<immintrin.h>` (or `<intrin.h>`) unconditionally and calls `_mm_pause()` in `CpuPause`. That ignores the `INTERI_SIMD_*` gating the math code uses (`MathKernels.h:9-14`). `kCacheLineSize` is hard-coded to 64. No tracked first-party file mentions `aarch64` or `ARM64`.
- **Impact:** ARM64 targets (Windows on ARM, Linux ARM, ARM-based handhelds) fail to compile the job system. ARM isn't claimed today, so this is Low, but it is the first blocker for any ARM target.

### PLAT-013 — Platform conditionals fall through to POSIX silently, with no #error for unknown targets
- **Severity:** Low
- **Confidence:** High
- **Category:** Platform define hygiene
- **Location:** `engine/core/src/memory/HeapAllocator.cpp:17-31`, `engine/platform/src/DynamicLibrary.cpp:6-16`, `engine/jobs/src/JobSystem.cpp:80-92`, `engine/hotreload/src/ModuleHost.cpp:334-344`, `engine/sdk/include/interi/sdk/Toolchain.h:20-29`
- **Evidence:** Each site is `#if defined(_WIN32) ... #else <POSIX>` (`posix_memalign`, `dlopen`, `.so` names). `PinCurrentThread`'s `#else` is `(void)cpu;`. `Toolchain.h` falls back to `"unknown-compiler"` and `"unknown-stdlib"`. There are about 29 platform or compiler conditionals across 14 first-party files, and they aren't centralised.
- **Impact:** A new platform (a private console repo) compiles against the wrong branch and fails at link or run time, not at a clear compile-time `#error`. The private backends have to patch public files to add their branch.

### PLAT-014 — Thread affinity and naming are naive
- **Severity:** Low
- **Confidence:** High
- **Category:** Threading / core affinity
- **Location:** `engine/jobs/src/JobSystem.cpp:78-92`, `engine/jobs/src/JobSystem.cpp:103-106`, `engine/jobs/src/JobSystem.cpp:134-143`
- **Evidence:** With `pinThreads`, worker *i* is pinned to logical CPU `(i+1) % hardware_concurrency()`. That ignores the process's affinity mask and cgroup quota on Linux, so `INTERI_VERIFY(result == 0)` fires under `taskset` or containers. On Windows it uses a single-group 64-bit mask. `DefaultWorkerCount()` is `hardware_concurrency() - 1`, regardless of SMT siblings or P/E cores. No `SetThreadDescription` or `pthread_setname_np` call exists in first-party code.
- **Impact:** Pinning can assert on constrained hosts, and it oversubscribes on hybrid CPUs. Consoles need explicit per-platform core maps, which the `workerCount`/`pinThreads` description can't express. Unnamed workers make platform profilers and crash dumps harder to read.

### PLAT-015 — The audio device has no device-change or device-lost handling and only stereo output
- **Severity:** Low
- **Confidence:** High
- **Category:** Platform audio
- **Location:** `engine/audio/src/AudioEngine.cpp:194-227`
- **Evidence:** `ma_device_config` sets format, `channels = 2`, sample rate, period and data callback, and no `notificationCallback`. Nothing reacts to stop, reroute or unplug.
- **Impact:** Unplugging a headset, or a Bluetooth controller headset disconnecting, can leave the game silent until restart on backends without automatic rerouting. That is a common certification complaint, and there is no surround output.

### PLAT-016 — SDL3 on Linux is built without IBus/D-Bus
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Linux desktop
- **Location:** `vcpkg.json` (`sdl3` with `"default-features": false` and only `wayland`/`x11` on Linux plus `vulkan`), `external/vcpkg/ports/sdl3/vcpkg.json` (its default features include `ibus`, which pulls in `dbus`)
- **Evidence:** The manifest removes the port's default `ibus` feature and doesn't add `dbus`.
- **Impact:** CJK input-method composition doesn't reach `TextInputEvent` on Linux, which breaks the editor and in-game text fields for those users. D-Bus-based features (screensaver inhibit through the portal, some system theme and locale queries) are unavailable. This is inferred from SDL's feature documentation and wasn't tested.

### PLAT-017 — No crash reporting and no Windows application manifest
- **Severity:** Low
- **Confidence:** High
- **Category:** Windows / Linux runtime polish
- **Location:** `tools/editor/CMakeLists.txt:4`, no `*.manifest` or `*.rc` among tracked files
- **Evidence:** A search of first-party code for `MiniDumpWriteDump`, `SetUnhandledExceptionFilter`, `sigaction` or `signal(` finds nothing. No manifest declares per-monitor-v2 DPI awareness (SDL3 sets it at run time), `activeCodePage=UTF-8` (which would mitigate PLAT-004) or `longPathAware`.
- **Impact:** Crashes in the field leave no dump or stack. Paths longer than 260 characters (deep `imported/<source path>/...` trees) can fail on Windows.

### PLAT-018 — Asset cooking has no target-platform dimension
- **Severity:** Low
- **Confidence:** High
- **Category:** Cooked, platform-specific assets
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:408-436`, `engine/asset_import/include/interi/asset_import/AssetDatabase.h:83`, `engine/assets/include/interi/assets/Texture.h:22-29`
- **Evidence:** The cook key hashes importer and format versions, settings, source bytes, reads and references, but no target platform. All products go to one `imported/` folder. The texture formats are Rgba8, Rgba32F, BC1, BC4 and BC5.
- **Impact:** Per-platform output (console-specific texture tiling and formats, BC7 versus ASTC, per-platform mesh or shader packages) has no place to live. Different platforms would overwrite each other's cooks. This is fine for PC today and needed before the first console cook.

### PLAT-019 — Editor state and fallback preferences go to the shared temp directory
- **Severity:** Low
- **Confidence:** High
- **Category:** Per-user data locations
- **Location:** `tools/editor/EditorApp.cpp:90`, `engine/editor/src/EditorServices.cpp:122-143`
- **Evidence:** The dock layout is `temp_directory_path()/interi_editor_layout_v10.ini`. Preferences fall back to `temp_directory_path()/interi_editor.json` when `APPDATA` / `XDG_CONFIG_HOME` / `HOME` are missing. The XDG and APPDATA handling itself is correct.
- **Impact:** Temp directories get cleaned, and on Linux `/tmp` is shared between users, so layouts are lost or collide between users. This is minor polish.

### PLAT-020 — tools/platform_audit holds only untracked bytecode caches
- **Severity:** Info
- **Confidence:** High
- **Category:** Tooling hygiene
- **Location:** `tools/platform_audit/__pycache__/platform_audit.cpython-314.pyc`, `tools/platform_audit/tests/__pycache__/`
- **Evidence:** `git ls-files tools/platform_audit` returns nothing, and no CMake, CI or doc file references it. Only `.pyc` caches remain in the working tree.
- **Impact:** None for builds. This is a leftover from a removed or never-committed platform audit tool. Maintainers might decide whether a platform audit (for example, banned OS calls outside `engine/platform`) should exist in CI.

### PLAT-021 — Positive observations: clean backend layering and no NDA material
- **Severity:** Info
- **Confidence:** High
- **Category:** Platform abstraction
- **Location:** `engine/rhi/CMakeLists.txt:4`, `engine/platform/include/interi/platform/Window.h:27-43`, `engine/platform/src/DynamicLibrary.cpp`, `engine/core/include/interi/core/memory/HeapAllocator.h`
- **Evidence:** `interi::platform` never links SDL. `interi::rhi` and `interi::render` don't include Vulkan; only `rhi_vulkan`, `platform_sdl` and `tools/editor` do. OS calls are confined to 8 files. Every allocation goes through `IAllocator`, so a console memory budget can be hooked in. The editor's preferences honour `XDG_CONFIG_HOME` and `%APPDATA%`. A search of tracked files for console SDK identifiers found no NDA headers, libraries or define usage (one false positive: `nvn` as a test variable name).
- **Impact:** None. This is a good foundation to keep. The issues above are gaps in the layer, not leakage of OS calls through gameplay code.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes; indexed @ 4631f35 = HEAD |
| 2 | cypher | Community by symbolCount LIMIT 30 | Top 30 are all third-party (SDL Joystick/Hidapi, Tracy, Manifold, ImGui docking); no first-party cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 vcpkg buildtree flows (SDL_AppIterate, Android/KMSDRM/X11_CreateWindow); none first-party |
| 4 | query | "platform file system native path user config folder" | Only SDL_filesystem/SDL_dialog definitions from vcpkg trees (BM25 weak); no first-party hits |
| 5 | query | "window SDL platform event source" | `SdlPlatform` (Platform module), `IWindow`, `IWindow::NativeHandle`, `SdlSurfaceTest` |
| 6 | query | "thread worker count hardware concurrency" | `JobSystem::DefaultWorkerCount` (JobSystem.cpp:103), `JobSystem` ctor (:108) |
| 7 | context | DefaultWorkerCount (engine/jobs/src/JobSystem.cpp) | 1 caller (`JobSystem` ctor); exact |
| 8 | impact | IWindow upstream (Window.h) | Ambiguous across 4 candidates; class IWindow: 1 direct dependent, LOW |
| 9 | cypher | Files under `engine/platform*` | 17 files: FileSystem, AsyncFileReader, DynamicLibrary, Input*, Window, SdlPlatform |
| 10 | cypher | CALLS to OS APIs (getenv/dlopen/LoadLibrary/popen...) from engine/tools/samples | 0 rows (system calls not resolved as graph nodes; switched to literal Grep for conditionals) |
| 11 | cypher | Callers of Supported/LoadProject/Load in engine/hotreload (non-test) | Only `LoadProject` and `Load` call `Supported` |
| 12 | cypher | HAS_METHOD of SaveSlots/NativeFileSystem/VirtualFileSystem | SaveSlots: Count/Write/Read/Exists/List/PathOf; NativeFileSystem: Exists/ReadFile/WriteFile/Root/Resolve |
| 13 | context | SaveSlots (Class) | No incoming calls; typed property `GameSession::m_slots` is its only owner |
| 14 | cypher | `main`/`WinMain` functions in first-party dirs | `tools/editor/main.cpp`, `tools/module_check/main.cpp`, one test main; no game player |
| 15 | cypher | Callers of `Open` in engine/game (non-test) | `EditorModules.cpp` and `GameSession.cpp` call `ProjectModules::Open` |
| 16 | cypher | Callers of ReflectSpirv | Only `VulkanDevice::CreateComputePipeline`/`CreateGraphicsPipeline` (+1 test) |
| 17 | cypher | File IMPORTS into engine/rhi_vulkan/include from outside | 0 rows (C++ include edges sparse; confirmed layering with Grep) |

## Analysis limitations
- Static reading only. Nothing was built, configured or run, so the Windows Unicode failure (PLAT-004), the HiDPI mouse offset (PLAT-006) and the missing IME (PLAT-016) are inferred from the code and documented API behaviour, not reproduced.
- The GitNexus BM25 index had failed (as the dispatch prompt warned), and clusters and processes are dominated by vendored SDL, Tracy and ImGui sources. The `query` calls returned mostly third-party hits, so first-party discovery leaned on targeted `cypher` plus literal Grep for `#if` platform conditionals, which the graph doesn't index.
- The graph doesn't model calls into OS/CRT functions or many C++ include edges (calls #10 and #17 returned 0), so OS-call leakage was measured textually.
- The private PS5 and Xbox backend repos and `modules/` (physics, destruction) weren't available. Console findings describe public, general requirements only.
- I couldn't verify the Windows CI runtime behaviour (MSVC build results, DLL export counts) without running CI.

## Cross-discipline notes
- Security: `tools/editor/EditorApp.cpp:1187-1215` (`RunShell`) runs a build command string through `popen`/`_popen`. Check where that string comes from.
- Security: `ModuleHost` loads native code from project-specified folders with full trust (by design, see `TrustStore`). Copying into a world-writable `/tmp/interi_modules` (`ModuleHost.cpp:412`) lets another local user swap the shadow DLL between copy and `dlopen`.
- C++ / Error handling: `std::filesystem::path::string()` can throw on Windows (PLAT-004) in code that otherwise assumes `Result`-based errors.
- CI/Build: no job builds `INTERI_BUILD_SHARED=OFF` or `INTERI_SIMD=OFF` (PLAT-009).
