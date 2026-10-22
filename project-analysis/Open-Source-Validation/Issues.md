# Open-Source-Validation — Issues

*This is an engineering license review, not legal advice.*

**Project:** Interi (repository root), branch `autoflow/003-goal-implement-milestone-21-8367f0` @ 4631f35
**Analyzed:** 2026-10-19
**Scope:** The project license (`LICENSE`, `README.md` § Licensing, SPDX headers). The vcpkg manifest (`vcpkg.json`), the `external/vcpkg` submodule, the overlay port `cmake/ports/ufbx`, triplets `cmake/triplets/*`, the license gates `tools/license_check/*` (policy, checker, notices generator, CTest wiring), `THIRD_PARTY_NOTICES.md` (19,295 lines). The installed port metadata `build/linux-clang-debug/vcpkg_installed/x64-linux/share/*/vcpkg.spdx.json` (24 ports) and vendored sources under `external/vcpkg/buildtrees/{tracy,imgui,sdl3,manifold,...}`. The install rules (`cmake/InteriPackage.cmake`), all 781 tracked files (binary assets, fuzz corpus, golden images, sample content, docs, CI). Literal searches for third-party banners and NDA/console SDK material in first-party code. A light look at the gitignored `modules/physics` and `modules/destruction`.
**Summary:** 11 issues — Critical: 0 · High: 0 · Medium: 2 · Low: 5 · Info: 4

Overall the project is in good shape for this discipline. The license is a clear MIT `LICENSE`, every tracked source and CMake file carries `SPDX-License-Identifier: MIT`, and every dependency is permissive. The allowlist checker, a GPL-injection negative test and the notices up-to-date test run in CI under the `unit` label. No GPL/LGPL/AGPL, proprietary or NDA material was found in the repository. The findings are about **attribution completeness in binary distributions** and hygiene.

## Inventory

Versions come from the installed vcpkg SPDX documents (`build/linux-clang-debug/vcpkg_installed/x64-linux/share/<port>/vcpkg.spdx.json`), pinned by `builtin-baseline` e456309 (`vcpkg.json:6`) and the submodule at the same commit.

