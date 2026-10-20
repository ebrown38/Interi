# CMake — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. **CMAKE-002:** A shipping engine currently produces a package whose modules cannot load. Fixing it also defines the shipping configuration that CMAKE-011 adds to CI.
2. **CMAKE-001:** Moving overlays into the manifest unblocks IDE and packager configures. It is also a prerequisite for CMAKE-007, which changes how triplets are chosen.
3. **CMAKE-003:** Decide what the Vulkan component is for before more modules depend on it.
4. **CMAKE-011, CMAKE-005:** These keep the build modes and the package honest.
5. **CMAKE-012, CMAKE-004:** Manifest features and test gating are best done together.
6. **CMAKE-009, CMAKE-010:** Package polish for module authors.
7. **CMAKE-007, CMAKE-008, CMAKE-006:** Low-risk improvements, done in any order.

CMAKE-013, CMAKE-014 and CMAKE-015 are Info items with no required action. Optional notes for them are at the end.

## CMAKE-001 — Configuring without a preset is half-supported and cannot succeed
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add a `vcpkg-configuration.json` next to `vcpkg.json`, with `"overlay-ports": ["./cmake/ports"]` and `"overlay-triplets": ["./cmake/triplets"]`. The vendored vcpkg tool (2026-09-26 per `external/vcpkg/vcpkg --version`) supports these keys.
  2. Remove `VCPKG_OVERLAY_PORTS`/`VCPKG_OVERLAY_TRIPLETS` from `CMakePresets.json`.
  3. Choose one policy for configuring without a preset:
     - **(a) Support it.** Keep the toolchain fallback in `CMakeLists.txt:5-8`, and warn when the compiler is not Clang or MSVC.
     - **(b) Refuse it.** Replace the fallback with a `FATAL_ERROR` that tells the user to run `cmake --preset <name>`, issued when `CMAKE_TOOLCHAIN_FILE` is undefined.
  4. In `cmake/InteriShaders.cmake`, before line 13, fail early with a clear message if `VCPKG_INSTALLED_DIR` or `VCPKG_HOST_TRIPLET` is empty.
  5. Mention presets as the only supported entry point in the README and CLAUDE.md "Build and test" sections. CLion: enable the presets and disable the default `Debug` profile.
- **Illustrative change (not applied):**
  ```cmake
  # cmake/InteriShaders.cmake
  if(NOT VCPKG_INSTALLED_DIR OR NOT VCPKG_HOST_TRIPLET)
      message(FATAL_ERROR "Interi must be configured through vcpkg: use `cmake --preset <name>` (see CMakePresets.json)")
  endif()
  ```
- **Verification:**
  - `cmake -S . -B /tmp/np -G Ninja -DCMAKE_CXX_COMPILER=clang++` should either configure successfully (policy a) or stop with the preset message (policy b), not at `find_program(INTERI_DXC)`.
  - `cmake --preset linux-clang-debug` should still configure and resolve `ufbx`. The vendored vcpkg has no `ports/ufbx`, so it must come from the overlay.
- **Risks / trade-offs:** The overlay paths in `vcpkg-configuration.json` are relative to the manifest. Module projects that consume the engine standalone need their own overlay settings, which they already carry.

## CMAKE-002 — `INTERI_SHIPPING` has no CMake option and is not carried to installed consumers
- **Severity / Effort:** Medium / M
- **Depends on:** None (CMAKE-011 adds the CI job)
- **Steps:**
  1. In `CMakeLists.txt`, add `option(INTERI_SHIPPING "Final release build: compiles out asserts, the StringId name table and profiling" OFF)`.
  2. In `cmake/InteriArch.cmake`, or a new small block included before `InteriBuildSettings`, append `INTERI_SHIPPING=1` to `INTERI_ABI_DEFINITIONS` when the option is ON. It then reaches in-tree targets through `add_compile_definitions` and installed consumers through `interi::build_settings`.
  3. Fail at configure time if both `INTERI_SHIPPING` and `INTERI_PROFILING` are ON, since profiling is compiled out anyway.
  4. Add `linux-clang-shipping` and `windows-msvc-shipping` configure, build and test presets (RelWithDebInfo or Release plus `INTERI_SHIPPING=ON`).
  5. Optionally, have `InteriConfig.cmake.in` record the engine's fingerprint-relevant switches (`set(Interi_SHIPPING @INTERI_SHIPPING@)`) so a consumer can check them at configure time.
