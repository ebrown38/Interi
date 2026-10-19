# CI-CD — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.

## Recommended order
1. **CICD-016 (composite setup action), CICD-008 (timeouts), CICD-005 (pinning):** cheap structural fixes. With one shared setup action, every later cache or toolchain change is made once.
2. **CICD-006, CICD-007 (toolchain pin + cache keys + ccache):** make builds reproducible and fast before adding more jobs.
3. **CICD-009 (parallel CTest):** frees the time budget needed for the next step.
4. **CICD-001, CICD-002 (TSan on PRs, shipping/scalar/static legs):** the two High gaps. They are affordable once 2 and 3 are done.
5. **CICD-004 (concurrency and triggers), CICD-013 (label allowlist):** make the gates trustworthy.
6. **CICD-003 (Windows GPU), CICD-010 (release pipeline):** larger platform and delivery work.
7. **CICD-011, CICD-012, CICD-014, CICD-015, CICD-017:** incremental hardening.

## CICD-001 — ThreadSanitizer is not a merge gate
- **Severity / Effort:** High / M
- **Depends on:** CICD-007 (the TSan triplet cache), CICD-009 (parallel CTest keeps the leg fast)
- **Steps:**
  1. Add a `linux-clang-tsan` leg to the push/PR matrix in `.github/workflows/ci.yml:48`. If its cost is too high on every feature-branch push, run it on `pull_request` and pushes to `main` only, via a separate job or an `if:` on the leg.
  2. Give the TSan leg its own cache key that includes the triplet (`x64-linux-tsan`). See CICD-007.
  3. Keep the nightly job for the full unfiltered run, plus anything too slow for PRs (long fuzzing, high `INTERI_FUZZ_ITERATIONS`).
  4. Add the TSan leg to the required checks (or to the aggregate job from CICD-016).
- **Illustrative change (not applied):**
  ```yaml
  linux:
    strategy:
      matrix:
        preset: [linux-clang-debug, linux-clang-release, linux-clang-asan, linux-clang-tsan,
                 linux-clang-tidy, linux-clang-profile]
  ```
- **Verification:** Open a PR that introduces a deliberate data race in a scratch test, such as an unsynchronised counter in `interi_jobs_stress_tests`. The PR's TSan check must fail and block merge.
- **Risks / trade-offs:** About 1.5–2x wall time for that leg, and occasional TSan false positives from uninstrumented code. The overlay triplet and `VulkanTestSupport.cpp` suppressions already handle the known ones.

## CICD-002 — Shipping, scalar-SIMD and static-engine configurations are never compiled in CI
- **Severity / Effort:** High / M
- **Depends on:** CICD-007 (caching)
- **Steps:**
  1. Add a `linux-clang-shipping` and a `windows-msvc-shipping` configure, build and test preset. Use `CMAKE_BUILD_TYPE=Release` and define `INTERI_SHIPPING`, ideally through a CMake option (`INTERI_SHIPPING=ON` → `add_compile_definitions(INTERI_SHIPPING)` and into `INTERI_ABI_DEFINITIONS` so the toolchain fingerprint and `interi::build_settings` carry it).
  2. Add a `linux-clang-scalar` preset with `INTERI_SIMD=OFF` and a `linux-clang-static` preset with `INTERI_BUILD_SHARED=OFF`. Both can be build-plus-unit only.
  3. Put the shipping legs on both compilers in the PR matrix as a gate, because `/WX` on unused-variable-after-assert-removal is MSVC-specific. Scalar and static can run on PR or nightly.
  4. Run the tests in the shipping leg, so the `GTEST_SKIP` paths when `!kAssertsEnabled` are exercised.
- **Illustrative change (not applied):**
  ```json
  { "name": "linux-clang-shipping", "inherits": "linux-clang", "displayName": "Linux Clang Shipping",
    "cacheVariables": { "CMAKE_BUILD_TYPE": "Release", "INTERI_SHIPPING": "ON" } }
  ```
- **Verification:** `cmake --preset linux-clang-shipping && cmake --build --preset linux-clang-shipping && ctest --preset linux-clang-shipping` is green in CI on both OSes. A deliberately assert-only variable makes that leg fail.
- **Risks / trade-offs:** The first run will likely surface real build breaks, which is the point. The scalar path must keep the bit-exact parity tests (`SimdParityTests`) green.