| Component | Version/pin | Source (file:line) | License (SPDX) | Linkage | Compatible? | Notes |
|---|---|---|---|---|---|---|
| Interi (own) | 0.1.0 | `LICENSE:1-3`, `vcpkg.json:4` | MIT | — | — | Copyright "The Interi Authors" |
| vcpkg (submodule) | e456309 | `.gitmodules:1-3` | MIT | tool | Yes | Build-time package manager |
| benchmark | 1.9.5 | `vcpkg.json:8` | Apache-2.0 | static (bench exes only) | Yes | Test/bench only; listed in notices |
| cgltf | 1.15 | `vcpkg.json:9` | MIT | header (asset_import, static) | Yes | |
| directx-dxc | 2026-05-27 | `vcpkg.json:10-13` | NCSA (override; port metadata `LicenseRef-vcpkg-null`) | host tool | Yes | Package also holds proprietary libdxil, not linked or shipped (OSS-010) |
| gtest | 1.18.0 | `vcpkg.json:14` | BSD-3-Clause | static (tests only) | Yes | |
| imgui (docking) | 1.92.9 | `vcpkg.json:15-20` | MIT | static (editor) | Yes | Embeds ProggyClean/ProggyForever fonts, MIT, separate copyright (OSS-002) |
| manifold | 3.5.4 | `vcpkg.json:21` | Apache-2.0 | static (modeling, editor-only) | Yes | No upstream NOTICE file found |
| clipper2 (transitive, manifold) | 2.0.1 | installed share | BSL-1.0 | static | Yes | In notices |
| meshoptimizer | 1.3 | `vcpkg.json:22` | MIT | static (asset_import) | Yes | Its copyright file embeds fast_obj source (OSS-008) |
| miniaudio | 0.11.25 | `vcpkg.json:23` | Unlicense OR MIT-0 | header, compiled into shared `interi_audio` | Yes | `engine/audio/src/MiniaudioImplementation.cpp:4` |
| nlohmann-json | 3.12.0#2 | `vcpkg.json:24` | MIT | header | Yes | |
| recastnavigation | 1.6.0#1 | `vcpkg.json:25` | Zlib | static, private to shared `interi_nav` | Yes | `engine/nav/CMakeLists.txt:6` |
| sdl3 | 3.4.18 | `vcpkg.json:26-40` | Apache-2.0 AND BSD-3-Clause AND MIT AND Zlib | static (platform_sdl) | Yes | Bundled hidapi (BSD option) is covered by SDL's copyright file. X11/Wayland/dbus are system libraries loaded at runtime (OSS-011) |
| spirv-reflect | 1.4.357.0 | `vcpkg.json:41` | Apache-2.0 | static (rhi_vulkan) | Yes | |
| stb | 2026-08-02 | `vcpkg.json:42` | MIT OR Unlicense (override "MIT"; port metadata `LicenseRef-vcpkg-null`) | header (asset_import) | Yes | Notices show the placeholder (OSS-003) |
| tinyobjloader | 2.0.0rc13 | `vcpkg.json:43` | MIT | static (asset_import) | Yes | |
| tracy (on-demand) | 0.14.1#1 | `vcpkg.json:44-50` | BSD-3-Clause | static (Linux) / DLL (Windows), profile builds only | Yes | Bundles libbacktrace (BSD-3, FSF), LZ4 (BSD-2), rpmalloc. Not in notices (OSS-002) |
| pthreads (transitive, tracy) | 3.0.0#14 | installed share | Apache-2.0 | empty on POSIX | Yes | `!windows` only, empty package, correctly skipped |
| ufbx (overlay port) | 0.23.1, SHA512-pinned | `cmake/ports/ufbx/vcpkg.json:3-6`, `portfile.cmake:4-10` | MIT (upstream MIT or public domain) | static (asset_import) | Yes | |
| volk | 1.4.363.0 | `vcpkg.json:52` | MIT | static | Yes | Loads the system Vulkan loader at runtime |
| vulkan-headers | 1.4.363.0 | `vcpkg.json:53` | Apache-2.0 OR MIT | header | Yes | |
| vulkan-memory-allocator | 3.4.0 | `vcpkg.json:54` | MIT | header/static | Yes | |
| xxhash | 0.8.4 | `vcpkg.json:55` | BSD-2-Clause | private to shared `interi_core` | Yes | `engine/core/CMakeLists.txt:25` |
| Python tooling | stdlib only | `tools/**/*.py` | PSF (interpreter) | tool | Yes | No third-party Python packages imported |
| Test/fuzz assets | in-house | `tests/asset_import/fuzz/corpus/*`, `tests/render/golden/*.ppm` | MIT (implicit) | asset (tests) | Yes | Generated by `FuzzStressTests.cpp:203-210` and the renderer. No license metadata (OSS-006) |
| Sample content | in-house | `samples/modules/starter/**` | MIT (module.json) | asset | Yes | JSON only, no binary art |
| Fonts | ImGui default (embedded) | `external/vcpkg/buildtrees/imgui/.../imgui_draw.cpp:6373-6376, 6562-6565` | MIT | compiled into editor | Yes | No font files in the repo (OSS-002) |
| Jolt / Destruction modules | gitignored | `.gitignore:23`, `modules/*/module.json:7` | MIT (declared) | separate projects | Yes | Not distributed by this repo (OSS-009) |

## Findings

### OSS-001 — `cmake --install` ships binaries that contain third-party code, without LICENSE or notices
- **Severity:** Medium
- **Confidence:** High
- **Category:** Attribution / binary redistribution
- **Location:** `cmake/InteriPackage.cmake:32-42`, `cmake/InteriPackage.cmake:86-90`
- **Evidence:** The install step installs the shared engine libraries, `interi_module_check`, the static Vulkan-component libraries and, on Windows profile builds, the Tracy DLL (`install(IMPORTED_RUNTIME_ARTIFACTS Tracy::TracyClient ...)`, line 41). No rule installs `LICENSE` or `THIRD_PARTY_NOTICES.md`; a search of `CMakeLists.txt` and `cmake/*.cmake` finds no `install(FILES ... LICENSE|NOTICES)`. The installed binaries embed third-party code:
  - miniaudio is compiled into the shared `interi_audio` (`engine/audio/src/MiniaudioImplementation.cpp:4`).
  - Recast/Detour are linked into `interi_nav` (`engine/nav/CMakeLists.txt:6`).
  - xxHash is linked into `interi_core` (`engine/core/CMakeLists.txt:25`).
  - The static `interi_platform_sdl` and `interi_rhi_vulkan` archives need SDL3, volk, VMA and SPIRV-Reflect.
- **Impact:** MIT, BSD-2/3, Zlib (for altered or binary distributions this is a "should", but notices are customary) and Apache-2.0 §4(a) all require the copyright and license text to accompany binary redistributions. An SDK built with `cmake --install`, as documented in CLAUDE.md and README for module authors, carries no attribution. This includes Interi's own MIT notice, which the MIT license requires in "all copies or substantial portions".

