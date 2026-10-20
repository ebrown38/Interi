# CI-CD — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi)
**Analyzed:** 2026-10-19
**Scope:** The single workflow `.github/workflows/ci.yml` (173 lines, 5 jobs). `CMakePresets.json` (10 configure, 10 build and 9 test presets). Test registration in every first-party `CMakeLists.txt` and `cmake/*.cmake` (20 `add_test` sites plus `gtest_discover_tests` via `cmake/InteriModule.cmake`). Release and packaging tooling: `cmake/InteriPackage.cmake`, `tests/package/`, `tools/license_check/`, `tools/bench_compare/`, `tools/new_module/`. Also `vcpkg.json`, `.gitmodules`, `.gitignore`, `README.md` and `CLAUDE.md`. Excluded: `external/`, `build/`, `cmake-build-*`, `modules/` (separate repos) and `project-analysis/`.
**Summary:** 18 issues — Critical: 0 · High: 2 · Medium: 8 · Low: 7 · Info: 1

## Findings

### CICD-001 — ThreadSanitizer is not a merge gate: it runs only nightly, only on `main`
- **Severity:** High
- **Confidence:** High
- **Category:** Quality gates / sanitizers
- **Location:** `.github/workflows/ci.yml:48`, `.github/workflows/ci.yml:145-152`, `CMakePresets.json:31-33`
- **Evidence:**
  - The push/PR Linux matrix is `[linux-clang-debug, linux-clang-release, linux-clang-asan, linux-clang-tidy, linux-clang-profile]` (`ci.yml:48`). It has no `linux-clang-tsan`.
  - TSan exists only in the `nightly` job, which is guarded by `if: github.event_name == 'schedule' || github.event_name == 'workflow_dispatch'` (`ci.yml:146`, matrix `ci.yml:152`).
  - GitHub runs `schedule` triggers only against the default branch, so a `milestone-N` or `autoflow/*` branch is never TSan-checked before its PR merges.
  - CLAUDE.md requires TSan before committing ("TSan matters for any concurrent code"; "`ModuleHostTest` and `ModuleProjectTest` must stay clean under TSan").
  - The engine is heavily concurrent: Chase-Lev deques, the MPMC/SPSC queues, the lock-free logger, the job system's sleep protocol and `FramePipeline`. It also has dedicated `stress`-labelled TSan suites (`engine/jobs/CMakeLists.txt:23`, `engine/runtime/CMakeLists.txt:27`, `engine/audio/CMakeLists.txt:35`, `engine/ecs/CMakeLists.txt:24`, `engine/chain/CMakeLists.txt:33`).
- **Impact:** A data race can pass every required check and land on `main`. It is discovered the next morning at best, and nothing blocks the next merge in the meantime. CI does not gate the project's most important class of defect for a job system.

### CICD-002 — Shipping, scalar-SIMD and static-engine configurations are never compiled in CI
- **Severity:** High
- **Confidence:** High
- **Category:** Build matrix coverage
- **Location:** `CMakePresets.json:25-58`, `engine/core/include/interi/core/Config.h:6-11`, `engine/core/include/interi/core/Assert.h:12`, `cmake/InteriArch.cmake:10`, `CMakeLists.txt:24`, `CMakeLists.txt:56-58`
- **Evidence:**
  - `Config.h:6` says "INTERI_SHIPPING: defined for final release builds. Asserts and profiling are compiled out." No preset, CMake file or workflow defines `INTERI_SHIPPING`. A literal search of tracked `*.txt`, `*.cmake`, `*.json` and `*.yml` finds no definition. It is only *tested* in `Config.h` and `Profiling.h`.
  - Both "release" presets are `RelWithDebInfo` (`CMakePresets.json:27-28`, `:55-56`), and asserts stay on there (CLAUDE.md). As a result, no CI build compiles with `kAssertsEnabled == false` (`Assert.h:12`). The graph finds `kAssertsEnabled` defined only in `Assert.h`, and 5 test files reference it to `GTEST_SKIP`. Those skip branches never execute.
  - `option(INTERI_SIMD ... ON)` (`InteriArch.cmake:10`) promises a scalar fallback ("`INTERI_SIMD=OFF` builds the scalar path"), but no preset or job sets it OFF.
  - `option(INTERI_BUILD_SHARED ... ON)` (`CMakeLists.txt:24`) has a documented OFF mode (`ModuleHost::Supported()` false, module tests not built, starter skipped at `CMakeLists.txt:56-58`), and that mode is never built.