## CICD-003 — Windows never executes the Vulkan RHI, GPU or golden-image tests
- **Severity / Effort:** Medium / L
- **Depends on:** CICD-016, CICD-005
- **Steps:**
  1. Option A (preferred, hosted): install Mesa lavapipe for Windows in the job (for example, the `mesa-dist-win` release ZIP, verified by SHA-256 and cached). Point `VK_DRIVER_FILES` at its `lvp_icd.x86_64.json`, install the Vulkan validation layers (vcpkg `vulkan-validationlayers` or the LunarG runtime, checksum-verified), and set `INTERI_REQUIRE_GPU: 1`.
  2. Option B: a self-hosted Windows runner with a real GPU, restricted to `push` on `main` and PRs from trusted authors (never fork PRs).
  3. Upload `build/<preset>/golden-output` on failure, as the Linux job does.
  4. Golden images are generated on Linux lavapipe, so confirm Windows lavapipe agrees within `GoldenTolerance`. If it doesn't, keep goldens Linux-only but still run the Vulkan `integration` tests on Windows.
- **Verification:** The Windows job log shows the `VulkanTest` suites executing (non-zero passed, zero skipped). Removing the driver makes the job fail because of `INTERI_REQUIRE_GPU`.
- **Risks / trade-offs:** The Windows Mesa build is third-party binary input and must be pinned and hashed. A self-hosted runner needs isolation.

## CICD-004 — Concurrency group cancels nightly runs and doubles PR-branch runs
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add the event name to the concurrency group, so a schedule run and a push to `main` never share a group, and never cancel in-progress runs on `main`.
  2. Remove the push/PR double run without losing push CI on working branches. The project pushes one commit per phase "so CI checks MSVC early" on `milestone-N` and `autoflow/*` branches. Choose one of:
     - (a) Keep `push` on all branches and add a cheap guard job that skips the `pull_request` run when the head repo is this repo (the push run already covers it). Fork PRs still run.
     - (b) Restrict `push` to `main` and rely on `pull_request`, with working branches opened as draft PRs from their first commit.
     Do **not** narrow `push` to `milestone-*` only, which would drop CI on the `autoflow/*` branches in use today.
  3. Add `if: github.event_name != 'schedule'` to `lint`, `linux`, `windows` and `coverage` if the nightly should run only sanitizers, or document that it intentionally re-runs everything.
- **Illustrative change (not applied):**
  ```yaml
  concurrency:
    group: ${{ github.workflow }}-${{ github.event_name }}-${{ github.ref }}
    cancel-in-progress: ${{ github.ref != 'refs/heads/main' }}
  jobs:
    linux:
      # option (a): same-repo PRs are already covered by the branch push run
      if: github.event_name != 'pull_request' || github.event.pull_request.head.repo.full_name != github.repository
  ```
- **Verification:** Merging a PR during a nightly run leaves the nightly run alive. A same-repo working-branch commit with an open PR shows exactly one full CI run. Pushes to `autoflow/*` and `milestone-*` branches still trigger CI.
- **Risks / trade-offs:** With option (a), required checks must be satisfied by the push run's statuses on the PR head SHA. GitHub attaches them to the commit, so this works, but confirm it in branch protection. Option (b) changes the working habit.

## CICD-005 — Actions and container images use floating tags
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Pin every `uses:` to a full commit SHA with the tag as a comment (`actions/checkout`, `actions/cache`, `actions/upload-artifact`, `ilammy/msvc-dev-cmd`).
  2. Pin `fedora:44` by digest (`fedora:44@sha256:...`).
  3. Add `.github/dependabot.yml` with the `github-actions` ecosystem (and `docker` for the container) so pins are bumped by reviewed PRs.
  4. Set `persist-credentials: false` on every `actions/checkout`.
  5. Replace `safe.directory '*'` with `"$GITHUB_WORKSPACE"` and `"$GITHUB_WORKSPACE/external/vcpkg"`.
  6. Enforce this with `zizmor` or `actionlint` (see CICD-014).
- **Illustrative change (not applied):**
  ```yaml
  - uses: actions/checkout@<40-hex-sha> # v7.x.y
    with: { submodules: true, persist-credentials: false }
  ```
