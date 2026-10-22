# Open-Source-Validation — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Nothing here has been applied. These are instructions, not changes.
*Engineering guidance, not legal advice.*

## Recommended order
1. OSS-002, OSS-003: fix the notices content first (the generator and policy), so that what OSS-001 installs is complete and correct.
2. OSS-001: install `LICENSE` and `THIRD_PARTY_NOTICES.md` with every `cmake --install`. This is the distribution obligation.
3. OSS-004: widen the SPDX gate (one line; protects the current clean state).
4. OSS-005, OSS-006: add an SBOM and REUSE metadata (they share the SPDX tooling).
5. OSS-007: settle copyright-holder wording and the contribution terms before outside contributions arrive.

OSS-008 to OSS-011 are Info and need no action. Optional notes for OSS-008 are at the end.

## OSS-001 — `cmake --install` ships binaries without LICENSE or notices
- **Severity / Effort:** Medium / S
- **Depends on:** OSS-002, OSS-003 (so the installed notices are complete)
- **Steps:**
  1. In `cmake/InteriPackage.cmake`, install `LICENSE` and `THIRD_PARTY_NOTICES.md` to `${CMAKE_INSTALL_DATADIR}/doc/interi` (`share/doc/interi`, the GNU convention) *before* the `if(NOT INTERI_BUILD_SHARED) return()` guard (lines 6-8), with `include(GNUInstallDirs)` moved above it, so the two files install for both shared and static builds (and any install rule a static build gains later carries them).
  2. Do the same for any future game or editor packaging target (`level_editor` is not installed today, but a release zip of it would need the same two files next to the executable).
  3. Mention the installed location in `README.md` § Licensing.
- **Illustrative change (not applied):**
  ```cmake
  install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md
      DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/interi)
  ```
- **Verification:** Extend the `Package_*` integration tests (which already run `cmake --install`) to assert that `<prefix>/share/doc/interi/LICENSE` and `THIRD_PARTY_NOTICES.md` exist and match the source tree.
- **Risks / trade-offs:** None. A static engine (`INTERI_BUILD_SHARED=OFF`) still returns early for the libraries and CMake package, but now installs the license and notices, so AC-1 holds for every preset. The `Package_*` tests cover the shared presets; a static-preset install check (or a configure-time assertion that the rule sits above the guard) covers the other case.

## OSS-002 — Notices miss bundled sub-components (Tracy's libbacktrace, LZ4, rpmalloc; ImGui fonts)
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Add a reviewed `extra_notices` section to `tools/license_check/license_policy.json`, keyed by port. Each entry names a component, its SPDX id and a path to the license text inside the port's installed or source files, or a short verbatim text stored under `tools/license_check/extra_notices/<port>-<component>.txt`. Seed it with:
     - `tracy`: libbacktrace (BSD-3-Clause, "Copyright (C) 2012-2016 Free Software Foundation, Inc."), LZ4 (BSD-2-Clause, Yann Collet), rpmalloc (check the header of `public/client/tracy_rpmalloc.cpp`; upstream rpmalloc is public domain or MIT).
     - `imgui`: ProggyClean (MIT, Tristan Grimmer, 2004–2005) and ProggyForever (MIT, Disco Hello 2026, Tristan Grimmer 2019/2023).
  2. Have `gen_notices.py` append those texts under the port's section ("Bundled components").
  3. Record a "last reviewed at version X" field for each entry, so a port version bump fails a test until someone re-reviews (same mechanism as OSS-003).
  4. Before adding entries, grep the other ports' installed headers once more for bundled licenses (SDL3 already ships hidapi and others inside its copyright file, so it is fine).
- **Illustrative change (not applied):**
  ```json
  "extra_notices": {
    "tracy": [{"component": "libbacktrace", "license": "BSD-3-Clause", "text": "extra_notices/tracy-libbacktrace.txt", "reviewed_version": "0.14.1#1"}],
    "imgui": [{"component": "ProggyClean font", "license": "MIT", "text": "extra_notices/imgui-proggyclean.txt", "reviewed_version": "1.92.9"}]
  }
  ```
- **Verification:**
  - A unit test in `test_license_tools.py` checks that an `extra_notices` entry appears in the generated output.
  - `ThirdPartyNotices_UpToDate` passes on Linux and Windows.
  - `grep -n 'Free Software Foundation\|Yann Collet\|Tristan Grimmer' THIRD_PARTY_NOTICES.md` finds each one.
- **Risks / trade-offs:** The list is manual, so it goes stale on upgrades, which is why step 3 matters. The alternative is a ScanCode run over `buildtrees` per port: more complete, but heavier and noisy. Another option is to set `IMGUI_DISABLE_DEFAULT_FONT` and ship an OFL or MIT font with its own notice; that is more work for no license benefit.

## OSS-003 — Overrides are not version-bound and not applied to the notices
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `"version": "<port version>"` to each entry in `license_policy.json` `overrides`: `stb` → `2026-08-02`, `directx-dxc` → `2026-05-27`, after re-reviewing DXC at that version (the reason text cites v1.9.2602.24).
  2. In `check_licenses.find_violations`, apply an override only when the installed port version (from the SPDX document name `<port>:<triplet>@<version>`) matches. Otherwise report "override needs re-review for version X".
  3. In `gen_notices.generate_notices`, print `License: MIT (concluded by Interi review; vcpkg metadata: LicenseRef-vcpkg-null)` for overridden ports. Load the policy overrides as it already loads `build_tools`.
  4. Regenerate `THIRD_PARTY_NOTICES.md`.
