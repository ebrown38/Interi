# Security — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Baseline plan written before milestone M-01. These are instructions, not changes.

**Status update:** SEC-001, SEC-002 and SEC-003 were applied by WI-001 to WI-003 (milestone M-01); their steps are kept for reference. Everything else here is unapplied.

## Recommended order
1. **SEC-002, SEC-001.** Hostile content alone can trigger these, with no native code and no prompt: one deletes files, the other corrupts memory. Both fixes are small and local.
2. **SEC-003, SEC-004, SEC-005.** These close the native-code loading path: a private shadow directory, a trust prompt when a project opens, and trust bound to the library hash.
3. **SEC-006, SEC-008.** Add depth limits, then fuzz targets for every runtime parser. The fuzzers keep SEC-001, SEC-006, SEC-010 and SEC-011 from coming back.
4. **SEC-007, SEC-009, SEC-017.** Turn on build hardening and supply-chain hygiene, including a dependency scan in CI.
5. **SEC-010 to SEC-016.** These are defence-in-depth items. Batch them.

## SEC-001 — Detour navigation tile from `.inav` is handed to `dtNavMesh::init` without bounds validation
- **Severity / Effort:** Critical / M
- **Depends on:** None
- **Steps:**
  1. In `engine/nav/src/NavMesh.cpp`, add `ValidateTileData(std::span<const std::byte>)` and call it from `FromData` before `dtAlloc` and `init`. It should:
     - require `size >= sizeof(dtMeshHeader)` and a 4-byte-aligned length, and read the header with `memcpy`;
     - check `magic`, `version`, `x == 0`, `y == 0` and `layer == 0`;
     - require every count (`vertCount`, `polyCount`, `maxLinkCount`, `detailMeshCount`, `detailVertCount`, `detailTriCount`, `bvNodeCount`, `offMeshConCount`) to be ≥ 0 and below a fixed cap, for example 1<<20;
     - recompute the section sizes exactly as `addTile` does, in `std::size_t` with overflow checks, and require their sum to equal the data size;
     - require `maxLinkCount >= 1` whenever `polyCount > 0`;
     - require `offMeshBase <= polyCount` and finite `bmin`/`bmax`.
  2. Walk the polygons. For each poly, require `vertCount <= DT_VERTS_PER_POLYGON` and `verts[i] < header.vertCount`. Neighbour refs must be `< polyCount` or external-link flags. Check the detail-mesh `vertBase`/`triBase` ranges and the BV-node `i` indices the same way. Detour indexes these without checks in `connectIntLinks`, `baseOffMeshLinks` and queries.
  3. Reject `data.size() > INT_MAX` before the `static_cast<int>`.
  4. Bake keeps its current path. Validate only `Read` output, or both, to stay consistent.
  5. Add `NavMeshAsset` to the fuzz targets (SEC-008), with a harness that recomputes the XXH3 so mutations reach the tile parser.
- **Illustrative change (not applied):**
  ```cpp
  Result<void> ValidateTileData(std::span<const std::byte> d)
  {
      if (d.size() < sizeof(dtMeshHeader) || d.size() > INT_MAX || d.size() % 4 != 0) return Corrupt("nav tile size");
      dtMeshHeader h; std::memcpy(&h, d.data(), sizeof h);
      if (h.magic != DT_NAVMESH_MAGIC || h.version != DT_NAVMESH_VERSION) return Corrupt("nav tile header");
      auto sz = [](int n, std::size_t e) -> std::optional<std::size_t> { /* n in [0, cap], align4(n*e) */ };
      // sum(header, verts, polys, links, detail*, bv, offmesh) == d.size(), else Corrupt
      // then range-check poly verts/neis, detail bases, bv indices
  }
  ```
- **Verification:**
  - A unit test feeds truncated, oversized-count, negative-count and mismatched-size tiles under the asan preset. Each must return `Corrupt` with no ASan report.
  - The new fuzz target runs clean for N iterations in `interi_asset_import_stress_tests` or a new nav stress executable.
- **Risks / trade-offs:** The validator duplicates Detour's layout rules. Pin it to Detour 1.6.0 with a `static_assert` on `DT_NAVMESH_VERSION` so that an upgrade forces a review.