- **Impact:** The configuration that actually ships is never compiled. Unused-variable warnings under `/WX`/`-Werror` once asserts compile out, code that only builds with profiling or asserts, and broken scalar or static paths all stay latent until someone tries to ship or port. That includes the console ports, which are the stated reason for the x86-64-v3 baseline.

### CICD-003 — Windows never executes the Vulkan RHI, GPU or golden-image tests
- **Severity:** Medium
- **Confidence:** High
- **Category:** Build matrix coverage / platform
- **Location:** `.github/workflows/ci.yml:20-21`, `.github/workflows/ci.yml:114`, `tests/rhi_vulkan/VulkanTestDevice.h:42-58`
- **Evidence:**
  - `ci.yml:114` reads `ctest --preset ${{ matrix.preset }} -L "unit|stress|benchmark|integration|golden" # GPU tests skip: no Vulkan driver`. `INTERI_REQUIRE_GPU` is not set for the Windows job, so `VulkanTest` skips silently (`VulkanTestDevice.h:42-58`).
  - The Windows `golden` and Vulkan `integration` legs pass trivially. That covers `interi::rhi_vulkan`, the forward renderer, particles, decals, the ImGui renderer and the editor GPU paths.
- **Impact:** Windows is a supported PC target, and MSVC-specific defects in the Vulkan backend can't be caught: struct layout or packing, `/arch:AVX2` codegen differences in shader-constant packing, and DLL-boundary issues with `volk` or VMA. A green Windows check overstates what was verified.

### CICD-004 — Concurrency group cancels nightly sanitizer runs and doubles PR-branch runs
- **Severity:** Medium
- **Confidence:** High
- **Category:** Pipeline reliability / speed
- **Location:** `.github/workflows/ci.yml:7-16`, `.github/workflows/ci.yml:29`, `:42`, `:92`, `:116`
- **Evidence:**
  ```yaml
  on:
    push: # all branches
    pull_request:
    schedule:
      - cron: "0 7 * * *"
  concurrency:
    group: ${{ github.workflow }}-${{ github.ref }}
    cancel-in-progress: true
  ```
  - A `schedule` run has `github.ref == refs/heads/main`, the same group as a push to `main`. Merging a PR while the nightly ASan/TSan job runs cancels it, and a nightly run that starts during a push run cancels that one.
  - `push` fires for every branch and `pull_request` fires for the same commit with ref `refs/pull/N/merge`. These are different groups, so every same-repo PR branch runs the full 5+3+1-job pipeline twice.
  - `lint`, `linux`, `windows` and `coverage` have no `if:`, so the nightly schedule also re-runs the whole push matrix on `main` alongside the sanitizers.
- **Impact:** Nightly sanitizer coverage, already the only TSan signal (CICD-001), can be silently cancelled. Runner minutes roughly double for PRs, which lengthens queues on the Windows runners.

### CICD-005 — Actions and container images use floating tags, and nothing updates them
- **Severity:** Medium
- **Confidence:** High
- **Category:** Pipeline supply-chain security
- **Location:** `.github/workflows/ci.yml:31`, `:34`, `:44`, `:51`, `:58`, `:73`, `:99`, `:102`, `:106`, `:118`, `:140`, `:148`, `:155`, `:162`; `.github/workflows/ci.yml:37`, `:56`
- **Evidence:**
  - Every action uses a mutable tag: `actions/checkout@v7`, `actions/cache@v6`, `actions/upload-artifact@v7`, and the third-party `ilammy/msvc-dev-cmd@v1` (`ci.yml:102`).
  - The container is `fedora:44` with no digest (`ci.yml:31`, `:44`, `:118`, `:148`).
  - No `.github/dependabot.yml` or Renovate config is tracked (`git ls-files` shows only `ci.yml` under `.github/`).
  - `actions/checkout` keeps its default `persist-credentials: true`, so the read-only `GITHUB_TOKEN` stays in `.git/config` while vcpkg builds third-party sources.
  - `git config --global --add safe.directory '*'` (`ci.yml:56`, `:126`, `:160`) disables the ownership check for all paths.
- **Impact:** `permissions: contents: read` (`ci.yml:4-5`) limits the blast radius, which is why this is Medium rather than High. A re-tagged or compromised action, or a changed base image, still runs arbitrary code in every job. It can also poison the shared `.vcpkg-cache` binary cache, which later jobs trust and link into test binaries.

