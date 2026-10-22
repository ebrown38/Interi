# Open-Source-Validation — Goal State

*Engineering guidance, not legal advice.*

## Vision
Interi remains an unambiguously MIT-licensed engine whose every shipped binary, SDK install and source checkout carries complete, accurate attribution:
- Each vcpkg port has its license text, and so does every component bundled inside a port (Tracy's libbacktrace, LZ4 and rpmalloc; ImGui's embedded fonts).
- License conclusions are tied to reviewed versions, so a dependency upgrade cannot silently change the license picture.
- Machine-readable evidence (an SPDX SBOM per platform, REUSE metadata) is produced in CI.
- The existing state is preserved: permissive-only dependencies, SPDX headers everywhere, no NDA or proprietary material.

## Acceptance criteria
- [ ] AC-1 After `cmake --install build/<preset> --prefix <p>`, the files `<p>/share/doc/interi/LICENSE` and `<p>/share/doc/interi/THIRD_PARTY_NOTICES.md` exist and are byte-identical to the repository copies; a `Package_*` integration test asserts it. (resolves OSS-001)
- [ ] AC-2 `THIRD_PARTY_NOTICES.md` contains the copyright and license text for libbacktrace (FSF, BSD-3-Clause), LZ4 (Yann Collet, BSD-2-Clause), Tracy's rpmalloc, and the ImGui ProggyClean/ProggyForever fonts (Tristan Grimmer / Disco Hello, MIT); `ThirdPartyNotices_UpToDate` passes on Linux and Windows. (resolves OSS-002)
- [ ] AC-3 Every `overrides` entry in `license_policy.json` names the port version it was reviewed for; `LicenseCheck_AllDepsOnAllowlist_Passes` fails when the installed version differs; the notices show the concluded license (no `LicenseRef-vcpkg-null` in `THIRD_PARTY_NOTICES.md`). (resolves OSS-003)
- [ ] AC-4 `check_spdx_headers.py` scans every tracked first-party source file, including `benchmarks/` and `samples/` (or all of `git ls-files` minus `external/`), and a unit test proves a header-less `samples/` file fails. (resolves OSS-004)
- [ ] AC-5 CI publishes an SPDX 2.3 (or CycloneDX) SBOM per platform triplet, listing every non-helper port with version and license and the root package `interi` (MIT). (resolves OSS-005)
- [ ] AC-6 `reuse lint` passes: a `LICENSES/MIT.txt` exists, and `REUSE.toml` covers golden images, fuzz corpus, sample content, docs and CI files. (resolves OSS-006)
- [ ] AC-7 One copyright-holder string is used in `LICENSE`, sample manifests and the `new_module.py` default; `CONTRIBUTING.md` states that contributions are accepted under MIT (inbound = outbound), and "The Interi Authors" is defined (AUTHORS file or git history). (resolves OSS-007)
- [ ] AC-8 (preserve) `ctest -L license` passes on both platforms; the allowlist still excludes all GPL/LGPL/AGPL/MPL/SSPL/BUSL/NC licenses; `LicenseCheck_GplDepInjected_Fails` still runs in CI. (keeps OSS-011 true; context for OSS-008 and OSS-010)
- [ ] AC-9 (preserve) No console SDK, NDA or prebuilt proprietary binary is tracked. A check scans every tracked file's content, not just its extension (e.g. `git ls-files -z | xargs -0 grep -IL .` lists the non-text files), and every binary it finds must match an explicit, reviewed allowlist of first-party data (today only `tests/asset_import/fuzz/corpus/*`, plus the `external/vcpkg` submodule entry), so a tracked `.exe`, `.bin`, `.a`, `.so`, `.lib`, `.dll`, `.dylib`, `.o`/`.obj` or extension-less executable fails; and no PS5/Xbox/Switch SDK headers appear in the tree (the platform audit's `d3d12`/`dxgi`/console-header rule keeps passing). (keeps OSS-011 true)
- [ ] AC-10 (preserve) `directx-dxc` stays in `build_tools` and nothing links or packages `libdxil`/`dxil.dll`. Any change to that triggers a license re-review. (OSS-010)
- [ ] AC-11 (before publication only) `modules/physics` and `modules/destruction` each carry a `LICENSE` and notices for Jolt before they are published as separate repositories. (OSS-009)

## Guardrails to keep it that way
- CI `license` label tests (`LicenseCheck_*`, `ThirdPartyNotices_UpToDate`, `SpdxHeader_AllSourceFiles_Present`) stay in the blocking `unit` set on Linux and Windows.
- A version-pinned overrides and extra-notices review: a baseline bump that moves a reviewed port fails until the entry is re-reviewed.
- `Package_*` integration tests assert that the installed LICENSE and notices are present.
- CI uploads the SBOM artifact; optionally run `reuse lint` in CI.
- PR checklist item: "New dependency? Add to `vcpkg.json`, run `ctest -L license`, regenerate notices, check for bundled sub-components."

## Out of scope
- Legal interpretation beyond engineering review (this is not legal advice).
- Auditing the private console backends and the gitignored module projects in depth (beyond AC-11).
- Licensing of system libraries loaded at runtime that Interi neither builds nor redistributes (the Vulkan loader, Mesa/lavapipe, X11/Wayland/dbus).
- Content licensing for user projects and third-party modules: the engine records their manifest `license` field but does not police it.
