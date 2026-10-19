# CMake — Issues

**Project:** Interi (`/home/eric/CLionProjects/Interi`)
**Analyzed:** 2026-10-19
**Scope:** Every tracked CMake file: root `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`, 13 modules in `cmake/` (`InteriModule`, `InteriArch`, `InteriWarnings`, `InteriSanitizers`, `InteriBuildSettings`, `InteriShaders`, `EmbedSpirv`, `InteriPackage`, `InteriConfig.cmake.in`, `InteriGameplayModule`, `InteriVulkanTesting`, `InteriCoverage`, `MergeProfiles`), the overlay port `cmake/ports/ufbx`, both triplets in `cmake/triplets`, all 25 `engine/*/CMakeLists.txt`, plus `testing/`, `tests/` (including `tests/package`, `tests/meta`, `tests/cmake`), `tools/*`, `benchmarks/*` and `samples/modules/starter`. I also read `.github/workflows/ci.yml` to see how presets are used, and `engine/sdk/include/interi/sdk/Toolchain.h` because the build flags feed the module ABI fingerprint. I did not read `modules/` (separate projects) or vendored vcpkg sources. CMake 4.3.0 is on the host; nothing was configured or built.
**Summary:** 15 issues — Critical: 0 · High: 0 · Medium: 3 · Low: 9 · Info: 3

**Checked with no issue found (keep these):**
- Usage requirements are target-based throughout. There is no `include_directories`, `link_directories`, `add_definitions`, `link_libraries` or `CMAKE_CXX_FLAGS` editing in the tree.
- Every engine library has a namespaced `interi::` ALIAS and a generated `Export.h`.
- Third-party warnings are isolated. vcpkg includes are `-isystem`, cgltf, stb and miniaudio are `SYSTEM PRIVATE`, and `interi::warnings` is `$<BUILD_INTERFACE>` on exported static libraries.
- The CRT is consistent: the `x64-windows-static-md` triplet (dynamic CRT) matches CMake's default `/MD[d]`.
- Each module's declared `DEPS` cover every cross-module `#include <interi/<x>/...>` in its sources. I compared all 25 modules with `git grep`, and the target graph mirrors the include graph.
- `check` found no first-party import cycles.
- Shader `DEPENDS` cover every `.hlsli` that is actually included.
- `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)` is used, and tests are labelled.
- Install/export works: `GNUInstallDirs`, `configure_package_config_file`, a version file and `find_dependency` calls are all present.
- CI runs the same presets that developers use.

## Findings

### CMAKE-001 — Configuring without a preset is half-supported and cannot succeed
- **Severity:** Medium
- **Confidence:** High (missing overlay); Medium (exact failure mode in the local IDE build)
- **Category:** Presets and toolchains / dependency strategy
- **Location:** `CMakeLists.txt:4-8`, `CMakePresets.json:5-24`, `vcpkg.json:51`, `cmake/InteriShaders.cmake:13-14`
- **Evidence:**
  - The root file deliberately supports configuring without a preset. It sets the vcpkg toolchain when none is given (`if(NOT DEFINED CMAKE_TOOLCHAIN_FILE) set(CMAKE_TOOLCHAIN_FILE ".../vcpkg.cmake" CACHE FILEPATH ...)`).
  - Everything else the build needs exists only in the presets:
    - `VCPKG_OVERLAY_PORTS` (`CMakePresets.json:11`);
    - the overlay triplets (`:33`, `:49`);
    - `clang++`, `LLD` and the host triplet (`:20-22`).
  - `vcpkg.json:51` lists `ufbx`, which exists only as the overlay port `cmake/ports/ufbx` and is not in the vendored baseline. A manifest install without the preset cannot resolve it.
  - `InteriShaders.cmake:13-14` builds the DXC path from `${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}` without checking that either variable is set. When they are empty, the search path becomes `//tools/directx-dxc`.
  - Corroboration from gitignored local state, so not authoritative: the CLion default profile `cmake-build-debug/` was configured today without a preset. It used `/usr/bin/c++`, its cache has no `VCPKG_*` entries, and it failed with `CMake Error at cmake/InteriShaders.cmake:14 (find_program): Could not find INTERI_DXC` (`cmake-build-debug/CMakeFiles/clion-Debug-log.txt`). I did not establish why the toolchain did not run there.