- **Illustrative change (not applied):**
  ```cmake
  option(INTERI_SHIPPING "Final release build (asserts, StringId names, profiling compiled out)" OFF)
  if(INTERI_SHIPPING)
      list(APPEND INTERI_ABI_DEFINITIONS INTERI_SHIPPING=1)
  endif()
  ```
- **Verification:**
  - With `cmake --preset linux-clang-shipping`, `compile_commands.json` shows `-DINTERI_SHIPPING=1` on every TU.
  - `cmake --install` followed by `Package_Starter_BuildsOutOfTree` loads the module, which proves the fingerprints match.
  - `InteriTargets.cmake` lists `INTERI_SHIPPING=1` in `interi::build_settings`' `INTERFACE_COMPILE_DEFINITIONS`.
- **Risks / trade-offs:** Shipping builds compile out asserts, so tests that depend on them must `GTEST_SKIP` (already required by CLAUDE.md). Expect some test fixes the first time the preset runs.

## CMAKE-003 — The static `Vulkan` package component is offered to gameplay modules but cannot safely be linked into one
- **Severity / Effort:** Medium / M (option A) or L (option B)
- **Depends on:** None
- **Steps:**
  1. Decide what the component is for:
     - **(A) Executables only** (GPU test executables and tools). Document this in `InteriConfig.cmake.in`, `InteriPackage.cmake` and CLAUDE.md. Enforce it in `InteriGameplayModule.cmake` by failing when a gameplay module links `interi::rhi_vulkan` or `interi::platform_sdl`, e.g. a check on the target's `LINK_LIBRARIES` after `cmake_language(DEFER ...)`.
     - **(B) Usable by modules.** Make `rhi_vulkan` and `platform_sdl` shared, like the rest of the engine (`interi_add_module(... SHARED)`), and bring SDL3/volk in as a single host-owned copy. Modules then reach the device through the host's instance.
  2. Whichever is chosen, set `POSITION_INDEPENDENT_CODE ON` on every static library that is installed (the `COMPONENT` branch in `InteriModule.cmake:51-57`). Alternatively, set `CMAKE_POSITION_INDEPENDENT_CODE ON` globally so archives never depend on the compiler's PIE default.
  3. Add a package test that matches the decision. For (A), a module that links the component must fail at configure time. For (B), a `MODULE` library that links `interi::rhi_vulkan` builds and loads under `interi_module_check`.
- **Illustrative change (not applied):**
  ```cmake
  # cmake/InteriModule.cmake, COMPONENT branch
  set_target_properties(${target} PROPERTIES EXPORT_NAME ${name} POSITION_INDEPENDENT_CODE ON)
  ```
- **Verification:**
  - `compile_commands.json` shows `-fPIC` on `VulkanDevice.cpp` and `SdlPlatform.cpp`.
  - The new package test passes on Linux and Windows.
- **Risks / trade-offs:**
  - Option B changes the module ABI surface and needs a decision about who owns SDL and volk (bump `INTERI_MODULE_ABI_VERSION`).
  - Option A's configure-time check could break the two module projects. Both use `find_package(Interi COMPONENTS Vulkan)`, and this analysis did not read `modules/`. Before landing the check, build `modules/physics` and `modules/destruction` standalone against the change and confirm that only their test executables, not their `MODULE` libraries, link the component.
  - PIC costs a negligible amount of performance on x86-64.