- **Verification:** `grep -nE 'uses: [^@]+@(v[0-9]|main|master)' .github/workflows/*.yml` returns nothing, and Dependabot opens update PRs.
- **Risks / trade-offs:** More update PRs to review, but small ones.

## CICD-006 — Toolchain versions float
- **Severity / Effort:** Medium / M
- **Depends on:** CICD-005 (image digest)
- **Steps:**
  1. Build and publish a CI image (for example, `ghcr.io/ebrown38/interi-ci:<date>`) from a tracked `ci/Dockerfile` that installs `FEDORA_PACKAGES` with explicit versions. Reference it by digest. This also removes the `dnf install` step from every job, which saves minutes.
  2. Alternatively, keep `fedora:44@sha256:...` and install explicit package NEVRAs, or use `dnf versionlock`.
  3. Print `clang --version`, `clang-format --version` and the `cl` banner in each job, and fail if the major.minor differs from a value tracked in the repo (for example, `ci/toolchain.env`).
  4. Pin the Windows MSVC toolset with `ilammy/msvc-dev-cmd`'s `toolset:` input so `_MSC_VER` (part of the module fingerprint) is stable.
- **Verification:** Two CI runs a week apart report identical compiler versions. A clang-format-only diff never appears on an untouched PR.
- **Risks / trade-offs:** Toolchain upgrades become explicit PRs, which is intended. The image must be rebuilt for security updates, and publishing it needs `packages: write` on a separate image-build workflow only.

## CICD-007 — vcpkg cache key misses and stale hits, no compiler cache
- **Severity / Effort:** Medium / M
- **Depends on:** CICD-016 (do it once in the composite action), CICD-006 (the compiler version is known)
- **Steps:**
  1. Include the triplet, overlay ports and compiler version in the key: `vcpkg-<os>-<triplet>-<compiler-ver>-<vcpkg commit>-hashFiles('vcpkg.json','cmake/triplets/**','cmake/ports/**')`. The TSan leg then gets its own key.
  2. **Primary fix:** split `actions/cache` into `actions/cache/restore` plus `actions/cache/save` with a run-unique key suffix (`-${{ github.run_id }}`) and a prefix `restore-keys`. New ABI entries are then always saved, and GitHub's LRU eviction prunes old ones.
  3. Add ccache (Linux) and sccache or ccache (MSVC with `/Z7`) via `CMAKE_CXX_COMPILER_LAUNCHER`, cached per preset in the same restore/save pattern.
  4. Alternative to step 2 (not preferred): vcpkg's NuGet binary source on GitHub Packages, which is content-addressed per package ABI. See Risks.
- **Illustrative change (not applied):**
  ```yaml
  - uses: actions/cache/restore@<sha>
    with:
      path: .vcpkg-cache
      key: vcpkg-${{ runner.os }}-${{ matrix.triplet }}-${{ steps.tc.outputs.clang }}-${{ steps.vcpkg.outputs.commit }}-${{ hashFiles('vcpkg.json','cmake/triplets/**','cmake/ports/**') }}-${{ github.run_id }}
      restore-keys: vcpkg-${{ runner.os }}-${{ matrix.triplet }}-${{ steps.tc.outputs.clang }}-
  # ... build ...
  - uses: actions/cache/save@<sha>
    if: always()
    with: { path: .vcpkg-cache, key: <same key> }
  ```
- **Verification:** The second nightly TSan run logs vcpkg restoring every package with zero port builds. A no-op PR's compile step shows a high ccache hit rate.
- **Risks / trade-offs:**
  - Run-unique keys create more cache entries and more pressure on the 10 GB limit. Rely on LRU, or save only from `main`.
  - The NuGet/GitHub Packages alternative needs `packages: write` and a token in CI. That conflicts with the `contents: read` least-privilege baseline (CICD-018, AC-18), and fork PRs can't write to it. Use it only from a trusted `main` job if at all.

## CICD-008 — No job has `timeout-minutes`
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Set `timeout-minutes` per job, sized from recent cold-cache run durations plus margin. A starting point: lint 10, linux 90, windows 120, coverage 90, nightly 180.
  2. Add a per-test `timeout` in `test-base` (`"execution": { "timeout": 600 }`) and longer `TIMEOUT` properties on the stress executables, so a hang surfaces as a named test failure well before the 1500 s default.