### CICD-006 — Toolchain versions float, so builds, lint and module fingerprints are not reproducible
- **Severity:** Medium
- **Confidence:** High
- **Category:** Reproducibility
- **Location:** `.github/workflows/ci.yml:22-26`, `:33`, `:50`, `:93`, `engine/sdk/include/interi/sdk/Toolchain.h:11-24`, `README.md:12`
- **Evidence:**
  - `dnf install -y ... $FEDORA_PACKAGES` installs whatever `clang`, `lld`, `llvm`, `clang-tools-extra` and `mesa-vulkan-drivers` versions Fedora 44 ships that day (`ci.yml:22-26`, `:50`). The lint job uses the same unpinned `clang-tools-extra` for `clang-format` (`ci.yml:33`).
  - `windows-2025` (`ci.yml:93`) moves MSVC minor versions with each image update.
  - `README.md:12` states "CI and development use Clang 22", but nothing enforces it.
  - `Toolchain.h:11-24` bakes `clang-<major>.<minor>` or `msvc-<_MSC_VER>` into the module ABI fingerprint, and the loader rejects mismatches.
- **Impact:**
  - A Fedora point update can change `clang-format` output and turn `lint` red on an untouched PR.
  - Lavapipe or Mesa updates can shift golden-image pixels.
  - Any future published engine package gets a fingerprint that depends on the day it was built.
  - A failure can't be reproduced locally with certainty.

### CICD-007 — The vcpkg binary cache key causes permanent misses (TSan) and stale hits, and there is no compiler cache
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Pipeline caching
- **Location:** `.github/workflows/ci.yml:19`, `:58-62`, `:106-110`, `:128-132`, `:162-166`
- **Evidence:**
  - The key is `vcpkg-fedora44-<vcpkg commit>-${{ hashFiles('vcpkg.json', 'cmake/triplets/**') }}`. The same key is shared by the 5 push-matrix jobs, `coverage` and both `nightly` jobs.
  - `actions/cache` does not save on an exact-key hit. The push jobs save the key first with `x64-linux` binaries, so the `x64-linux-tsan` triplet binaries (`CMakePresets.json:33`) built by the nightly TSan job can never be saved. TSan rebuilds every dependency from source each night, including SDL3, Manifold, DXC and gtest.
  - The key omits `cmake/ports/**` (overlay port `ufbx`) and the compiler version. After a Fedora clang bump or a ufbx overlay change, vcpkg's ABI hashes change but the key still hits exactly. The rebuilt binaries are never persisted, so every run pays the rebuild until `vcpkg.json` changes.
  - `restore-keys` prefixes plus `files,...,readwrite` (`ci.yml:19`) mean old ABI entries accumulate with no pruning.
  - No ccache or sccache is configured. Each push compiles the whole engine nine times (5 Linux, 3 Windows, coverage).
- **Impact:** Long and variable pipeline times, especially the nightly TSan job and Windows. Wasted runner minutes. Cache-size eviction under GitHub's 10 GB per-repo limit makes cold builds more frequent. (Medium confidence: inferred from `actions/cache` semantics, because run logs and hit rates were not visible.)

### CICD-008 — No job has `timeout-minutes`
- **Severity:** Medium
- **Confidence:** High
- **Category:** Pipeline reliability
- **Location:** `.github/workflows/ci.yml:29`, `:42`, `:92`, `:116`, `:145`; `CMakeLists.txt:36`
- **Evidence:** None of the five jobs (`lint`, `linux`, `windows`, `coverage`, `nightly`) sets `timeout-minutes`, so GitHub's 360-minute default applies. Several tests can hang by design: CLAUDE.md records that "the profile preset hung in `ModuleHostTest`" (Tracy under the loader lock), and the job system, audio thread and TSan stress tests are long or concurrent. The test presets set no `timeout` (`CMakePresets.json:73-75`). The project uses `include(CTest)` (`CMakeLists.txt:36`), so only CTest's 1500 s per-test default applies.
- **Impact:** A deadlock regression holds a Windows runner for up to 6 hours. It delays every queued PR and hides the real signal ("hung") behind a generic cancellation.