## CMAKE-004 — `BUILD_TESTING` is ignored: tests, benchmarks and their dependencies are always required
- **Severity / Effort:** Low / M
- **Depends on:** CMAKE-012 (manifest features for gtest/benchmark)
- **Steps:**
  1. Keep `BUILD_TESTING` ON by default and in every preset, which preserves the TDD rule.
  2. Guard `find_package(GTest|benchmark|Python3)`, `add_subdirectory(tests)` and `add_subdirectory(benchmarks)` with `if(BUILD_TESTING)`. Inside `interi_add_module`, only create the test executable when `BUILD_TESTING` is ON, but keep the `TEST_SOURCES is required` check unconditional.
  3. Do the same for `interi_add_test_executable` and `interi_add_benchmark` callers in `engine/*/CMakeLists.txt` (stress tests, GPU tests, fuzzer), e.g. by returning early inside the helpers when `BUILD_TESTING` is OFF.
  4. Replace `include(CTest)` with `enable_testing()` plus `option(BUILD_TESTING ... ON)` to drop the unused CDash targets. Alternatively, keep `include(CTest)` if CDash is planned.
  5. Add a `linux-clang-package` preset (`BUILD_TESTING=OFF`, release) used for producing installs.
- **Verification:**
  - `cmake --preset linux-clang-package && cmake --build --preset linux-clang-package` builds no `*_tests` or `*_benchmarks` targets: `ninja -t targets all | grep -c _tests` returns 0.
  - `cmake --install` still succeeds.
  - Default presets keep every test.
- **Risks / trade-offs:**
  - `tests/package` and `Package_*` tests need `BUILD_TESTING` anyway.
  - Some engine code (`interi_testing`) is test-only by nature and should move under the guard too.

## CMAKE-005 — In-tree `modules/*` projects can leak into the engine package; the directory glob is not re-evaluated
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Snapshot the global properties before adding module projects. In `CMakeLists.txt`, call `get_property(... INTERI_SHARED_LIBRARIES)` and the `INTERI_COMPONENT_*` properties before line 62, then let `InteriPackage.cmake` install only that snapshot.
  2. Alternatively, move `include(InteriPackage)` above the module loop. `InteriPackage` needs `interi_module_check`, which `tools/` already defines by then.
  3. Add `CONFIGURE_DEPENDS` to the `file(GLOB INTERI_MODULE_DIRS ...)` call, or document that adding a module folder needs a reconfigure.
- **Illustrative change (not applied):**
  ```cmake
  get_property(INTERI_ENGINE_SHARED_LIBRARIES GLOBAL PROPERTY INTERI_SHARED_LIBRARIES) # before the modules/ loop
  ```
- **Verification:**
  - With a module folder present, `cmake --install build/<preset> --prefix /tmp/i` installs no module target. Check with `grep -c interi_ /tmp/i/lib*/cmake/Interi/InteriTargets.cmake` before and after.
  - Package tests still pass.
- **Risks / trade-offs:** Coverage (`InteriCoverage.cmake`) may intentionally want module test executables. Keep that list unfiltered if so.

## CMAKE-006 — Option and project hygiene
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `set_property(CACHE INTERI_SANITIZER PROPERTY STRINGS "" asan tsan)` after `CMakeLists.txt:21`.
  2. Add an in-source build guard right after `project()`.
  3. Use one version source:
     - either read the version from `vcpkg.json` with `file(READ)` and `string(JSON ...)` before `project()`;
     - or add a `tools/license_check`-style unit test that fails when `project(VERSION)` and `vcpkg.json` `version` differ.
- **Illustrative change (not applied):**
  ```cmake
  if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
      message(FATAL_ERROR "In-source builds are not supported; use `cmake --preset <name>`")
  endif()
  ```
- **Verification:**
  - `cmake -S . -B .` fails with the message.
  - `ccmake` shows a drop-down for `INTERI_SANITIZER`.
  - The version test passes.
- **Risks / trade-offs:** None.

