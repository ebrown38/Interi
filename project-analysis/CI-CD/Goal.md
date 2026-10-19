# CI-CD — Goal State

## Vision
Every push to a working branch (`main`, `milestone-*`, `autoflow/*`) and every PR builds and tests every supported configuration. That covers Linux Clang debug, release, shipping, ASan, TSan, tidy, profile, scalar-SIMD and static, plus Windows MSVC debug, release, shipping and profile with real Vulkan execution. Each change runs exactly once, and a single required check blocks merge on any failure. The pipeline is pinned, least-privilege, cached and parallel, so a warm PR run finishes in well under an hour and a hang fails fast with a named test. Tagged releases are produced by CI from a pinned toolchain, with checksums, symbols, an SBOM and provenance, and downstream module projects learn about API breaks automatically.

## Acceptance criteria
- [ ] AC-1: The PR/push matrix includes `linux-clang-tsan`, and a PR with a deliberate data race in a stress test cannot be merged. (resolves CICD-001)
- [ ] AC-2: Shipping presets (`INTERI_SHIPPING` defined, `CMAKE_BUILD_TYPE=Release`) exist for Linux and Windows and are built and tested on every PR. `INTERI_SIMD=OFF` and `INTERI_BUILD_SHARED=OFF` legs are built at least nightly. (resolves CICD-002)
- [ ] AC-3: The Windows CI log shows the `VulkanTest`-based suites executing (0 skipped) with `INTERI_REQUIRE_GPU=1` set, or a documented, reviewed exception lists exactly which suites are Linux-only. (resolves CICD-003)
- [ ] AC-4: The `concurrency.group` includes the event name, and runs on `main` are never cancelled. A same-repo working-branch commit with an open PR triggers exactly one full CI run, and pushes to `milestone-*` and `autoflow/*` branches still trigger CI. (resolves CICD-004)
- [ ] AC-5: `grep -nE 'uses: [^@]+@(v[0-9]+|main|master)\b' .github/workflows/*.yml` returns nothing, the container image is referenced by `@sha256:` digest, `.github/dependabot.yml` covers `github-actions`, and every checkout sets `persist-credentials: false`. (resolves CICD-005)
- [ ] AC-6: The Clang, clang-format and MSVC toolset versions used in CI are pinned in a tracked file, and each job fails if the installed version differs. (resolves CICD-006)
- [ ] AC-7: Cache keys include the OS, triplet, compiler version and `cmake/ports/**`, and new cache entries are saved on every run. The second consecutive nightly TSan run builds zero vcpkg ports, and ccache or sccache reports a hit rate above 80 % on a no-op PR. (resolves CICD-007)
- [ ] AC-8: Every job in every workflow has `timeout-minutes`, and `test-base` sets a per-test `timeout`. (resolves CICD-008)
- [ ] AC-9: Every CI `ctest` call runs in parallel (`--parallel` or an integer `execution.jobs` in `test-base`), and 10 consecutive green `main` runs show no new flakes. (resolves CICD-009)
- [ ] AC-10: Pushing a `v*` tag produces a GitHub Release with Linux and Windows install archives, `SHA256SUMS`, debug-symbol archives, an SBOM, `THIRD_PARTY_NOTICES.md`, the toolchain fingerprint and a build-provenance attestation. The version has a single source that matches the tag. (resolves CICD-010)
- [ ] AC-11: `compare_benchmarks.py` has a CTest, and the nightly run publishes a benchmark comparison against a baseline. (resolves CICD-011)
- [ ] AC-12: The coverage job fails below a tracked minimum line-coverage floor and writes its summary to the job summary. Uploaded artifacts set `retention-days`. (resolves CICD-012)
- [ ] AC-13: No blocking CI job filters tests with a `-L` allowlist (only `-LE pending` or the preset exclusion). `interi_add_module_load_test` sets a default label, and a test enforces that every CTest test has at least one label. (resolves CICD-013)
- [ ] AC-14: CodeQL (C++ and Python), `ruff` on `tools/`, and `actionlint`/`zizmor` on workflows run in CI and fail on new findings. (resolves CICD-014)
- [ ] AC-15: The nightly run fuzzes the importers for at least 15 minutes with a non-fixed seed and uploads crash inputs on failure. (resolves CICD-015)
- [ ] AC-16: Shared setup lives in one composite action, CTest JUnit results are published, and a single aggregate `ci-ok` job is the only required status check. (resolves CICD-016)
- [ ] AC-17: A change to `engine/hotreload/include/interi/hotreload/ModuleApi.h` on `main` automatically triggers a build of `modules/physics` and `modules/destruction` against the new engine, and its result is visible to the maintainer. (resolves CICD-017)
- [ ] AC-18: The strengths in CICD-018 still hold: `permissions: contents: read` at workflow level (write scopes only on the release job), no `pull_request_target`, `INTERI_REQUIRE_GPU=1` on Linux, the pending-test gate on PRs and `main`, license and SPDX gates on both OSes, and DXC shader compilation in the build. (preserves CICD-018)

## Guardrails to keep it that way
- An aggregate `ci-ok` required check, so new matrix legs are gated automatically.
- `zizmor` and `actionlint` in `lint` reject unpinned actions, missing `permissions`, missing timeouts and template injection.
- Dependabot PRs for action, image and toolchain pins. Toolchain bumps are explicit PRs that also refresh golden images and the clang-format baseline.
- A CTest that fails when any test lacks a label (alongside the existing `Ci_PendingLabelFilter_ExcludesSample`).
- A CTest that asserts `project(VERSION)` equals `vcpkg.json`'s `version`.
- A review checklist item: any new CMake option with an OFF/ON mode documented as supported gets a CI leg.

## Out of scope
- GCC, clang-cl and macOS builds: the project claims only Linux Clang and Windows MSVC.
- PS5 and Xbox CI: those backends live in private repos with NDA SDKs and need their own isolated self-hosted runners.
- Fixing the defects the new gates will surface (races, shipping-build errors, coverage gaps): those belong to the owning disciplines.
- End-user installers (MSI, AppImage, Steam depots): Interi is an engine SDK, and archives plus the CMake package are sufficient until a game ships.