### OSS-002 — THIRD_PARTY_NOTICES misses notices for components bundled inside ports (Tracy sub-libraries, ImGui's embedded fonts)
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Attribution completeness
- **Location:**
  - `tools/license_check/gen_notices.py:32-45`
  - `THIRD_PARTY_NOTICES.md:18885-18918` (tracy section), `THIRD_PARTY_NOTICES.md:294-321` (imgui section)
  - `external/vcpkg/buildtrees/tracy/src/v0.14.1-a7372d1a4f.clean/public/libbacktrace/LICENSE:1`, `.../public/common/tracy_lz4.hpp:1-12`, `.../public/client/tracy_rpmalloc.cpp`
  - `external/vcpkg/buildtrees/imgui/src/9b-docking-9ae2fb46a1.clean/imgui_draw.cpp:6373-6376`, `:6562-6565`
- **Evidence:** `generate_notices` copies only each port's `share/<port>/copyright` file:
  ```python
  copyright_file = share_dir / "copyright"
  ...
  parts.append(f"\n## {port}\n\nLicense: {license_expr or 'NOASSERTION'}\n\n```text\n{text}\n```\n")
  ```
  For Tracy that file is only Tracy's own BSD-3 text. The TracyClient compiles in libbacktrace ("Copyright (C) 2012-2016 Free Software Foundation", BSD-3 with a binary-reproduction clause), LZ4 ("Copyright (C) 2011-2020, Yann Collet", BSD-2) and rpmalloc. None of these is named in the notices.

  ImGui's copyright file is Omar Cornut's MIT only. The editor never loads a font (no `AddFont*` call in `engine/editor_ui` or `tools/editor`), so it uses the embedded ProggyClean ("MIT License / Copyright (c) 2004, 2005 Tristan Grimmer") / ProggyForever ("Copyright (c) 2026 Disco Hello, Copyright (c) 2019,2023 Tristan Grimmer") data. Those copyrights appear only in source comments.
- **Impact:** Binaries built with profiling (`*-profile` presets, plus the Windows Tracy DLL from OSS-001) and every `level_editor` build redistribute BSD/MIT code without the required copyright lines. The port-level `copyright` file is not a complete attribution source; vcpkg is upfront that it collects only the upstream top-level license. Confidence is Medium because whether the ImGui font is reached depends on runtime defaults (inferred from the absence of any font loading).

### OSS-003 — License overrides are not version-bound and are not applied to the notices (stb shows `LicenseRef-vcpkg-null`)
- **Severity:** Low
- **Confidence:** High
- **Category:** License metadata / policy
- **Location:** `tools/license_check/license_policy.json:18-27`, `tools/license_check/check_licenses.py:131-133`, `tools/license_check/gen_notices.py:44`, `THIRD_PARTY_NOTICES.md:18791-18793`
- **Evidence:** `find_violations` replaces the port's expression with `overrides[port]["license"]` whatever the port version. The dxc override's reason cites "v1.9.2602.24 ... reviewed 2026-10-13", while the installed port is `directx-dxc@2026-05-27`. The stb override was reviewed 2026-10-18 for `stb@2026-08-02`. `gen_notices.py` writes the raw `license_expr`, so the published notices say `License: LicenseRef-vcpkg-null` for stb even though the policy concluded MIT.
- **Impact:** A baseline bump that changed stb's or DXC's upstream license would pass the gate silently, because the override applies to any version. Readers of the notices see a non-SPDX placeholder instead of the concluded license.

### OSS-004 — SPDX header gate does not scan `benchmarks/` or `samples/`
- **Severity:** Low
- **Confidence:** High
- **Category:** SPDX headers / CI guardrail
- **Location:** `tools/license_check/check_spdx_headers.py:14`
- **Evidence:** `DEFAULT_DIRS = ["cmake", "engine", "testing", "tests", "tools", "game"]`. The tracked folders `benchmarks/` (14 files) and `samples/` (16 files, including `samples/modules/starter/src/StarterModule.cpp`) are not scanned, and `game/` does not exist. A manual scan of every tracked `.h/.hpp/.cpp/.inl/.in/.cmake/.py/.hlsl/.hlsli/CMakeLists.txt` file found **zero** files missing the header today.
- **Impact:** A new benchmark or sample file without a header would pass CI (`.github/workflows/ci.yml:40`), which contradicts the documented rule "Every source and CMake file starts with SPDX-License-Identifier: MIT".