## SEC-002 — The asset database deletes files at paths read from `.meta` files without validating them
- **Severity / Effort:** Critical / S
- **Depends on:** None
- **Steps:**
  1. In `ReadMeta` (`AssetDatabase.cpp:233-252`), run `NormalizeVirtualPath` on every `products[].path`, `reads[]` and `references[]` entry. Then require each product path to start with `<importedFolder>/`. Treat a meta that fails as unreadable, so `Scan()` regenerates it with a warning, which is the existing behaviour for unreadable metas.
  2. At both delete sites (lines 694-700 and 965-970), delete through a helper that:
     - resolves `root / path`;
     - calls `weakly_canonical` on it;
     - checks that the result is inside `canonical(root / importedFolder)`;
     - refuses symlinks (`symlink_status`).
  3. Better still, delete only files this database wrote. That is, keep products whose recorded hash matches the file on disk. A file whose contents differ from what was cooked should be left alone.
  4. Add regression tests: a valid PNG source plus a `.meta` with `"path": "/tmp/x"`, `"../outside"` and `"imported/../../x"`. Each must leave the target file in place after `CookAll()` and `Prune()`.
- **Illustrative change (not applied):**
  ```cpp
  bool SafeRemoveProduct(const fs::path& importedRoot, std::string_view rel)
  {
      auto norm = NormalizeVirtualPath(rel); if (!norm) return false;
      const fs::path p = fs::weakly_canonical(importedRoot.parent_path() / *norm);
      if (!IsWithin(p, fs::canonical(importedRoot)) || fs::is_symlink(fs::symlink_status(p))) return false;
      std::error_code e; return fs::remove(p, e);
  }
  ```
- **Verification:** The new tests in `tests/asset_import` pass. A grep confirms that no `fs::remove` in `engine/asset_import` takes a path that skipped `SafeRemoveProduct`.
- **Risks / trade-offs:** Metas written by older builds with unusual paths would be regenerated, which costs one re-cook. That is safe.

## SEC-003 — Module shadow copies go to a shared, predictable `/tmp/interi_modules` directory (Linux)
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Default `shadowDirectory` to a per-user directory that only that user can access:
     - Linux: `$XDG_RUNTIME_DIR/interi/modules`, falling back to `$XDG_CACHE_HOME`/`~/.cache/interi/modules`;
     - Windows: `%LOCALAPPDATA%\Interi\modules`.
  2. Alternatively, create a unique session directory with `mkdtemp`, which uses mode 0700.
  3. After `create_directories`, verify that the directory is owned by the current uid, is not a symlink and has mode 0700 (`lstat`). Refuse to load if any check fails.
  4. Write each shadow copy with `open(O_CREAT|O_EXCL|O_NOFOLLOW, 0600)`, using a random suffix instead of a counter. `dlopen` the descriptor-backed path, or open and `dlopen` `/proc/self/fd/N` on Linux, so the checked file is the loaded file.
  5. Set `shadowDirectory` explicitly in `EditorApp` and `ProjectModules`, so games and the editor don't depend on the default.
- **Illustrative change (not applied):**
  ```cpp
  // m_desc.shadowDirectory = PerUserCacheDir() / "modules";  // 0700, owner-checked
  int fd = ::open(shadow.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  ```
- **Verification:**
  - A test pre-creates the shadow directory with another mode or as a symlink and expects `ModuleHost` to refuse the load.
  - `ModuleHostTest` stays green under the tsan and asan presets.
  - Manual check: `stat` on the directory shows 0700 and the current user as owner.
- **Risks / trade-offs:** Shadow files can accumulate across crashes. Add cleanup of stale files at startup, limited to the per-user directory.

## SEC-004 — Opening a project loads its native module libraries without a trust confirmation
- **Severity / Effort:** High / M
- **Depends on:** SEC-005 (so the prompt is meaningful)
- **Steps:**
  1. In `EditorApp::OpenProjectFolder`, resolve the module graph without opening libraries. Collect every module that has a `library` or an adapter library and for which `NeedsTrust` is true.
  2. If any are untrusted, open the project in restricted mode, like VS Code's Workspace Trust:
     - data editing, chains with the core library, and no `LoadProjectModulesHost`;
     - a banner and a "Trust and load modules" button that reuses the Module Manager confirmation dialog.
  3. Apply the same gate before `LibraryWatcher` auto-reloads a library that changed (`EditorApp.cpp:1350`). With SEC-005's hash, a changed library is no longer trusted until confirmed again.
  4. For `level_editor --project-file`, add an explicit `--trust-modules` flag for scripted use. Without it, use restricted mode.
  5. Update `docs/module-manager.md:74-75` to describe the new behaviour.