- **Illustrative change (not applied):**
  ```python
  expr = license_expr
  if port in overrides and overrides[port].get("version") == version:
      expr = f"{overrides[port]['license']} (reviewed override; vcpkg metadata: {license_expr})"
  ```
- **Verification:** New unit tests: a version mismatch produces a violation, and a matching override shows the concluded license in the notices. Then `ctest -L license` passes.
- **Risks / trade-offs:** Every baseline bump that moves stb or dxc needs a one-line policy update. That is the intended friction.

## OSS-004 — SPDX header gate skips `benchmarks/` and `samples/`
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `tools/license_check/check_spdx_headers.py:14`, set `DEFAULT_DIRS = ["benchmarks", "cmake", "engine", "samples", "testing", "tests", "tools"]` (add the two, drop the non-existent `game`). Better still, walk `git ls-files` so new top-level folders are covered automatically, excluding `external/`.
  2. Add a unit test that a header-less file in `samples/` is reported.
- **Verification:** `python3 tools/license_check/check_spdx_headers.py` exits 0 today. The manual scan showed every tracked source file already has the header, so no source changes are needed.
- **Risks / trade-offs:** Generated files under `build/` must stay excluded; scanning `git ls-files` handles that.

## OSS-005 — No SBOM
- **Severity / Effort:** Low / M
- **Depends on:** OSS-003 (concluded licenses for overridden ports)
- **Steps:**
  1. Add `tools/license_check/gen_sbom.py`. It merges `vcpkg_installed/<triplet>/share/*/vcpkg.spdx.json` into one SPDX 2.3 JSON document. Add a root package `interi` (MIT, version from `vcpkg.json`) with `DEPENDS_ON` relationships, skip vcpkg helper ports, and mark `build_tools` as `BUILD_TOOL_OF`.
  2. Run it in CI per platform and upload `interi-<triplet>.spdx.json` as an artifact; attach it to releases.
  3. Optionally, validate the result with an SPDX validator in CI.
- **Verification:** The CI artifact exists. It has one package per non-helper port, with versions matching the installed SPDX documents and a license on each.
- **Risks / trade-offs:** vcpkg's per-port SPDX documents already carry download hashes, so the merge is cheap. CycloneDX is an equivalent alternative.

## OSS-006 — Not REUSE-compliant
- **Severity / Effort:** Low / S
- **Depends on:** OSS-007 (holder wording)
- **Steps:**
  1. Add `LICENSES/MIT.txt` (keep the root `LICENSE` for GitHub's detector).
  2. Add `REUSE.toml` with annotations:
     - `tests/render/golden/**`, `tests/asset_import/fuzz/corpus/**`, `samples/**/*.json`, `docs/**`, `**/*.md`, `.github/**`, `data/**` → `SPDX-License-Identifier: MIT`, `SPDX-FileCopyrightText: The Interi Authors`.
     - `THIRD_PARTY_NOTICES.md` → annotated as an aggregate of third-party texts.
     - `external/vcpkg` (submodule) → excluded or annotated MIT, Microsoft.
  3. Optionally, add a `reuse lint` CI step.
- **Verification:** `reuse lint` reports compliance.
- **Risks / trade-offs:** None. Annotating via `REUSE.toml` avoids touching binary assets.

## OSS-007 — Inconsistent copyright holder; no contribution terms
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Pick one holder string, "Copyright (c) 2026 The Interi Authors" (matches `LICENSE:3`), and use it in every location:
     - `LICENSE:3` (already correct; keep it).
     - `samples/modules/starter/module.json:8` (today "Interi contributors").
     - `tools/new_module/new_module.py:57`: make the in-tree default the same holder string (today it is `{year} {args.author}`); keep an explicit option for out-of-tree authors to set their own.
     - The plugin-stub fixtures `tests/editor/plugin_stubs/{event,hook}/module.json` (today "Copyright (c) Interi", built from the stub author by `engine/editor/src/PluginAuthoring.cpp:255`): give the fixture inputs the same holder, then regenerate the stubs with `INTERI_UPDATE_GOLDEN=1`.
  2. Add a `CONTRIBUTING.md` stating inbound = outbound MIT (optionally with a DCO sign-off requirement). It is required, not optional.
  3. Define "The Interi Authors": add an `AUTHORS` file, or state in `CONTRIBUTING.md` (and `README.md` § Licensing) that it means the contributors recorded in the git history. One of the two is required.
- **Verification:** `git grep -n -i 'copyright (c)' -- LICENSE samples tools tests/editor/plugin_stubs engine/editor` shows the one holder string in every location above (the `PluginAuthoring.cpp` format string excepted, whose output the fixtures check); `CONTRIBUTING.md` exists, and either `AUTHORS` exists or the git-history definition appears in `CONTRIBUTING.md`; `PluginStub_*_LoadsOverStarter` and `new_module.py`'s tests still pass.
- **Risks / trade-offs:** Other test data under `tests/` (e.g. `tests/sdk/modules`) can keep dummy values; they are data, not claims.

## Optional notes for Info items
- **OSS-008:** if the notices size becomes a problem, have `gen_notices.py` keep only license blocks for ports whose copyright file embeds source (meshoptimizer), via a policy `notice_text` override. Optionally drop test-only ports (benchmark, gtest) by listing them as `test_only`.
- **OSS-009:** before publishing `modules/physics` or `modules/destruction`, give each a `LICENSE` and a generated notices file (Jolt is MIT).
- **OSS-010:** if a D3D12 backend ever emits DXIL or ships `dxil.dll`, re-review the Microsoft redistribution terms before moving `directx-dxc` out of `build_tools`.
- **OSS-011:** nothing to fix. Keep the guardrails listed in Goal.md.