- **Impact:**
  - Any IDE profile, script or packager that configures without `--preset` fails, or silently builds with GCC/ld instead of the supported Clang/LLD.
  - The error message points at DXC rather than at the missing preset, so the cause is hard to diagnose.
  - The fallback on lines 5-8 suggests a path is supported when it is not.

### CMAKE-002 — `INTERI_SHIPPING` has no CMake option and is not carried to installed consumers
- **Severity:** Medium
- **Confidence:** High
- **Category:** Build configuration / ABI flags for out-of-tree modules
- **Location:** `cmake/InteriBuildSettings.cmake:7-19`, `cmake/InteriArch.cmake:17-36`, `engine/sdk/include/interi/sdk/Toolchain.h:69`, `engine/core/include/interi/core/Config.h:11`, `CMakePresets.json` (no shipping preset)
- **Evidence:**
  - `INTERI_SHIPPING` changes public behaviour and layout: it compiles out asserts, the StringId name table and profiling (`Config.h:11`, `Profiling.h:19`). It is also part of the module toolchain fingerprint (`Toolchain.h:69`: `#if defined(INTERI_SHIPPING) ... "shipping" #else "dev"`).
  - No `option()`, cache variable or preset defines it. `git grep INTERI_SHIPPING -- '*.txt' '*.cmake' CMakePresets.json` returns nothing.
  - The only shipping builds on this machine pass it through the raw flag string (`build/check-shipping/CMakeCache.txt` and `build/m16-shipping/CMakeCache.txt`: `CMAKE_CXX_FLAGS:STRING=-DINTERI_SHIPPING=1`).
  - `interi::build_settings` (which carries the global ABI flags to installed consumers) only carries `INTERI_ABI_COMPILE_OPTIONS`/`_DEFINITIONS`/`_LINK_OPTIONS`. These are collected in `InteriArch.cmake` and `InteriSanitizers.cmake`, and `INTERI_SHIPPING` is never added to them.
- **Impact:**
  - An engine built and installed for shipping exports a package that compiles modules as "dev". `ModuleHost` then rejects every such module because the fingerprint differs.
  - If the check were ever relaxed, the headers would disagree about layouts across the DLL boundary (an ODR violation).
  - Shipping is untested in CI. The package tests (`tests/package/BuildOutOfTree.cmake`) would not notice, because they never build a shipping engine.

### CMAKE-003 — The static `Vulkan` package component is offered to gameplay modules but cannot safely be linked into one
- **Severity:** Medium
- **Confidence:** Medium (no PIC: verified; link failure and duplicated state: inferred)
- **Category:** Install/export, shared-library hygiene
- **Location:** `cmake/InteriModule.cmake:39-57`, `cmake/InteriPackage.cmake:51-66`, `cmake/InteriConfig.cmake.in:16-26`, `engine/rhi_vulkan/CMakeLists.txt:23-31`, `engine/platform_sdl/CMakeLists.txt:8-13`, `engine/platform_sdl/src/SdlPlatform.cpp:19`
- **Evidence:**
  - `interi_add_module(... COMPONENT Vulkan)` builds `interi_rhi_vulkan` and `interi_platform_sdl` as `STATIC`, without `POSITION_INDEPENDENT_CODE`. The project never sets `CMAKE_POSITION_INDEPENDENT_CODE`.
  - In `build/linux-clang-release/compile_commands.json`, `VulkanDevice.cpp` and `SdlPlatform.cpp` compile with neither `-fPIC` nor `-fPIE`.
  - The package installs them as `interi::rhi_vulkan`/`interi::platform_sdl`. CLAUDE.md describes them as being "for modules that draw, open windows or run GPU tests", and gameplay modules are `MODULE` libraries (`InteriGameplayModule.cmake:18`).
  - Their private dependencies (SDL3, volk, VMA, SPIRV-Reflect) are static in both triplets.
  - `SdlPlatform.cpp:19` keeps a process-wide "one platform" guard (`std::atomic<bool> g_platformAlive`) in an anonymous namespace. volk keeps its Vulkan function pointers in globals.
  - The only out-of-tree consumer test, `tests/package/vulkan_consumer`, links them into an executable, never into a module.