## CMAKE-007 — The default Linux triplet builds dependencies with the host's default compiler, not the engine's Clang
- **Severity / Effort:** Low / S
- **Depends on:** CMAKE-001 (overlay triplets in the manifest)
- **Steps:**
  1. Add `cmake/triplets/x64-linux-clang.cmake` (dynamic CRT, static libraries) that sets `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` to a small `cmake/triplets/toolchains/linux-clang.cmake`. That file sets `CMAKE_C_COMPILER clang`, `CMAKE_CXX_COMPILER clang++` and `CMAKE_LINKER_TYPE LLD`.
  2. Point the `linux-clang` preset's `VCPKG_TARGET_TRIPLET` at it.
  3. Make the TSan triplet chainload the same toolchain (plus the sanitizer flags) instead of using `VCPKG_CMAKE_CONFIGURE_OPTIONS`, so non-CMake ports are covered too.
  4. Re-check `ThirdPartyNotices_UpToDate`, because the triplet name changes the `vcpkg_installed` path.
- **Verification:** `build/<preset>/vcpkg_installed/vcpkg/blds/...`, or the port's `config-*-CMakeCache.txt.log`, shows `CMAKE_CXX_COMPILER=.../clang++` for every port.
- **Risks / trade-offs:**
  - One full rebuild of the dependencies, and the CI cache key changes.
  - Benefit: dependencies and engine share one compiler, which matches the toolchain-fingerprint premise.

## CMAKE-008 — Test presets set no parallelism or timeout
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `"execution": { "noTestsAction": "error", "jobs": 8, "timeout": 600 }` to `test-base`. A larger timeout, or a per-test `TIMEOUT` property, is needed on stress, golden and package tests.
  2. Make sure tests that share files use `RESOURCE_LOCK` (already done for `module_shadow_copy`). Watch for `Package_*` tests writing to `package-test/`; they are serialised by the fixture.
- **Verification:**
  - `ctest --preset linux-clang-debug` runs in parallel and stays green across 5 consecutive runs.
  - Wall time drops in CI.
- **Risks / trade-offs:** Parallel runs can expose hidden shared-state bugs between tests. Fix them with `RESOURCE_LOCK` rather than by going back to serial runs.

## CMAKE-009 — Shader build tooling is not installed with the package
- **Severity / Effort:** Low / M
- **Depends on:** CMAKE-003 (decide whether modules draw through their own pipelines)
- **Steps:**
  1. Install `cmake/InteriShaders.cmake` and `cmake/EmbedSpirv.cmake` to `${INTERI_INSTALL_CMAKEDIR}`. Install `engine/shaders/include/` to `${CMAKE_INSTALL_DATADIR}/interi/shaders/include`.
  2. Make `InteriShaders.cmake` package-aware: take the DXC path from an `INTERI_DXC` cache variable or `find_program(dxc)` when `VCPKG_INSTALLED_DIR` is undefined, and take the include directory relative to the config file.
  3. Include it from `InteriConfig.cmake.in` under the Vulkan component.
  4. Extend `tests/package/vulkan_consumer` to compile one HLSL file with `interi_add_shaders`.
- **Verification:** `Package_VulkanComponent_BuildsOutOfTree` builds a shader with the installed helper.
- **Risks / trade-offs:** Consumers need DXC on PATH or via vcpkg. Document this. If modules are not meant to own pipelines (CMAKE-003 option A), document that instead and close this issue.

## CMAKE-010 — `InteriVulkanTesting.cmake` hard-codes the install layout and the lavapipe path
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Generate `InteriVulkanTesting.cmake` through `configure_package_config_file`, or move its body into `InteriConfig.cmake.in`. Then use `set_and_check(_interi_gpu_testing "@PACKAGE_INTERI_GPU_TESTING_DIR@")`, with `PATH_VARS` set to `${CMAKE_INSTALL_DATADIR}/interi/gpu_testing`.
  2. Replace the fixed lavapipe search with `find_file(INTERI_LAVAPIPE_ICD NAMES lvp_icd.${CMAKE_SYSTEM_PROCESSOR}.json lvp_icd.json PATH_SUFFIXES vulkan/icd.d HINTS ENV XDG_DATA_DIRS PATHS /usr/share /usr/local/share)`. Do this in both `InteriVulkanTesting.cmake` and `engine/rhi_vulkan/CMakeLists.txt`. Alternatively, resolve the ICD at run time in `VulkanTestSupport.cpp`.