- **Verification:**
  - A headless editor test opens a project that lists a test module with an empty `TrustStore`. It must assert that `m_modules == nullptr` and that a pending trust request exists.
  - After `Trust`, the modules load.
- **Risks / trade-offs:** This adds a click for developers who open their own projects. The trust store keeps that to once per module version or hash.

## SEC-005 — The trust store identifies modules by self-declared metadata, not by library content
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Extend the `TrustStore` entry with the SHA-256 of each library file (main and adapters) at the moment of trust. Keep id, author and license for display.
  2. Make `IsTrusted` require a matching hash. A rebuilt library asks again, or offers "trust rebuilds of this module from this folder", which records the folder path plus the user's explicit choice.
  3. When neither `HOME` nor `XDG_CONFIG_HOME` is set, `DefaultPreferencesPath` should refuse to persist trust rather than fall back to `temp_directory_path()`. Create the preferences directory with mode 0700.
- **Verification:** Unit tests in `tests/editor`:
  - same id, author and license with a different library hash → `NeedsTrust` is true;
  - an unchanged library → false;
  - no `HOME` → `TrustStore::Save` fails or is skipped.
- **Risks / trade-offs:** Hashing large libraries on every open costs a few milliseconds each. Cache the result by (path, size, mtime) when needed.

## SEC-006 — Unbounded recursion in the chain expression parser and evaluator
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add `kMaxExpressionDepth`, for example 128, and `kMaxExpressionLength`, for example 16 KiB, to `Expression.cpp`. Reject longer source text with a diagnostic before lexing.
  2. Keep a parser depth counter incremented in `ParseBinary`, `ParseUnary` and `ParsePrimary`, the latter covering `(` and call arguments. When it exceeds the limit, return `Fail("expression nests too deeply", span)`.
  3. Also cap the depth of the expression tree, because parse depth does not bound it. `ParseBinary`'s loop builds left-deep trees for `1+1+1+...` or `a&&b&&...` without recursing, so a flat chain within the length cap still gives thousands of `Eval` levels. In `Add()`, store `depth = 1 + max(child depths)` on each node, or in a parallel array, and fail compilation when it exceeds `kMaxExpressionDepth`. A balanced alternative is to make `Evaluator::Eval` iterative over an explicit stack.
  4. Add tests. 10,000 nested `(`, 10,000 unary `-`, and a flat chain of 5,000 `+1` terms must each produce a compiler diagnostic, not a crash, under asan.
- **Illustrative change (not applied):**
  ```cpp
  struct DepthGuard { int& d; explicit DepthGuard(int& x) : d(++x) {} ~DepthGuard() { --d; } };
  ParseResult ParseUnary() { DepthGuard g(m_depth); if (m_depth > kMaxExpressionDepth) return Fail("too deep", Peek().span); ... }
  // In Add(node): node depth = 1 + max(depth[a], depth[b], args...); if > kMaxExpressionDepth -> Fail("expression too deep")
  ```
- **Verification:** The new `ChainExpression.*Deep*` tests pass under asan. `ChainDeterminism.MatchesCrossPlatformGoldenHash` is unchanged.
- **Risks / trade-offs:** The tree-depth cap also limits long flat sums. 128 or more terms covers any hand-written chain. Raise it, or switch `Eval` to an explicit stack, if generated content needs more.

## SEC-007 — The build sets no platform hardening flags
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Add `cmake/InteriHardening.cmake`, applied to engine, tool and game targets, not to tests.
  2. Clang/Linux flags:
     - `-fstack-protector-strong`, `-fcf-protection=full`, `-fstack-clash-protection`;
     - `-D_FORTIFY_SOURCE=3` for non-Debug builds only, together with `-U_FORTIFY_SOURCE` to avoid redefinition warnings;
     - link options `-Wl,-z,relro,-z,now,-z,noexecstack`;
     - `CMAKE_POSITION_INDEPENDENT_CODE ON`.
  3. MSVC: `/guard:cf` at compile and link time, `/CETCOMPAT` at link time, and `/DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT` for clarity.
  4. Carry the options that affect ABI through `interi::build_settings` only if they are needed. These flags are not ABI-relevant, so the toolchain fingerprint is unchanged.