- **Impact:**
  - On Linux, linking the archive into a `.so` relies on the compiler's PIE default and may fail with "recompile with -fPIC" relocation errors.
  - Where it does link (Windows, or Linux when it happens to work), each module DLL gets its own copy of SDL, volk, VMA and the SDL-platform guard, separate from the host's. The module's volk is never initialised by the host, and two "single" SdlPlatforms can coexist.
  - That contradicts the rule "one copy of each global" that the shared engine exists for (`InteriModule.cmake:19-21`).

### CMAKE-004 — `BUILD_TESTING` is ignored: tests, benchmarks and their dependencies are always required
- **Severity:** Low
- **Confidence:** High
- **Category:** Testing / packaging
- **Location:** `CMakeLists.txt:36`, `CMakeLists.txt:44-55`, `cmake/InteriModule.cmake:76-82`
- **Evidence:**
  - `include(CTest)` defines `BUILD_TESTING`, but nothing reads it: `git grep BUILD_TESTING` finds no use.
  - `find_package(GTest ... REQUIRED)`, `find_package(benchmark ... REQUIRED)` and `find_package(Python3 REQUIRED ...)` are unconditional. `tests/` and `benchmarks/` are always added.
  - `interi_add_module` always creates `interi_<name>_tests`.
  - `include(CTest)` also generates CDash dashboard targets (`Experimental`, `Nightly`, ...) and `DartConfiguration.tcl`, which the project does not use.
- **Impact:**
  - Building the engine only to `cmake --install` it for module authors also compiles every test and benchmark executable. It also pulls GTest, Google Benchmark and Python into every dependency set.
  - This is a deliberate TDD rule, so the cost is mostly build time and packaging flexibility.

### CMAKE-005 — In-tree `modules/*` projects can leak into the engine package; the directory glob is not re-evaluated
- **Severity:** Low
- **Confidence:** Medium (inferred from global-property flow; `modules/` not read)
- **Category:** Install/export
- **Location:** `CMakeLists.txt:62-74`, `cmake/InteriModule.cmake:38,57`, `cmake/InteriPackage.cmake:15,54`, `cmake/InteriCoverage.cmake:15-17`
- **Evidence:**
  - The root adds every `modules/<name>/` with `add_subdirectory` (lines 62-71) before `include(InteriPackage)` and `include(InteriCoverage)` (lines 73-74).
  - Those read the global properties `INTERI_SHARED_LIBRARIES` and `INTERI_COMPONENT_Vulkan`, which `interi_add_module` appends to.
  - So by construction, a module project that uses `interi_add_module(... SHARED|COMPONENT ...)` is installed into the engine's `InteriTargets` export and package.
  - `file(GLOB INTERI_MODULE_DIRS LIST_DIRECTORIES true ...)` has no `CONFIGURE_DEPENDS`, so a new module folder is ignored until someone reconfigures by hand.
- **Impact:**
  - The engine package's contents can depend on which gitignored folders exist on the build machine, which makes packages non-reproducible.
  - A module's targets could end up in `interi::` and clash with the module's own standalone package.

### CMAKE-006 — Option and project hygiene
- **Severity:** Low
- **Confidence:** High
- **Category:** Modern CMake style
- **Location:** `CMakeLists.txt:10`, `CMakeLists.txt:21`, `vcpkg.json:4`, `CMakeLists.txt` (no in-source guard)
- **Evidence:**
  - `INTERI_SANITIZER` is a free-form `CACHE STRING`. It has no `set_property(CACHE INTERI_SANITIZER PROPERTY STRINGS "" asan tsan)`, so IDEs and `ccmake` offer no choices; validation only happens later in `InteriSanitizers.cmake:19-21`.
  - There is no guard against in-source builds (`CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR`).
  - The version lives in two places: `project(Interi VERSION 0.1.0 ...)` and `"version": "0.1.0"` in `vcpkg.json`.