- **Verification:**
  - Install with `-DCMAKE_INSTALL_LIBDIR=lib/x86_64-linux-gnu`.
  - `Package_VulkanComponent_BuildsOutOfTree` still passes, and a missing directory fails at `find_package` with a clear `set_and_check` message.
- **Risks / trade-offs:** None.

## CMAKE-011 — The static-engine and scalar configurations are not built in CI; the static-mode PIC list is stale
- **Severity / Effort:** Low / M
- **Depends on:** CMAKE-002 (adds the shipping configuration)
- **Steps:**
  1. Replace the hand-maintained target list in `engine/hotreload/CMakeLists.txt:24-30`:
     - either set `CMAKE_POSITION_INDEPENDENT_CODE ON` when `INTERI_BUILD_SHARED` is OFF (in the root, before `add_subdirectory(engine)`);
     - or have `interi_add_module` set `POSITION_INDEPENDENT_CODE ON` on every library when the engine is static.
  2. Remove the stale "sandbox" comment.
  3. Add hidden presets `linux-clang-static` (`INTERI_BUILD_SHARED=OFF`) and `linux-clang-scalar` (`INTERI_SIMD=OFF`), plus the shipping preset from CMAKE-002.
  4. Add them to a CI job: build them on push, or in the nightly matrix with `ctest -L unit`.
- **Verification:** The new CI jobs are green. In the static build, `compile_commands.json` shows `-fPIC` on `interi_anim`, `interi_nav` and `interi_runtime`.
- **Risks / trade-offs:** More CI minutes. The nightly job is the cheapest place for these.

## CMAKE-012 — Optional dependencies are not modelled as vcpkg manifest features
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Move `tracy` into a `"features": { "profiling": { "description": "...", "dependencies": [ { "name": "tracy", "default-features": false, "features": ["on-demand"] } ] } }` entry.
  2. Optionally, move `gtest` and `benchmark` into a `"tests"` feature that is on by default.
  3. Set `VCPKG_MANIFEST_FEATURES` from the presets: `profiling` for `*-profile`, `tests` for all but a packaging preset.
  4. Regenerate `THIRD_PARTY_NOTICES.md` for each feature set, or document that the notices list the dependencies of the development build.
- **Illustrative change (not applied):**
  ```json
  "features": { "profiling": { "description": "Tracy CPU profiling", "dependencies": [ { "name": "tracy", "default-features": false, "features": ["on-demand"] } ] } }
  ```
- **Verification:** `build/linux-clang-debug/vcpkg_installed/x64-linux/share/tracy` is absent, `build/linux-clang-profile/...` has it, and `ctest -L license` passes in both.
- **Risks / trade-offs:**
  - `ThirdPartyNotices_UpToDate` compares against one committed file. Decide which feature set it reflects (the superset is simplest), or the test will flap between presets.
  - The Windows profile triplet special-cases Tracy (`x64-windows-static-md.cmake:25-27`), which still works under a feature.

## Info items (no action required)
- **CMAKE-013:** If you want the MSVC determinism guarantee to be explicit, add `/fp:precise` to `INTERI_ABI_COMPILE_OPTIONS` under `if(MSVC)`, with a comment that `/fp:contract` and `/fp:fast` are forbidden.
- **CMAKE-014:** Revisit visibility (`CXX_VISIBILITY_PRESET hidden` plus the existing `INTERI_<NAME>_API` macros) only if Linux load times or the Windows 65,535-exports-per-DLL limit become a problem. It would be an ABI decision for modules.
- **CMAKE-015:** Optionally pass `$<$<CONFIG:Debug>:-Zi -Qembed_debug>` to DXC for RenderDoc debugging, and use DXC's dependency output with `DEPFILE`, if the vendored DXC supports `-MD`/`-MF`. Verify that before switching away from the glob.