- **Verification:**
  - `checksec --file=build/linux-clang-release/tools/editor/level_editor` reports Full RELRO, a canary, NX, PIE and FORTIFY.
  - `dumpbin /headers` on Windows shows `Guard CF` and `CET compatible`.
  - All presets build with `-Werror`, and benchmarks show at most a 2% regression.
- **Risks / trade-offs:** FORTIFY needs optimisation, so Debug builds do not get it. CFG can conflict with Tracy or third-party static libraries. Build-test on the profile preset.

## SEC-008 — Fuzzing covers asset import only, not the runtime content parsers
- **Severity / Effort:** Medium / L
- **Depends on:** SEC-001 and SEC-006 should land first, or the new targets will fail at once.
- **Steps:**
  1. Extend the `Target` enum in `tests/asset_import/fuzz/FuzzTargets.cpp`, or add `tests/runtime_fuzz/` with the same structure, with targets for:
     - `NavMesh::Read`;
     - `ReadSave` and `RestoreSceneState` on a small scene;
     - `ReadCookedData`;
     - `ParseChain` + `CompileChain`, and `ParseChainFunction`;
     - `CompileExpression`;
     - `ParseScene` + `InstantiateScene`, and prefab expansion;
     - `ParseHud`, `ParseStringTable`, `ParseSkeleton`, `ParseClip` and `ParseAnimGraph` (anim);
     - `ParseManifest`, `ParseProject`, `LoadProjectData`, `ParseCameraRigs` and `ApplyInputConfig`.
  2. For hashed containers, add a harness mode that mutates the payload and then rewrites the XXH3, so mutations get past the integrity check.
  3. Seed corpora from `samples/modules/starter/templates/coins` and the test fixtures, then commit them under `tests/.../corpus`.
  4. Run the libFuzzer harness nightly with a time budget, using asan and ubsan. Keep the deterministic stress mode in the PR CI.
- **Verification:** Each runtime format has a target. The nightly CI job runs each one for at least 5 minutes with no crashes.
- **Risks / trade-offs:** CI time. Keep PR runs short, with fixed iteration counts.

## SEC-009 — stb_image v2.30 (editor import) has a history of unpatched upstream advisories
- **Severity / Effort:** Medium / S
- **Depends on:** SEC-017 (scanner in CI)
- **Steps:**
  1. Run OSV-Scanner or `vcpkg` SBOM plus Grype on the resolved ports. Record the open advisories that affect stb_image's PNG, JPEG, TGA and HDR paths.
  2. If any apply, either move to a maintained fork or decoder (for example `wuffs`, `libspng` plus `libjpeg-turbo`, all permissive), or patch through an overlay port as was done for `cmake/ports/ufbx`.
  3. Keep the existing caps, and decode imports in the fuzzed path (already covered by `Target::Image`).
- **Verification:** The scanner report for the vcpkg baseline lists no open advisory against an enabled decoder, or each one has a documented waiver.
- **Risks / trade-offs:** Changing the decoder affects import bit-exactness of the cooked `.itex` files. Re-cook and regenerate goldens if needed.

## SEC-010 — `ArchiveReader` count checks allow allocations far larger than the input
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `ArchiveReader::Process` for vectors of non-byte types, don't resize up front. Call `reserve(min(count, Remaining() / kMinEncodedSize<T>))` and `emplace_back` per element, stopping when `!m_ok`.
  2. Alternatively, pass a per-type minimum encoded size and check `count * minSize <= Remaining()`.
  3. Move the `kMaxSections` check into a `Serialize` overload, or into a max-count helper, so the limit applies before allocation.
- **Verification:** A unit test with a 1 MB archive that claims 1M `SaveSection`s keeps peak RSS below a few MB (`TrackedAllocator` or `mallinfo`).
- **Risks / trade-offs:** Element-by-element growth is slightly slower for large valid arrays. `reserve` with the clamp avoids most of that cost.

## SEC-011 — Audio decoding has no cap on decoded length
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `kMaxClipFrames`, for example 10 minutes at the target rate, or a `DecodeOptions::maxFrames`.
  2. Before decoding, query `ma_decoder_get_length_in_pcm_frames` and reject clips that declare more.
  3. Also stop the loop when the limit is reached and return `InvalidArgument`.