- **Impact:** Minor friction and a chance of drift (for example, bumping one version and not the other changes the package's `SameMinorVersion` compatibility but not the manifest).

### CMAKE-007 — The default Linux triplet builds dependencies with the host's default compiler, not the engine's Clang
- **Severity:** Low
- **Confidence:** Medium (configuration: High; compiler observed in local build logs only)
- **Category:** Dependency strategy / reproducibility
- **Location:** `CMakePresets.json:15-24`, `cmake/triplets/x64-linux-tsan.cmake:15`
- **Evidence:**
  - Linux presets other than TSan use vcpkg's stock `x64-linux` triplet. Nothing chainloads a toolchain or sets the compiler, so ports use `/usr/bin/c++`.
  - `external/vcpkg/buildtrees/gtest/config-x64-linux-dbg-CMakeCache.txt.log` shows `CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/c++` (GCC on this Fedora host; the logs also show GNU 10.2.1 and 16.2.1 runs).
  - The TSan triplet does force Clang, but only through `VCPKG_CMAKE_CONFIGURE_OPTIONS` (line 15), which affects only CMake-based ports.
- **Impact:**
  - Dependency binaries depend on whatever `c++` the machine has. This is the same standard library (libstdc++), so it is ABI-compatible today.
  - However, it weakens reproducibility and the "one compiler" premise behind the toolchain fingerprint.
  - It also means ASan and coverage builds link dependencies built by a different compiler.

### CMAKE-008 — Test presets set no parallelism or timeout
- **Severity:** Low
- **Confidence:** High
- **Category:** CTest
- **Location:** `CMakePresets.json:72-75`
- **Evidence:**
  - `test-base` sets `output.outputOnFailure`, `noTestsAction` and the `pending` filter, but no `execution.jobs` and no `execution.timeout`.
  - CI runs `ctest --preset <p> -L ...` (`ci.yml:70,114,136,173`), so roughly 1,000+ discovered tests run serially.
  - The only per-test limit is CTest's default (1500 s, via `include(CTest)`).
  - The project already uses `RESOURCE_LOCK module_shadow_copy` (`engine/editor/CMakeLists.txt:93`, `tools/module_check/CMakeLists.txt:16`), so it is prepared for parallel runs.
- **Impact:** CI and local test runs are slower than necessary, and a hung stress test blocks a job for 25 minutes per test.

### CMAKE-009 — Shader build tooling is not installed with the package
- **Severity:** Low
- **Confidence:** High
- **Category:** Install/export, shader integration
- **Location:** `cmake/InteriPackage.cmake:51-90`, `cmake/InteriShaders.cmake:1-17`
- **Evidence:**
  - The package installs `InteriGameplayModule.cmake` and `InteriVulkanTesting.cmake`.
  - It does not install `InteriShaders.cmake`, `EmbedSpirv.cmake` or `engine/shaders/include/interi/Bindless.hlsli`, which declares the one bindless layout every pipeline must use.
  - The Vulkan component is still advertised for modules that draw.
- **Impact:**
  - An out-of-tree module that needs its own shader has to copy the DXC invocation (SM 6.6, `-fvk-use-scalar-layout`, `-HV 2021`) and the bindless header.
  - Copies drift from the engine's flags and layout, and `ReflectSpirv` rejects resources outside the shared tables.

### CMAKE-010 — `InteriVulkanTesting.cmake` hard-codes the install layout and the lavapipe path
- **Severity:** Low
- **Confidence:** High
- **Category:** Install/export relocatability
- **Location:** `cmake/InteriVulkanTesting.cmake:7-9`, `engine/rhi_vulkan/CMakeLists.txt:11`
- **Evidence:**
  - The prefix is computed as `${CMAKE_CURRENT_LIST_DIR}/../../..`. That assumes `CMAKE_INSTALL_LIBDIR` is exactly one directory deep (it is `lib/x86_64-linux-gnu` on Debian multiarch).
  - The data directory is the literal `share/interi/gpu_testing`, although it was installed to `${CMAKE_INSTALL_DATADIR}/interi/gpu_testing` (`InteriPackage.cmake:74-76`).
  - It does not use `@PACKAGE_INIT@`/`set_and_check`.
  - `find_file(INTERI_LAVAPIPE_ICD lvp_icd.x86_64.json PATHS /usr/share/vulkan/icd.d NO_DEFAULT_PATH)` hard-codes one distro path and one architecture. The result is baked into a compile definition.
- **Impact:**
  - On a multiarch prefix, or with a custom `CMAKE_INSTALL_DATADIR`, `interi::vulkan_testing` points at a non-existent directory, and consumers fail at compile time with a missing header.
  - Lavapipe in a non-standard location (Nix, `/usr/local`, Flatpak SDKs) is silently not selected.

### CMAKE-011 — The static-engine and scalar configurations are not built in CI; the static-mode PIC list is stale
- **Severity:** Low
- **Confidence:** High
- **Category:** Configuration coverage
- **Location:** `engine/hotreload/CMakeLists.txt:24-30`, `.github/workflows/ci.yml:48,97,152`
- **Evidence:**
  - With `INTERI_BUILD_SHARED=OFF`, the hotreload CMake sets `POSITION_INDEPENDENT_CODE ON` on a fixed list: `interi_core ... interi_chain interi_hotreload`. The comment says "The sandbox still builds its module". No sandbox exists in the tree, and the list omits `interi_anim`, `interi_nav` and `interi_runtime`, which `interi_hotreload` now links (line 17).
  - In-tree `modules/*` are still added in static mode (`CMakeLists.txt:62-71`), so a module linking static `hotreload` would hit non-PIC dependencies.
  - Neither `INTERI_BUILD_SHARED=OFF` nor `INTERI_SIMD=OFF` is in any preset or CI matrix. They were only checked by hand (`build/check-static`, `build/m21-static.*`, `build/m16-scalar`).
- **Impact:** Two documented build modes can break unnoticed, and the PIC list already lags the dependency graph.

### CMAKE-012 — Optional dependencies are not modelled as vcpkg manifest features
- **Severity:** Low
- **Confidence:** High
- **Category:** Dependency strategy
- **Location:** `vcpkg.json:7-56`, `engine/core/CMakeLists.txt:56-60`
- **Evidence:**
  - `tracy` is only used when `INTERI_PROFILING=ON` (`engine/core/CMakeLists.txt:56-60`), but it is an unconditional manifest dependency. The same holds for `gtest` and `benchmark` (see CMAKE-004).
  - There is no `features` section, and no preset sets `VCPKG_MANIFEST_FEATURES`.
- **Impact:**
  - Every configure builds Tracy, and every `vcpkg_installed` tree contains it.
  - `gen_notices.py` reads the installed tree, so non-profiling distributions list Tracy in `THIRD_PARTY_NOTICES.md` although they don't ship it.
  - Dependency install time is spent on unused ports.

### CMAKE-013 — MSVC floating-point contraction relies on the compiler default
- **Severity:** Info
- **Confidence:** Medium
- **Category:** Compiler flags / determinism
- **Location:** `cmake/InteriArch.cmake:28-33`
- **Evidence:** `-ffp-contract=off` is added only for non-MSVC compilers (`if(NOT MSVC)`). MSVC relies on its default `/fp:precise` not contracting `a*b+c` into FMA under `/arch:AVX2`. No `/fp:` flag is set explicitly, so a toolchain file or user flag could change it without any engine-side signal.
- **Impact:** None today. It is a question for the maintainers: consider adding `/fp:precise` explicitly (and documenting that `/fp:contract` must never be used) so the determinism guarantee is visible in the build files.

### CMAKE-014 — Shared libraries export every symbol on Linux as well as Windows
- **Severity:** Info
- **Confidence:** High
- **Category:** Symbol visibility
- **Location:** `cmake/InteriModule.cmake:35-48`
- **Evidence:**
  - `WINDOWS_EXPORT_ALL_SYMBOLS ON` is set on Windows. On Linux, no `CXX_VISIBILITY_PRESET hidden`/`VISIBILITY_INLINES_HIDDEN` is set, so the generated `INTERI_<NAME>_API` macros have no effect there.
  - This is a deliberate, documented parity choice: modules may call any engine function.
- **Impact:** None functionally. The engine's `.so`/`.dll` files have large dynamic symbol tables. Revisit if load times or the Windows 65,535-export limit per DLL become a concern.

### CMAKE-015 — Shader build integration is correct but always optimises and rebuilds conservatively
- **Severity:** Info
- **Confidence:** High
- **Category:** Shader build integration
- **Location:** `cmake/InteriShaders.cmake:23-54`
- **Evidence:**
  - Each shader depends on its source plus every `.hlsli` globbed (with `CONFIGURE_DEPENDS`) under the shared and per-target include directories. All current `#include`s resolve inside those directories.
  - DXC always runs with `-O3` and without `-Zi`, whatever the build configuration. There is no `DEPFILE`.
- **Impact:** Correct rebuilds, but editing any `.hlsli` recompiles all of that target's shaders, and Debug builds carry no shader debug info for RenderDoc. No action is required.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are all third-party (SDL Joystick 355, Hidapi, Tracy Profiler, Manifold, ImGui docking); no first-party cluster in the top 30 |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 example or video flows from `external/vcpkg/buildtrees/sdl3` |
| 4 | query | "toolchain fingerprint compiler standard library ABI" | `INTERI_TOOLCHAIN_FINGERPRINT`, `EngineToolchainFingerprint` (`sdk/Toolchain.h`), `ModuleApi.toolchain`, `ModuleHost::OpenLibrary` |
| 5 | query | "shader compilation embedded SPIR-V header" | Only third-party hits (stb_glprog, spirv-cross, spirv-reflect); the CMake shader pipeline is not indexed |
| 6 | query | "export API macro shared library dllexport" | Third-party export macros (hidapi, Jolt in vcpkg buildtrees, miniaudio); first-party `DynamicLibrary` |
| 7 | context | `EngineToolchainFingerprint` (sdk/Toolchain.h) | Found at line 103; 0 incoming/outgoing edges (declared in a header; the cross-TU call is unresolved) |
| 8 | context | `INTERI_TOOLCHAIN_FINGERPRINT` (include_content) | Fingerprint = ABI, compiler, stdlib, debug, std, arch, shipping, profiling, linkage; led to CMAKE-002 |
| 9 | impact | `ModuleApi` upstream (summaryOnly) | risk UNKNOWN, 0 callers resolved; treated as unresolved, not as unused |
| 10 | cypher | engine-module IMPORTS grouped with `split()` | Error: function SPLIT does not exist |
| 11 | cypher | count of engine→engine IMPORTS | 27 edges only |
| 12 | cypher | list engine→engine IMPORTS | All 27 are quoted intra-module private includes; `<interi/...>` includes are unresolved, so the target-vs-include comparison used `git grep` |
| 13 | check | cycles | 3 cycles in 2 components, all third-party (manifold wasm TS, SDL cocoa headers); 0 first-party |

## Analysis limitations
- **Search and graph coverage:**
  - The GitNexus BM25/full-text index failed to build, so `query` results came from vector search and were dominated by third-party code under `external/vcpkg/buildtrees` and `packages`.
  - IMPORTS edges only cover quoted includes, so the comparison of the target graph with the include graph was done with `git grep` on `#include <interi/...>`.
  - The graph does not index CMake files, so all CMake findings come from reading the files directly.
- **Not examined:** `modules/` (gitignored separate projects) was not read. CMAKE-003 and CMAKE-005 are therefore inferred from the engine side only.
- **Local evidence:** the evidence from `cmake-build-debug/`, `build/*/CMakeCache.txt`, `build/*/compile_commands.json` and `external/vcpkg/buildtrees/*` is local, gitignored build state. It reflects the last local runs, not necessarily CI.
- **No builds run:** nothing was configured, built, installed or tested, as the analysis was read-only. The Linux link failure in CMAKE-003 and the shipping-package fingerprint mismatch in CMAKE-002 were not reproduced.
- **MSVC:** MSVC behaviour (CMAKE-013) was not checked against a compiler.

## Cross-discipline notes
- CI/DevOps:
  - The vcpkg binary cache key (`ci.yml:61,109,131,165`) omits `cmake/ports/**`.
  - The `linux` matrix and the nightly TSan job share one key per OS, so TSan-triplet binaries are probably never saved: `actions/cache` does not overwrite an existing key.
- CI/DevOps: `INTERI_BUILD_SHARED=OFF`, `INTERI_SIMD=OFF` and shipping builds have no CI job (see CMAKE-002 and CMAKE-011).
- Architecture: `interi_hotreload` (the generic module host) links `interi::anim` and `interi::nav` publicly (`engine/hotreload/CMakeLists.txt:17`), so every gameplay module transitively links animation and navigation.
- Open Source/Licensing: `THIRD_PARTY_NOTICES.md` is generated from the installed tree, which always contains Tracy, GTest and Google Benchmark (CMAKE-012).