### CICD-009 — CTest runs tests serially in every job
- **Severity:** Medium
- **Confidence:** High
- **Category:** Pipeline speed
- **Location:** `CMakePresets.json:73-75`, `.github/workflows/ci.yml:70`, `:114`, `:136`, `:173`
- **Evidence:**
  - The `test-base` preset sets `outputOnFailure` and `noTestsAction` but no `execution.jobs`.
  - No `ctest` invocation in `ci.yml` passes `-j`/`--parallel`, and `CTEST_PARALLEL_LEVEL` is not set.
  - The suite is already prepared for parallel runs: tests that share the module shadow copy declare `RESOURCE_LOCK module_shadow_copy` (`engine/editor/CMakeLists.txt:93`, `tools/module_check/CMakeLists.txt:16`, `samples/modules/starter/CMakeLists.txt:18`), and the package tests use `FIXTURES_SETUP/REQUIRED` (`tests/package/CMakeLists.txt:11`, `:38`).
- **Impact:** The test phase of every job runs on one core of a multi-core runner. Sanitizer and coverage jobs, which are slowest per test, suffer the most.

### CICD-010 — No release, versioning or packaging pipeline
- **Severity:** Medium
- **Confidence:** High
- **Category:** Delivery
- **Location:** `.github/workflows/ci.yml` (only workflow), `CMakeLists.txt:10`, `vcpkg.json:4`, `cmake/InteriPackage.cmake:19-43`
- **Evidence:**
  - `git tag | wc -l` returns `0` across 184 commits, and there is no release workflow (no `on: push: tags` or `release:` trigger).
  - The version is hard-coded `0.1.0` in two places (`CMakeLists.txt:10`, `vcpkg.json:4`) with no single source and no tag derivation.
  - `cmake --install` produces a real consumable package (shared libraries, headers, `interi_module_check` and the `Interi` CMake config, `InteriPackage.cmake:19-43`), which `Package_*` tests verify. CI never uploads it as an artifact.
  - There are no checksums, signing (Authenticode for the Windows DLLs, sigstore or GPG), PDB/DWARF symbol archive, SBOM upload (vcpkg already emits SPDX documents) or changelog generation.
- **Impact:**
  - Module authors, who must build "from source against the engine" with a matching toolchain fingerprint, have no versioned, reproducible engine package to build against.
  - Crash dumps from any distributed build can't be symbolized.
  - Releasing is entirely manual and unrepeatable.

### CICD-011 — Benchmark regression tooling exists but is not wired into CI
- **Severity:** Low
- **Confidence:** High
- **Category:** Quality gates / performance
- **Location:** `tools/bench_compare/compare_benchmarks.py:1-8`, `:44-79`, `cmake/InteriModule.cmake:111-112`, `tools/CMakeLists.txt:1-5`
- **Evidence:**
  - `compare_benchmarks.py` compares Google Benchmark JSON against a baseline (exit 1 on regression). The graph shows `compare` has a single caller, `main`.
  - A literal search finds no reference to `bench_compare` in any workflow, CMake file or doc.
  - No baseline JSON is tracked (the only tracked root-level JSON files are presets, vcpkg and license-policy files).
  - `tools/CMakeLists.txt` adds no tests for the script (unlike `license_check` and `new_module`).
  - CI runs benchmarks only as `--benchmark_dry_run` smoke tests (`InteriModule.cmake:111-112`).
- **Impact:** Performance regressions in the job system, ECS queries, containers or the renderer CPU paths go unnoticed. The comparison script itself is untested and can rot.

### CICD-012 — Coverage is reported but not gated, and artifacts have no retention policy
- **Severity:** Low
- **Confidence:** High
- **Category:** Quality gates
- **Location:** `.github/workflows/ci.yml:116-143`, `cmake/InteriCoverage.cmake:25-33`
- **Evidence:**
  - The `coverage` job builds the `coverage` target (`llvm-cov report` plus `show -format=html`) and uploads `coverage-html` on every push.
  - There is no minimum threshold and no diff or PR comment.
  - `upload-artifact` steps set no `retention-days` (`ci.yml:73-77`, `:140-143`).
- **Impact:** Coverage can drop silently. A full HTML report is stored for 90 days on every push to every branch, which consumes artifact storage.