- **Verification:** A test with a synthetic WAV header that declares 2^32 frames returns an error without allocating more than the cap.
- **Risks / trade-offs:** Long music tracks would need streaming instead of `DecodeClip`. This is a reasonable split.

## SEC-012 — The native file system and template copy follow symlinks, check no containment and cap no read size
- **Severity / Effort:** Low / M
- **Depends on:** None
- **Steps:**
  1. In `NativeFileSystem::Resolve`, compute `weakly_canonical(m_root / normalized)` and require it to be inside `canonical(m_root)`. Add an option for the editor to refuse symlinks entirely.
  2. Add an optional `maxFileSize` to `NativeFileSystem`, for example 1 GiB by default, enforced in `ReadFile`.
  3. In `CreateProject`, pass `copy_options::recursive | copy_options::copy_symlinks`, or skip symlinks with a warning.
- **Verification:** Tests in `tests/platform` with a symlink pointing outside the root expect `ReadFile` and `WriteFile` to fail. A template containing a symlink expects it not to be dereferenced.
- **Risks / trade-offs:** Developers who symlink content folders on purpose need an opt-in.

## SEC-013 — Scene save restore applies transforms without checking they are finite
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `RestoreSceneState`, validate every `ObjectState` before mutating the world: positions and scales finite, scale non-zero, rotation finite and normalised within 1e-3. Return `Corrupt` before any change is made, which preserves the all-or-nothing behaviour of `LoadFromSlot`.
- **Verification:** A test that restores a save with NaN or zero-quaternion objects returns an error and leaves the world unchanged.
- **Risks / trade-offs:** None.

## SEC-014 — Windows module loading uses the default DLL search order for dependencies
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Call `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)` once at process start in the editor and game executables.
  2. In `DynamicLibrary::Open`, use `LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)`. Use `AddDllDirectory` for the engine's `bin` folder and the module's original folder, because the shadow folder holds only the module itself.
- **Verification:** The Windows CI module tests (`ModuleHostTest`, `Package_*`) still pass. Process Monitor shows no DLL probes in the current directory.
- **Risks / trade-offs:** Modules that rely on `PATH` for their dependencies must ship them next to the library.

## SEC-015 — The editor writes its ImGui layout to a fixed file name in the shared temp directory
- **Severity / Effort:** Low / S
- **Depends on:** SEC-005 (the same per-user config directory)
- **Steps:**
  1. Store `interi_editor_layout_v10.ini` in the per-user config directory next to `editor.json`, which is mode 0700.
- **Verification:** On Linux, `strace -e openat level_editor --frames 1` shows no file opened under `/tmp`.
- **Risks / trade-offs:** None.

## SEC-016 — The Tracy client in profile builds listens and broadcasts on all interfaces
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Define `TRACY_ONLY_LOCALHOST`, and optionally `TRACY_NO_BROADCAST`, when the tracy port is built and when `Tracy::TracyClient` is consumed. Use an overlay port or a triplet `VCPKG_CXX_FLAGS` entry for the port build.
  2. Add an opt-in `INTERI_PROFILING_REMOTE=ON` for console or remote capture.
- **Verification:** `ss -lntp` while `level_editor` from the profile preset is running shows the Tracy port bound to 127.0.0.1 / ::1 only.
- **Risks / trade-offs:** Remote profiling of a second machine then needs the opt-in.

## SEC-017 — CI references actions by mutable tags and uses an unpinned container image
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Pin every `uses:` to a full commit SHA, keeping the tag in a comment, and let Dependabot (`.github/dependabot.yml`, ecosystem `github-actions`) bump them.
  2. Pin `container: fedora:44@sha256:<digest>`.
  3. Add a dependency-vulnerability step, for example OSV-Scanner on `vcpkg.json` and the SBOM.
- **Verification:** `grep -E 'uses: [^@]+@v[0-9]' .github/workflows/ci.yml` returns nothing, and the scanner job runs on PRs.
- **Risks / trade-offs:** Pins need maintenance, which Dependabot automates.

## SEC-018 — Positive observations
- **Severity / Effort:** Info / —
- **Depends on:** None
- **Steps:** No action required. Keep these properties: no secrets in the repo, a read-only CI token, path confinement in `ResolveReference` and `project.json`, a non-throwing JSON parse, and the build command taken only from the UI. If multiplayer or networking is added later, start a server-authoritative design review at that point.
- **Verification:** —
- **Risks / trade-offs:** —