- **Verification:** Every job in `ci.yml` has `timeout-minutes`. An injected `sleep` in a test fails at the CTest timeout with the test's name.
- **Risks / trade-offs:** Timeouts that are too tight cause flaky cancellations on cold-cache runs. Size them on cold runs.

## CICD-009 — CTest runs tests serially
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Pass `--parallel` to every `ctest` call in `ci.yml` (with no value, CMake 3.29+ uses the number of CPUs), or set an explicit integer `"execution": { "jobs": 4 }` in `test-base` in `CMakePresets.json`.
  2. Audit tests that share files or ports. `RESOURCE_LOCK module_shadow_copy` already covers module shadow copies. Add `RESOURCE_LOCK gpu` to lavapipe suites if they contend, and `RUN_SERIAL` to thread-heavy stress executables if TSan timing becomes unreliable.
- **Verification:** The CI test phase wall time drops substantially, and 10 consecutive runs show no new flakes.
- **Risks / trade-offs:** Hidden shared-state tests surface as flakes. Fix them rather than reverting.

## CICD-010 — No release, versioning or packaging pipeline
- **Severity / Effort:** Medium / L
- **Depends on:** CICD-006 (reproducible toolchain), CICD-002 (shipping config), CICD-005
- **Steps:**
  1. Make one version source. Read `project(VERSION)` from a tracked `VERSION` file, or derive it from the tag in CI, and add a CTest asserting `vcpkg.json`'s `version` matches.
  2. Add `.github/workflows/release.yml` on `push: tags: ['v*']`. It builds `linux-clang-release` and `windows-msvc-release` (and shipping once CICD-002 exists), runs the full test set, then runs `cmake --install` into a staging prefix and archives it with `cpack` or `tar`/`zip`.
  3. Attach to the GitHub Release: the archives, `SHA256SUMS`, the toolchain fingerprint string, `THIRD_PARTY_NOTICES.md`, and an SBOM assembled from vcpkg's per-port SPDX files.
  4. Archive debug symbols (`.pdb`, and split DWARF or `.debug` via `objcopy --only-keep-debug`) as a separate asset.
  5. Add build provenance (`actions/attest-build-provenance`, pinned). Use Authenticode for Windows DLLs only if binaries go to end users. Grant `contents: write`, `id-token: write` and `attestations: write` on this job only.
  6. Generate release notes from PR titles (`gh release create --generate-notes`) or a CHANGELOG.
- **Verification:** Pushing a `v0.1.1` tag produces a Release with Linux and Windows archives, checksums, symbols and an attestation. A `Package_Starter_BuildsOutOfTree`-style build against the downloaded archive succeeds.
- **Risks / trade-offs:** Write permissions appear for the first time, so keep them confined to the tag-triggered job. The binaries are consumable only with the identical toolchain, so publish the fingerprint prominently.

## CICD-011 — Benchmark regression tooling not wired into CI
- **Severity / Effort:** Low / M
- **Depends on:** CICD-006 (stable toolchain), and preferably a consistent runner
- **Steps:**
  1. Add a CTest for `compare_benchmarks.py` (exit codes 0, 1 and 2 on small fixture JSON files), as `license_check` and `new_module` already have.
  2. In the nightly job, run the release-preset benchmark executables with `--benchmark_repetitions=5 --benchmark_out=<file>.json` and compare against a baseline. Store it as an artifact from the last `main` run, or as a tracked `benchmarks/baseline/*.json` updated by PR. Start with `--report-only`.
  3. Turn it into a failing gate only for benchmarks known to be stable on hosted runners. Otherwise, use a self-hosted performance runner.
- **Verification:** The nightly job summary shows a benchmark comparison table, and a synthetic 2x slowdown is flagged.
- **Risks / trade-offs:** Hosted-runner noise causes false alarms. Use medians and generous thresholds.

## CICD-012 — Coverage not gated; no artifact retention
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Parse `llvm-cov report`'s TOTAL line (or `llvm-cov export -summary-only`) and fail under a tracked floor set slightly below today's value. Ratchet it up over time.
  2. Write the summary to `$GITHUB_STEP_SUMMARY`.
  3. Set `retention-days` (for example, 14) on `coverage-html` and the golden-output artifacts.
