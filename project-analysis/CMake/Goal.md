# CMake — Goal State

## Vision
Interi configures in exactly one supported way: through `CMakePresets.json`. The manifest carries every vcpkg overlay, and any other path stops early with a clear message.

Every build mode the engine documents is a named preset that CI builds:
- shared and static engines;
- SIMD and scalar paths;
- dev and shipping;
- sanitizers;
- profiling.

Every flag that affects the module ABI fingerprint reaches installed consumers through `interi::build_settings`. The installed package contains only the engine and is relocatable. It states clearly which targets gameplay modules may link, and its tests prove that.

Dependencies are built by the same compiler as the engine. Optional dependencies are installed only when their feature is on.

The existing strengths are kept:
- target-based usage requirements;
- warnings isolated from third-party code;
- ALIAS targets and generated export headers;
- labelled CTest discovery;
- correct shader dependencies.

## Acceptance criteria
- [ ] AC-1: Configuring without a preset ends in one of two ways. Either it configures with Clang and the overlay port, or it stops with a message naming `cmake --preset`. It never fails inside `InteriShaders.cmake`. `vcpkg-configuration.json` declares `overlay-ports` and `overlay-triplets`, and `CMakePresets.json` no longer sets them. (resolves CMAKE-001)
- [ ] AC-2: `option(INTERI_SHIPPING)` exists, and `linux-clang-shipping` and `windows-msvc-shipping` presets exist. In a shipping build, every engine target carries the compile definition `INTERI_SHIPPING=1` (checked semantically, e.g. through `COMPILE_DEFINITIONS`/`INTERFACE_COMPILE_DEFINITIONS` or a compile-time `static_assert` in a probe TU, not by matching the `-D` or `/D` spelling in `compile_commands.json`), on Linux and Windows, and the installed `InteriTargets.cmake` lists `INTERI_SHIPPING=1` in `interi::build_settings`. `Package_Starter_BuildsOutOfTree` passes against a shipping install. (resolves CMAKE-002)
- [ ] AC-3: The `Vulkan` component has a documented, enforced purpose. Either a gameplay module that links it fails at configure time (executables only), or a package test builds and loads a `MODULE` that links `interi::rhi_vulkan`. Every installed static library has `POSITION_INDEPENDENT_CODE` ON (checked as the target property on both platforms; a literal `-fPIC` check, if any, is Linux-only, since MSVC has no such flag). `modules/physics` and `modules/destruction` still build standalone against the changed package. (resolves CMAKE-003)
- [ ] AC-4: With a packaging preset (`BUILD_TESTING=OFF`), configure and build produce zero `*_tests`/`*_benchmarks` targets and do not require GTest, Google Benchmark or Python. The default presets still build and run every test. The rule "every module needs `TEST_SOURCES`" still fails configure. (resolves CMAKE-004)
- [ ] AC-5: With a folder under `modules/` present, `cmake --install` installs the same `InteriTargets.cmake` target set as without it. Adding a module folder triggers a reconfigure (`CONFIGURE_DEPENDS`). (resolves CMAKE-005)
- [ ] AC-6: `INTERI_SANITIZER` has a `STRINGS` property. `cmake -S . -B .` fails with an in-source-build message. A test fails when `project(VERSION)` and `vcpkg.json` `version` differ. (resolves CMAKE-006)
- [ ] AC-7: Every vcpkg port in the Linux presets is configured with `clang++`, shown by the port's `CMAKE_CXX_COMPILER` in its vcpkg build log, including in the TSan triplet. (resolves CMAKE-007)
- [ ] AC-8: `test-base` sets `execution.jobs` and `execution.timeout`. `ctest --preset linux-clang-debug` runs in parallel and stays green across 5 consecutive runs. (resolves CMAKE-008)
- [ ] AC-9: Either the package installs `InteriShaders.cmake`, `EmbedSpirv.cmake` and `Bindless.hlsli`, and `Package_VulkanComponent_BuildsOutOfTree` compiles an HLSL file through them, or the docs state that modules do not own shader pipelines. (resolves CMAKE-009)
- [ ] AC-10: `interi::vulkan_testing` resolves its directory through `@PACKAGE_INIT@`/`set_and_check`, and works when installed with `-DCMAKE_INSTALL_LIBDIR=lib/x86_64-linux-gnu`. Lavapipe discovery doesn't depend on one hard-coded absolute path. (resolves CMAKE-010)
- [ ] AC-11: CI builds and unit-tests static-engine, scalar and shipping presets, at least nightly. No hand-maintained PIC target list remains in `engine/hotreload/CMakeLists.txt`. (resolves CMAKE-011)
- [ ] AC-12: Tracy is installed only when a profiling preset is used (vcpkg manifest feature). `ctest -L license` passes in profile and non-profile presets. (resolves CMAKE-012)
- [ ] AC-13: The Info items have a recorded decision, as a comment in the relevant CMake file or a line in CLAUDE.md:
  - explicit MSVC `/fp:precise` (CMAKE-013);
  - symbol visibility policy (CMAKE-014);
  - shader debug info and depfile (CMAKE-015).

## Guardrails to keep it that way
- **ABI flags reach consumers.** Every build switch that the toolchain fingerprint (`engine/sdk/include/interi/sdk/Toolchain.h`) keys on must be a CMake option whose define is appended to `INTERI_ABI_DEFINITIONS`. Add this to the review checklist next to "bump `INTERI_MODULE_ABI_VERSION`".
- **Presets and CI stay in sync.** Each documented build mode has a preset and a CI job (push or nightly). A preset without a CI job is a review blocker.
- **Package tests prove consumer behaviour.** The `Package_*` integration tests stay the proof of what consumers can do. Any new installed target or component gets an out-of-tree consumer test.
- **Keep what was verified.** These are checked today and must stay true:
  - no `include_directories`, `add_definitions`, `link_libraries` or `CMAKE_CXX_FLAGS` edits in first-party CMake (a `git grep` in the lint job can enforce this);
  - third-party includes stay `SYSTEM`;
  - `interi::warnings` stays `BUILD_INTERFACE`-only on exported targets.
- **Target graph mirrors includes.** Cross-module `#include <interi/<x>/...>` must be backed by a declared `DEPS` entry. A simple `git grep` script in CI can compare them.
- **License gate covers feature sets.** `ThirdPartyNotices_UpToDate` and the license checks run for every manifest feature combination that a preset enables.

## Out of scope
- C++20 modules: `CMAKE_CXX_SCAN_FOR_MODULES OFF` is a deliberate choice.
- Console (PS5/Xbox) presets and toolchains: they live in private repositories.
- A GCC preset: GCC is not a supported compiler.
- Unity builds, precompiled headers and IPO/LTO tuning: no finding requires them.
- The internal CMake of `modules/physics` and `modules/destruction` (separate projects), and the vendored vcpkg ports.
- CI cache-key design (CI/DevOps discipline) and module-host layering (Architecture discipline).