### OSS-005 — No SBOM is produced or published
- **Severity:** Low
- **Confidence:** High
- **Category:** SBOM
- **Location:** repository root (no `*.spdx.json` / `*.cdx.json` / `sbom*` file tracked); `tools/license_check/check_licenses.py:107-118` already reads vcpkg's per-port `vcpkg.spdx.json`
- **Evidence:** The per-port SPDX 2.x documents exist under `vcpkg_installed/<triplet>/share/*/vcpkg.spdx.json` and are parsed for the license gate, but nothing aggregates them into a project-level SBOM, and CI uploads none.
- **Impact:** Downstream users and console/storefront compliance reviews cannot get a machine-readable dependency list with versions, hashes and licenses. This is increasingly expected (for example by the EU CRA).

### OSS-006 — Not REUSE-compliant: no `LICENSES/` folder; non-code files carry no license metadata
- **Severity:** Low
- **Confidence:** High
- **Category:** REUSE readiness
- **Location:** repository root (no `LICENSES/`, no `REUSE.toml` or `.reuse/dep5`); `tests/render/golden/*.ppm`, `tests/asset_import/fuzz/corpus/*`, `samples/modules/starter/templates/coins/**/*.json`, `docs/*.md`, `.github/workflows/ci.yml`
- **Evidence:** SPDX headers cover only code and CMake. The 3 golden images and 22 corpus files are generated in-house (`tests/asset_import/fuzz/FuzzStressTests.cpp:203-210`), and the sample JSON content is first-party, but nothing in the tree records that.
- **Impact:** `reuse lint` would fail. Provenance of the binary test assets rests on knowing how they were generated, not on recorded metadata.

### OSS-007 — Copyright holder named inconsistently; no AUTHORS or contribution-provenance policy
- **Severity:** Low
- **Confidence:** High
- **Category:** Project license hygiene
- **Location:** `LICENSE:3` ("The Interi Authors"), `samples/modules/starter/module.json:8` ("Copyright (c) 2026 Interi contributors"), `tests/editor/plugin_stubs/event/module.json:3` ("Copyright (c) Interi"); no `AUTHORS`, `CONTRIBUTING.md` or DCO/CLA in `git ls-files`
- **Evidence:** Three different holder strings. "The Interi Authors" is not defined anywhere. The repo is public (github.com/ebrown38/Interi), and `git log` currently shows a single author under two identities.
- **Impact:** This becomes ambiguous once outside contributions arrive: who "The Interi Authors" are, and on what terms contributions are accepted (inbound = outbound MIT). It is low risk today with one author.

### OSS-008 — THIRD_PARTY_NOTICES includes large source dumps and test-only components
- **Severity:** Info
- **Confidence:** High
- **Category:** Attribution quality
- **Location:** `THIRD_PARTY_NOTICES.md:530-9421` (meshoptimizer), `:9528-18582` (sdl3), `:7` (benchmark), `:259` (gtest)
- **Evidence:** meshoptimizer's vcpkg `copyright` file embeds the full cgltf/fast_obj headers (about 8,900 lines of C source), so the notices file is 19,295 lines. benchmark and gtest only link test and benchmark executables but are listed.
- **Impact:** None legally (over-attribution is harmless). The file is hard to review, and a `ThirdPartyNotices_UpToDate` diff on a baseline bump is mostly noise. No action required.