- **Verification:** Deleting a tested module's tests in a scratch PR fails the coverage job.
- **Risks / trade-offs:** A global floor is coarse. Per-directory floors are better but cost more upkeep.

## CICD-013 — Label allowlist silently skips unlabelled tests
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Replace `-L "unit|stress|benchmark|integration|golden"` with no `-L` (the presets already exclude `pending`), or with `-LE pending`.
  2. Make `interi_add_module_load_test` set a default label (`integration`) in `cmake/InteriGameplayModule.cmake`.
  3. Optionally, add a CTest (like `Ci_PendingLabelFilter_ExcludesSample`) that parses `ctest --show-only=json-v1` and fails on any test without a label.
- **Verification:** A scratch unlabelled `add_test` shows up in the Windows and Linux PR job logs, or the label guard test fails.
- **Risks / trade-offs:** None significant.

## CICD-014 — Static-analysis gaps
- **Severity / Effort:** Low / M
- **Depends on:** CICD-005
- **Steps:**
  1. Add CodeQL (`github/codeql-action`, pinned) for `c-cpp` (manual build mode, reusing the debug preset) and `python`. This needs `security-events: write` on that job only.
  2. Add `ruff check` and `ruff format --check` on `tools/**/*.py` to the `lint` job.
  3. Add `actionlint` and `zizmor` to `lint`.
  4. Optionally, add a nightly MSVC `/analyze` build, reported but not gating at first.
- **Verification:** The `lint` job fails on a deliberate Python lint error or an unpinned action. CodeQL alerts appear in the Security tab.
- **Risks / trade-offs:** CodeQL on a large C++ build adds a long job, so run it on `main` and weekly, not on every push.

## CICD-015 — Fuzzing is a 2,000-run smoke test only
- **Severity / Effort:** Low / S
- **Depends on:** CICD-004 (a nightly job that isn't cancelled)
- **Steps:**
  1. In the nightly ASan leg, run `interi_import_fuzzer` for a time budget (`-max_total_time=900`) with a random seed. Also run `interi_asset_import_stress_tests` with a high `INTERI_FUZZ_ITERATIONS`.
  2. Upload `crash-*` and `timeout-*` inputs as artifacts on failure.
  3. Consider ClusterFuzzLite or OSS-Fuzz, since the project is MIT open source.
- **Verification:** The nightly log shows libFuzzer's final `DONE` line with millions of executions, and a planted crash in a parser produces a crash artifact.
- **Risks / trade-offs:** Longer nightly runs. Findings need triage.

## CICD-016 — Workflow duplication, no test reporting, no aggregate check
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Extract `.github/actions/setup-interi/action.yml`, a composite action covering dnf or MSVC setup, the vcpkg commit id, the cache and bootstrap. Call it from every job.
  2. Pass `--output-junit build/<preset>/ctest.xml` to `ctest` and publish it (upload, plus a step summary or a pinned test-report action).
  3. Add a final `ci-ok` job with `needs: [lint, linux, windows, coverage]` and `if: always()` that fails unless every need succeeded. Make it the single required check.
- **Verification:** Adding a matrix leg needs no branch-protection change. The setup steps appear once.
- **Risks / trade-offs:** Composite actions can't declare `container:`, so keep the container on the job.

## CICD-017 — Engine changes not verified against the module projects
- **Severity / Effort:** Low / M
- **Depends on:** CICD-010 (an install artifact to build against)
- **Steps:**
  1. Upload the `cmake --install` prefix from the `linux-clang-debug` leg on `main` as an artifact.
  2. In the physics and destruction repos, add a workflow triggered by `repository_dispatch` (sent from Interi's `main`) or on a schedule. It fetches that artifact (or builds Interi at a given SHA) and runs their standalone build and tests.
  3. Alternatively, add a check that flags changes to `ModuleApi.h` and `PhysicsQueries.h` in a PR, so the author must confirm the modules were rebuilt.
- **Verification:** A breaking change to `engine/hotreload/include/interi/hotreload/ModuleApi.h` on Interi `main` turns the downstream module workflow red within a day.
- **Risks / trade-offs:** A cross-repo token or GitHub App is needed for `repository_dispatch`. Keep its scope to those repos and keep it out of PR-triggered jobs.