### CICD-013 — Test selection uses a label allowlist, so a new unlabelled test would be skipped silently
- **Severity:** Low
- **Confidence:** High
- **Category:** Quality gates
- **Location:** `.github/workflows/ci.yml:70`, `:114`, `cmake/InteriGameplayModule.cmake:23-30`
- **Evidence:**
  - The Linux PR matrix and Windows run `ctest ... -L "unit|stress|benchmark|integration|golden"`. Only `coverage` (`ci.yml:136`) and `nightly` (`ci.yml:173`) run unfiltered.
  - Every test registered in today's in-tree build carries a label (verified across the 20 `add_test` sites and the `interi_add_module`/`interi_add_test_executable` helpers).
  - However, the public helper `interi_add_module_load_test` adds `${target}_Loads` with no label (`InteriGameplayModule.cmake:28`). The in-tree starter only works because it adds one afterwards (`samples/modules/starter/CMakeLists.txt:18`, "in tree: CI runs it").
  - The test presets already exclude `pending` (`CMakePresets.json:75`), so the allowlist adds nothing except this failure mode.
- **Impact:** The next raw `add_test` or module load test added without a label would run in no blocking job on Windows and in only the coverage job on Linux.

### CICD-014 — Static-analysis gaps: no CodeQL/MSVC analysis, no Python lint, no workflow lint
- **Severity:** Low
- **Confidence:** High
- **Category:** Quality gates / static analysis
- **Location:** `.github/workflows/ci.yml:29-40`, `:48`
- **Evidence:**
  - clang-tidy (Linux, `linux-clang-tidy`), clang-format and the SPDX check are present.
  - There is no CodeQL (C++ and Python), no MSVC `/analyze` leg, no `ruff`/`mypy` for the 7 Python tools (`tools/license_check`, `tools/new_module`, `tools/bench_compare`), and no `actionlint` or `zizmor` on the workflow itself.
  - The engine parses hostile input (glTF/FBX/OBJ/images, module.json, save games). Those parsers are fuzz-smoked, but no SAST covers them.
- **Impact:** Classes of defects that clang-tidy's configured checks miss (MSVC-only diagnostics, taint-style findings, Python errors in license gates, workflow misconfiguration) are caught only by review.

### CICD-015 — Fuzzing is a 2,000-run smoke test only
- **Severity:** Low
- **Confidence:** High
- **Category:** Quality gates / fuzzing
- **Location:** `engine/asset_import/CMakeLists.txt:64-67`, `.github/workflows/ci.yml:145-173`
- **Evidence:** `ImportFuzzer_CorpusSmoke` runs `-runs=2000 -seed=1` (label `stress`). The nightly job doesn't raise `INTERI_FUZZ_ITERATIONS` or run `interi_import_fuzzer` for a time budget, and new crashing inputs aren't uploaded as artifacts.
- **Impact:** The fixed seed and tiny budget re-verify the corpus but explore almost nothing new. The importers are the engine's main untrusted-input surface.

### CICD-016 — Workflow duplication, no test-result reporting, no single aggregate status check
- **Severity:** Low
- **Confidence:** High
- **Category:** Maintainability / developer experience
- **Location:** `.github/workflows/ci.yml:50-63`, `:120-133`, `:154-167`, `:99-111`
- **Evidence:**
  - The "dnf install, checkout, vcpkg commit id, cache, bootstrap" block is copied three times for Linux and once more, slightly differently, for Windows.
  - No step emits JUnit (`ctest --output-junit`) or a job summary.
  - Branch protection must list every matrix leg by name (e.g. `linux (linux-clang-debug)`) because there is no aggregate `ci-ok` job with `needs:`.