### OSS-009 — Gitignored `modules/physics` and `modules/destruction` declare MIT but have no LICENSE file
- **Severity:** Info
- **Confidence:** High
- **Category:** Separate projects (not distributed here)
- **Location:** `.gitignore:23` (`/modules/`), `modules/physics/module.json:7`, `modules/destruction/module.json:7`
- **Evidence:** `ls modules/*/LICENSE*` finds nothing; both manifests say `"license": "MIT"`. Jolt is MIT (it shows up in older build trees' `vcpkg_installed/.../share/joltphysics`).
- **Impact:** None for this repository. If those projects are published, they need their own LICENSE and notices (Jolt). Not audited in depth, per the dispatch.

### OSS-010 — DXC host package contains proprietary libdxil; correctly treated as a build tool
- **Severity:** Info
- **Confidence:** High
- **Category:** Build tools
- **Location:** `vcpkg.json:10-13`, `tools/license_check/license_policy.json:23-30`, `tools/license_check/CMakeLists.txt:19-24`
- **Evidence:** DXC runs only at build time to emit SPIR-V. The policy excludes it from the notices and still license-checks the host triplet (`LicenseCheck_HostTools_OnAllowlist`). The note correctly records that `libdxil`/`dxil.dll` (Microsoft proprietary) is never linked or shipped.
- **Impact:** None, as long as nothing starts producing DXIL or packaging `dxil.dll` (a future D3D12 backend would change this).

### OSS-011 — No NDA, console-SDK, GPL-family or proprietary material found; runtime system libraries are not redistributed
- **Severity:** Info
- **Confidence:** High
- **Category:** Proprietary / NDA material
- **Location:** whole tracked tree (`git ls-files`, 781 files); `README.md:7`, `CLAUDE.md:7`
- **Evidence:**
  - Searches for `prospero|orbis|libsce|GDK|scarlett|xbox|ps5|playstation|nintendo` found only prose mentions in README/CLAUDE/AGENTS ("PS5/Xbox backends live in separate private repos").
  - A search of first-party code for `Copyright|Permission is hereby|GNU (Lesser )?General|adapted from|ported from|taken from|stackoverflow|public domain|Licensed under` found only Interi's own manifest fields and tooling text.
  - The tree contains no prebuilt `.a/.so/.lib/.dll` files.
  - The Vulkan loader (Apache-2.0), X11/Wayland/dbus (loaded by SDL at runtime) and Mesa lavapipe (tests only) come from the system.
  - The allowlist (`license_policy.json:3-17`) excludes every copyleft license, and `LicenseCheck_GplDepInjected_Fails` proves the gate rejects GPL.
- **Impact:** None. Record and preserve this state.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 552,120 edges, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | `MATCH (c:Community) ... ORDER BY c.symbolCount DESC LIMIT 30` | Top clusters are third-party build-tree code: Joystick/Hidapi/Video (SDL3), Profiler/Tracy/Libbacktrace/Client (Tracy), Manifold, ImGui docking. Confirms which vendored trees are compiled |
| 3 | cypher | `MATCH (p:Process) RETURN p LIMIT 30` | All 30 shown processes are in `external/vcpkg/buildtrees/sdl3/...` (SDL examples and video backends) |
| 4 | query | "third party license notices generation" | `tools/license_check/gen_notices.py` (`generate_notices`, `main`), `check_licenses.py` (`is_allowed`, `collect_port_licenses`), `NoticesTests`; also `TrustStore.Entry.license` in ModuleManager.h |
| 5 | query | "miniaudio implementation decode clip" | Hits only vendored miniaudio 0.11.25 / stb / ufbx 0.23.1 build trees (BM25 index degraded); first-party file confirmed by literal search |
| 6 | query | "imgui font atlas default font" | ImGui 1.92 docking `imgui_draw.cpp` default font atlas (`FONT_ATLAS_DEFAULT_TEX_DATA_*`), freetype backend; led to ProggyClean/ProggyForever notice check (OSS-002) |
| 7 | context | `generate_notices` (gen_notices.py) | 1 prod caller (`main`) + 5 tests; calls `collect_ports`, `_is_vcpkg_helper`; copies only `share/<port>/copyright` (OSS-002, OSS-003) |
| 8 | context | `is_allowed` (check_licenses.py) | Called by `find_violations` + SPDX expression tests; NOASSERTION/NONE rejected; parser-based allowlist evaluation |
| 9 | cypher | IMPORTS edges `engine/*` → `external/*` | 0 rows: third-party headers are reached through vcpkg include paths, not resolved as file imports; linkage taken from CMake instead |

## Analysis limitations
- No builds, installs or test runs, so `ctest -L license` and `gen_notices.py --check` were not executed. Their state is inferred from the committed notices and the installed SPDX documents of `build/linux-clang-debug`.
- The Windows triplet (`x64-windows-static-md`) installed tree was not available, so Windows-only package contents (for example the Tracy DLL's bundled files) were inferred from the Linux sources.
- The GitNexus full-text index was degraded (BM25 timings of about 55 s, keyword hits dominated by vendored trees). IMPORTS edges do not connect first-party files to vcpkg headers, so linkage was established from CMake files.
- Upstream license texts of sub-components were read from local `external/vcpkg/buildtrees` copies only; no network verification.
- `modules/physics` and `modules/destruction` (gitignored, own git history) were only glanced at, per the dispatch.

## Cross-discipline notes
- Build/Packaging: `cmake/InteriPackage.cmake` installs test-support sources (`tests/rhi_vulkan/VulkanTestSupport.cpp`) as part of the SDK. Fine license-wise; a packaging-scope question.
- Security: the editor's trust model (`TrustStore`, keyed by id + author + license) treats the manifest `license` string as identity and never validates it as an SPDX expression (`engine/editor/include/interi/editor/ModuleManager.h:30-35`).