- **Impact:** A change to setup (for example, fixing CICD-007's key) must be made four times and can drift. Failures are read from raw logs. Renaming or adding a matrix leg silently drops it from required checks.

### CICD-017 — Engine changes are not verified against the module projects that consume the module API
- **Severity:** Low
- **Confidence:** Medium
- **Category:** Build matrix coverage / integration
- **Location:** `CMakeLists.txt:60-66`, `.gitignore` (`/modules/`), `CLAUDE.md` ("Verify a change to the engine's module API by building both modules standalone against it")
- **Evidence:**
  - `modules/physics` (Jolt) and `modules/destruction` are gitignored separate repos, so CI sees neither.
  - Module-API compatibility is guarded in-repo only by the starter sample, the scaffolds and the plugin stubs (`Package_*`, `PluginStub_*`).
  - Per CLAUDE.md, the real consumers are checked by hand.
- **Impact:** A `ModuleApi.h`, registrar or `IPhysicsQueries` change can pass Interi CI and break both modules. It is noticed only when someone next builds them.

### CICD-018 — Strengths worth preserving
- **Severity:** Info
- **Confidence:** High
- **Category:** Observation
- **Location:** `.github/workflows/ci.yml:4-5`, `:14-16`, `:66-70`, `:71-77`, `:78-90`; `tools/license_check/CMakeLists.txt:33-36`; `cmake/InteriShaders.cmake:7`
- **Evidence:**
  - The workflow is least-privilege (`permissions: contents: read`), with no `pull_request_target`, no secrets and no `${{ github.event.* }}` interpolated into `run:`.
  - Superseded runs are cancelled.
  - GPU tests are forced to fail rather than skip on Linux (`INTERI_REQUIRE_GPU: 1`, lavapipe).
  - Golden mismatch images are uploaded on failure.
  - A self-tested pending-test gate (`Ci_PendingLabelFilter_ExcludesSample`) blocks PRs and `main`, while the informational `continue-on-error` step applies only to feature-branch pushes, deliberately.
  - License, notices and SPDX gates are CTest-labelled `unit;license`, so they run on both Linux and Windows.
  - HLSL is compiled by DXC at build time, so shader errors fail the build.
  - Warnings are errors on both compilers.
- **Impact:** None. These are the baseline the remediation must not regress.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 552,120 edges, 4,973 communities, 614 processes; indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top 30 are all third-party (Joystick/Hidapi/SDL, Tracy, Manifold, gtest "Test" clusters); no first-party build/CI cluster |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | 30 processes, all SDL3 sources under external/vcpkg/buildtrees (SDL_AppIterate, X11_CreateWindow...); none CI-relevant |
| 4 | query | "license check third party notices" | tools/license_check: gen_notices.generate_notices/main, check_licenses.find_violations/main, test_license_tools.NoticesTests |
| 5 | query | "benchmark compare regression" | tools/bench_compare/compare_benchmarks.py: compare, main (rest third-party gbench) |
| 6 | query | "golden image update compare" | tests/render/gpu/GoldenImage.h: ExpectMatchesGolden, GoldenTolerance |
| 7 | query | "packaging install module check" | check_licenses.collect_ports, ModuleGraph.h ResolveModuleGraph/CanAddModule, ModuleManager.h ModuleStatus |
| 8 | context | compare (tools/bench_compare/compare_benchmarks.py) | 1 caller (main), calls _metrics; no process; nothing outside the script uses it, so not wired into CI |
| 9 | context | ExpectMatchesGolden (tests/render/gpu/GoldenImage.h) | calls ReadPpm/WritePpm; incoming not resolved (macro/template call sites); golden path confirmed |
| 10 | context | find_violations (tools/license_check/check_licenses.py) | 6 callers: main + 5 CheckLicensesTests methods; the license gate is unit-tested |
| 11 | cypher | nodes named kAssertsEnabled / kShipping | 1 hit: Variable kAssertsEnabled in engine/core/include/interi/core/Assert.h; no kShipping |
| 12 | impact | EngineToolchainFingerprint (upstream, summaryOnly) | Ambiguous: 2 candidates (Toolchain.cpp: 1 direct caller, LOW; Toolchain.h: UNKNOWN, no callers resolved) |

## Analysis limitations
- CI YAML, presets and CMake are not indexed by GitNexus. They were read directly, and the graph was used only for the Python and C++ tooling and test helpers.
- The GitNexus BM25 index failed to build. Each `query` took about 25 s and returned mostly third-party hits, and the top communities and processes are third-party (SDL/Tracy/hidapi), so graph-driven discovery of first-party CI flows was limited.
- Branch-protection rules and required status checks on `main` could not be verified (no network or GitHub API access). CLAUDE.md states that `main` is protected and PRs must pass CI.
- Workflow run history, durations, cache hit rates and flake rates are not visible. CICD-007 and CICD-009 impact estimates are inferred from configuration.
- Nothing was built or run (read-only analysis). For example, whether an `INTERI_SHIPPING` build would compile today is unknown.
- `modules/` (separate repos) and private console-backend repos were out of scope. No console CI could be assessed.

## Cross-discipline notes
- Unit-Tests: tests that `GTEST_SKIP` when `!interi::kAssertsEnabled` (5 test files) have never executed their skip path. See CICD-002.
- Security: the asset importers (glTF/FBX/OBJ/images) are the main untrusted-input surface, and their fuzzing budget is minimal. See CICD-015.
- Open-Source: vcpkg already emits SPDX documents per port, so an SBOM could be attached to releases (CICD-010). The license gates themselves are healthy.
- Performance: `tools/bench_compare` has no baseline and no consumer (CICD-011).
