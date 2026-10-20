# Security — Issues

**Project:** Interi (/home/eric/CLionProjects/Interi)
**Analyzed:** 2026-10-19
**Scope:** This was a static review of first-party code: `engine/` (375 files), `tools/` (the C++ editor and module_check, plus 7 Python scripts), `samples/` and `testing/`, the CMake build (`CMakeLists.txt`, `cmake/*.cmake`, `CMakePresets.json`), `vcpkg.json` and the versions it resolves in `external/vcpkg/buildtrees`, and `.github/workflows/ci.yml`. I modelled these trust boundaries:
- cooked binary assets (`.imesh`, `.itex`, `.imat`, `.iskel`, `.ianim`, `.iskin`, `.inav`, `IVAL`);
- save files (`ISAV`);
- JSON content (scene, prefab, chain, chain function, HUD, strings, project, module.json, project data, `.meta`);
- model and image import (glTF, OBJ, FBX, PNG, JPEG, TGA, HDR) and audio decode;
- native module loading and hot reload, and the editor trust store;
- the virtual file system;
- the editor's shell command;
- CI.

`modules/` (Physics and Destruction) and vendored third-party code were not reviewed. The exceptions are the Detour and Tracy lines cited as sinks.
**Summary:** 18 issues — Critical: 2 · High: 2 · Medium: 5 · Low: 8 · Info: 1

## Findings

### SEC-001 — Detour navigation tile from `.inav` is handed to `dtNavMesh::init` without bounds validation (heap out-of-bounds write)
- **Severity:** Critical
- **Confidence:** High
- **Category:** Memory safety / untrusted binary parsing
- **Location:** `engine/nav/src/NavMesh.cpp:123-138`, `engine/nav/src/NavMesh.cpp:302-323`, `engine/game/src/GameSession.cpp:49-62`, `external/vcpkg/buildtrees/recastnavigation/src/v1.6.0-fcf1526bd5.clean/Detour/Source/DetourNavMesh.cpp:267-287,908-1005`
- **Evidence:** `NavMesh::Read` checks only the Archive header, the XXH3 payload hash, `!data.empty()` and the bake settings. It then calls `FromData`, which copies the blob and runs `mesh->init(tile, static_cast<int>(data.size()), DT_TILE_FREE_DATA)`. In Detour 1.6.0, `init` and `addTile` check only `magic` and `version`. They never compare `dataSize` with the header's counts. Even a blob shorter than `sizeof(dtMeshHeader)` is dereferenced. `addTile` derives every section pointer from header fields (`vertCount`, `polyCount`, `maxLinkCount`, `detailMeshCount` and others, all signed `int`):
  ```cpp
  const int linksSize = dtAlign4(sizeof(dtLink)*(header->maxLinkCount));
  ...
  tile->links[header->maxLinkCount-1].next = DT_NULL_LINK;
  for (int i = 0; i < header->maxLinkCount-1; ++i) tile->links[i].next = i+1;
  connectIntLinks(tile);
  ```
  These writes land at offsets the attacker chooses, outside the `dtAlloc` buffer. The XXH3 hash is not a barrier because it is unkeyed and anyone can recompute it. The `int` cast also truncates blobs of 2 GiB or more. `GameSession::Open` loads `content/navmesh.inav` automatically from the project folder. `impact(FromData)` shows two callers, `Read` and `Bake`, and only `Read` takes file input. No fuzz target covers this reader (see SEC-008).
- **Impact:** A crafted or modded `navmesh.inav` in a game or project corrupts heap memory as soon as the session opens. That can crash the game or the editor and may allow code execution. Content-only projects are affected too: the editor's trust model (`docs/module-manager.md`) treats them as safe because they contain no native library.

### SEC-002 — The asset database deletes files at paths read from `.meta` files without validating them (arbitrary file deletion when a project opens)
- **Severity:** Critical
- **Confidence:** High
- **Category:** Path traversal / untrusted data driving file-system operations
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:233-252`, `engine/asset_import/src/AssetDatabase.cpp:694-700`, `engine/asset_import/src/AssetDatabase.cpp:805-816`, `engine/asset_import/src/AssetDatabase.cpp:965-970`, `tools/editor/EditorApp.cpp:569-584`, `tools/editor/EditorApp.cpp:664-668`
- **Evidence:** `ReadMeta` copies `cook.products[].path` straight from the JSON (`field("path", product.path)`) and never calls `NormalizeVirtualPath`. `Scan()` keeps any `.meta` that parses, so the record carries the attacker's product list (`fresh = false`). It regenerates a meta only when the meta is unreadable or has a duplicate guid. `CookRecord` then cooks the source. Because the crafted meta's cook key doesn't match, the cook always runs. After a successful cook, it removes every earlier product that this cook did not produce:
  ```cpp
  if (!kept.contains(old.path)) { std::error_code ignored; fs::remove(options.root / old.path, ignored); }
  ```
  `Prune` does the same (`fs::remove(impl.options.root / product.path, error)`) for metas whose source is gone. With `std::filesystem`, `root / "/abs/path"` resolves to `/abs/path`, and `root / "../../x"` leaves the root. The only precondition is a valid source next to the crafted meta, for example a tiny PNG, so that the cook succeeds. `EditorApp::OpenProjectFolder` sets `m_dataDir = folder` and calls `OpenAssetDatabase()`, which runs `Scan()` and `CookAll()` with no prompt.
- **Impact:** Opening a downloaded project or asset pack in `level_editor` deletes any file the user can write, such as `~/.ssh/id_ed25519`, `~/.bashrc` or source files. No native code and no trust prompt are involved. The result is data loss.

### SEC-003 — Module shadow copies go to a shared, predictable `/tmp/interi_modules` directory before `dlopen` (Linux)
- **Severity:** High
- **Confidence:** High
- **Category:** Insecure temporary files / native plugin loading
- **Location:** `engine/hotreload/src/ModuleHost.cpp:409-412`, `engine/hotreload/src/ModuleHost.cpp:447-458`, `engine/game/src/ProjectModules.cpp:44`, `tools/editor/EditorApp.cpp:262-264`
- **Evidence:** When `shadowDirectory` is empty, the host uses `temp_directory_path() / "interi_modules"`. Neither the editor nor `ProjectModules` sets it, so both the editor and shipped games use the default. `OpenLibrary` calls `create_directories` and ignores whether the directory already existed and who owns it. It then runs `copy_file(library, shadow, overwrite_existing)` with `shadow = <stem>.<counter><ext>`, where the counter starts at 0, and finally calls `DynamicLibrary::Open(shadow)`. On Linux, `temp_directory_path()` is the shared `/tmp` whenever `TMPDIR` is unset, which is the usual case. A local attacker can create `/tmp/interi_modules` first. They then own it, so `fs.protected_symlinks` does not protect files inside it. They can swap `lib<x>.0.so` between the copy and the `dlopen`, or pre-plant a symlink or a writable file under the predictable name.
- **Impact:** Another local user can run code as the user who runs the editor or game, through module load and every hot reload. This is a cross-user escalation on shared Linux machines and CI hosts. Windows is not affected, because `%TEMP%` is per user.

### SEC-004 — Opening a project loads its native module libraries without a trust confirmation
- **Severity:** High
- **Confidence:** High
- **Category:** Mod/plugin loading trust boundary
- **Location:** `tools/editor/EditorApp.cpp:252-275`, `tools/editor/EditorApp.cpp:659`, `tools/editor/EditorApp.cpp:1350`, `engine/game/src/ProjectModules.cpp:15-48`, `docs/module-manager.md:74-75`
- **Evidence:** `OpenProjectFolder` calls `LoadProjectModulesHost(*project)`. That function opens every module in `project.modules` from the project's own `moduleDirs` through `ModuleHost::LoadProject`, and the libraries' `load` entry points run with full trust. `NeedsTrust` and the confirmation dialog run only on the Module Manager's Enable path (`ModuleManager.cpp:107`). The docs say so: "Opening a project that already lists modules does not ask: a project you did not write can load native code as soon as it opens." With auto-reload on, `LibraryWatcher` also reloads libraries that change on disk without asking (`EditorApp.cpp:1350`).
- **Impact:** Opening an untrusted project in the editor, from a recent-projects entry, `--project-file` or the Project menu, executes code from that project at once. The documentation states this, but nothing in the product enforces it. It is the same class of problem as opening an untrusted VS Code workspace before Workspace Trust existed.

### SEC-005 — The trust store identifies modules by self-declared metadata, not by library content
- **Severity:** Medium
- **Confidence:** High
- **Category:** Plugin authenticity
- **Location:** `engine/editor/src/ModuleManager.cpp:118-130`, `engine/editor/src/ModuleManager.cpp:196-201`, `engine/editor/src/EditorServices.cpp:122-143`
- **Evidence:** `TrustStore::IsTrusted` matches `id`, `author` and `license`, which are all free-text fields in the module's own `module.json`. Nothing records a hash or signature of the library. A malicious module that copies a trusted module's three strings is trusted silently, and so is a trusted module whose library is later replaced. `TrustStorePath` sits next to the preferences file. `DefaultPreferencesPath` falls back to `temp_directory_path()/interi_editor.json` when neither `XDG_CONFIG_HOME` nor `HOME` is set, which puts `trusted_modules.json` in the shared `/tmp`, where another user could pre-seed it.
- **Impact:** The one gate in front of native code (SEC-004) is easy to spoof. The trust decision does not cover the code that actually runs.

### SEC-006 — Unbounded recursion in the chain expression parser and evaluator (stack exhaustion from `.chain.json`)
- **Severity:** Medium
- **Confidence:** High
- **Category:** Untrusted input parsing / denial of service
- **Location:** `engine/chain/src/Expression.cpp:541-560`, `engine/chain/src/Expression.cpp:668-677`, `engine/chain/src/Expression.cpp:765-767`, `engine/chain/src/Expression.cpp:1152-1200`, `engine/chain/src/Compiler.cpp:293,406`
- **Evidence:** In the call graph (cypher on `Expression.cpp`), `ParseUnary` calls `ParseUnary` for each `-` or `!`, and `ParsePrimary` calls `ParseBinary` for each `(`. Neither has a depth counter, and expression text has no length cap. `Evaluator::Eval` recurses once per tree level. A flat left-associative chain such as `1+1+1+...` or `a&&b&&...` is parsed by `ParseBinary`'s loop without recursion, yet it still builds a left-deep tree whose depth equals the number of terms. That reaches the same `Eval` depth, so a limit on parser depth alone would not fix the evaluator. Expressions come from the `"expr"` bindings and link `condition`s in `.chain.json` files that projects ship, and the editor and `SceneRuntime` compile them. Evaluation runs on job workers.
- **Impact:** A chain containing, for example, 200,000 `(` characters, `-` signs or `+1` terms overflows the stack in the compiler or on a job worker, and the editor or game crashes. Windows threads have 1 MB stacks, which is the tightest limit.

### SEC-007 — The build sets no platform hardening flags
- **Severity:** Medium
- **Confidence:** Medium
- **Category:** Binary hardening
- **Location:** `cmake/InteriWarnings.cmake:7-14`, `cmake/InteriArch.cmake:35`, `CMakePresets.json:26-58`
- **Evidence:** A search of `CMakeLists.txt`, `cmake/*.cmake`, the presets and the engine and tool CMake files finds no `-fstack-protector-strong`, `-D_FORTIFY_SOURCE`, `-Wl,-z,relro,-z,now` or `-fcf-protection`. On MSVC it finds no `/guard:cf` or `/CETCOMPAT`. MSVC already defaults to `/GS`, `/DYNAMICBASE`, `/HIGHENTROPYVA` and `/NXCOMPAT`, and Clang 15+ defaults to PIE on Linux, so those are not reported as missing. For the flags that are missing, the actual state of the binaries depends on the distribution's compiler configuration. No binary was available to inspect.
- **Impact:** The engine parses hostile content (SEC-001, SEC-006, the importers), so these mitigations would turn some memory-corruption bugs into clean aborts. Without them, shipped games and the editor rely on the toolchain's defaults.

### SEC-008 — Fuzzing covers asset import only, not the runtime content parsers
- **Severity:** Medium
- **Confidence:** High
- **Category:** Untrusted parser assurance
- **Location:** `tests/asset_import/fuzz/FuzzTargets.cpp:130-247`, `engine/nav/src/NavMesh.cpp:302`, `engine/runtime/src/SaveGame.cpp:65`, `engine/scene/src/SceneSave.cpp:89`, `engine/values/include/interi/values/ProjectData.h:125`, `engine/chain/src/ChainAsset.cpp:274`, `engine/scene/src/SceneFile.cpp:127`, `engine/hud/src/HudDocument.cpp:219`, `engine/sdk/src/Manifest.cpp:544`, `engine/sdk/src/Project.cpp:90`, `engine/loc/src/Localization.cpp:74`, `engine/anim/src/Animation.cpp:249`
- **Evidence:** The fuzz targets cover glTF, OBJ, FBX, images, texture import, and the `.imesh`, `.itex`, `.imat`, `.iskel`, `.ianim`, `.iskin` and model-manifest formats. No target covers `NavMesh::Read`, `ReadSave`, `RestoreSceneState`, `ReadCookedData`, the chain or chain-function parsers, the expression compiler, scenes and prefabs, the HUD, string tables, anim JSON, module.json or project.json. These are the parsers a shipped game runs on its content and saves. SEC-001 and SEC-006 would probably have been found by such targets.
- **Impact:** Memory-safety and crash bugs in runtime parsers go undetected. The project's own CLAUDE.md treats importers as hostile-input code, but the runtime formats don't get the same treatment.

### SEC-009 — stb_image v2.30 (editor import) has a history of unpatched upstream advisories
- **Severity:** Medium
- **Confidence:** Low
- **Category:** Vulnerable dependencies
- **Location:** `vcpkg.json` (`"stb"`, baseline `e456309491fd875487bf02b8a0dca76c1b1b00cc`), `external/vcpkg/buildtrees/stb/src/bb2f77cd20-7445f7a01f.clean/stb_image.h:1`, `engine/asset_import/src/StbImplementation.cpp:4-10`
- **Evidence:** The resolved `stb_image.h` reports `v2.30`. Upstream stb_image has received several public memory-safety reports in recent years, for example the 2023 GHSL batch, and some stayed open in the upstream tracker. This build turns on the PNG, JPEG, TGA and HDR decoders, and `STBI_MAX_DIMENSIONS 16384` plus `kMaxImagePixels` limit the sizes. Whether any open advisory affects the decoders enabled here could not be checked offline.
- **Impact:** A hostile image in a project or asset pack might corrupt memory during editor import. Only the editor import is reachable; the runtime loads cooked `.itex` files.

### SEC-010 — `ArchiveReader` count checks allow allocations far larger than the input
- **Severity:** Low
- **Confidence:** High
- **Category:** Resource exhaustion
- **Location:** `engine/core/include/interi/core/Archive.h:219-235`, `engine/core/src/Archive.cpp:65-75`, `engine/runtime/src/SaveGame.cpp:78-84`
- **Evidence:** `ReadCount` rejects only `count > Remaining()`, on the assumption that each element takes at least one byte. `Process` then calls `value.resize(count)` before reading any element, which allocates `count * sizeof(T)`. For structs such as `SaveSection` (about 64 bytes) or `Joint`, that is up to about 64 times the file size. `kMaxSections` (4096) is checked only after the vector has been filled. The engine has no `catch` blocks, so a `std::bad_alloc` terminates the process.
- **Impact:** A large crafted save or cooked file can exhaust memory or abort the process. It is bounded by file size times element size.

### SEC-011 — Audio decoding has no cap on decoded length (decompression bomb)
- **Severity:** Low
- **Confidence:** High
- **Category:** Resource exhaustion
- **Location:** `engine/audio/src/SoundClip.cpp:14-43`
- **Evidence:** `DecodeClip` reads `kChunk` frames in a loop and appends them with `clip.samples.resize(offset + kChunk)` until the decoder stops. No maximum duration or sample count exists. A small MP3 or FLAC file can declare hours of audio.
- **Impact:** One small sound file in content can make the game or editor allocate gigabytes, or abort with `bad_alloc`.

### SEC-012 — The native file system and template copy follow symlinks, check no containment and cap no read size
- **Severity:** Low
- **Confidence:** High
- **Category:** Path traversal (defence in depth)
- **Location:** `engine/platform/src/FileSystem.cpp:55-108`, `engine/editor/src/Projects.cpp:45`
- **Evidence:** `NativeFileSystem::Resolve` rejects `..` and `:` lexically but follows symlinks on both read and write. `ReadFile` allocates `file_size` bytes with no limit. `CreateProject` calls `std::filesystem::copy(..., recursive)` without `copy_symlinks`, so a template that contains a symlink copies the link target's contents into the new project.
- **Impact:** A project folder with a symlink such as `content/x -> ~/.ssh` can have the editor read or overwrite files outside the project (autosave, CSG meshes, prefab writes). A template can copy private files into a new project that may later be shared.

### SEC-013 — Scene save restore applies transforms without checking they are finite
- **Severity:** Low
- **Confidence:** High
- **Category:** Save-file tamper tolerance
- **Location:** `engine/scene/src/SceneSave.cpp:89-121`
- **Evidence:** `RestoreSceneState` copies the `position`, `rotation` and `scale` it reads into `LocalTransformComponent` with no `isfinite` or unit-quaternion check. Values go through `Values().Set`, which does validate.
- **Impact:** An edited or corrupted save injects NaN, Inf or degenerate transforms, which then spread to rendering, triggers and navigation. This is a robustness problem, not code execution.

### SEC-014 — Windows module loading uses the default DLL search order for dependencies
- **Severity:** Low
- **Confidence:** Medium
- **Category:** DLL search-path hijacking
- **Location:** `engine/platform/src/DynamicLibrary.cpp:43-55`
- **Evidence:** `LoadLibraryW(widePath)` is called without `LoadLibraryExW` flags (`LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`) and without `SetDefaultDllDirectories`. The module's own path is absolute, because it is the shadow copy. Its imported DLLs are resolved through the standard order: application directory, system directories, current directory, then `PATH`.
- **Impact:** A planted DLL in the working directory or a writable `PATH` entry can be loaded into the editor or game.

### SEC-015 — The editor writes its ImGui layout to a fixed file name in the shared temp directory
- **Severity:** Low
- **Confidence:** High
- **Category:** Insecure temporary files
- **Location:** `tools/editor/EditorApp.cpp:90`
- **Evidence:** `m_iniPath = temp_directory_path() / "interi_editor_layout_v10.ini"`, which ImGui reads and rewrites. On Linux this is `/tmp/interi_editor_layout_v10.ini`, a name shared by every user. `fs.protected_symlinks`, on by default in most distributions, blocks the classic symlink clobber in sticky `/tmp`. Another user can still pre-create the file and feed the editor a crafted layout.
- **Impact:** Minor: layout tampering or denial of service between users. The same pattern also appears in SEC-003 and SEC-005.

### SEC-016 — The Tracy client in profile builds listens and broadcasts on all interfaces
- **Severity:** Low
- **Confidence:** High
- **Category:** Network exposure (development builds)
- **Location:** `engine/core/CMakeLists.txt:55-58`, `vcpkg.json` (`tracy`, features `on-demand`), `external/vcpkg/buildtrees/tracy/src/v0.14.1-a7372d1a4f.clean/public/common/TracySocket.cpp:430-461`, `external/vcpkg/buildtrees/tracy/src/v0.14.1-a7372d1a4f.clean/public/client/TracyProfiler.cpp:1914-1934`
- **Evidence:** `INTERI_PROFILING=ON` (the `*-profile` presets) links `Tracy::TracyClient`. The build defines neither `TRACY_ONLY_LOCALHOST` nor `TRACY_NO_BROADCAST`. `addrinfo_and_socket_for_family` therefore passes `AI_PASSIVE` with a null host, which binds to all interfaces over IPv6 and IPv4, unless the user sets the environment variable `TRACY_ONLY_LOCALHOST=1`. The client also broadcasts its presence to `255.255.255.255`.
- **Impact:** Anyone on the LAN can connect to a running profile build and read its profiling data (symbols, zone names, and possibly source text when the client sends it). This affects development builds only, because `INTERI_SHIPPING` compiles profiling out.

### SEC-017 — CI references actions by mutable tags and uses an unpinned container image
- **Severity:** Low
- **Confidence:** High
- **Category:** CI supply chain
- **Location:** `.github/workflows/ci.yml:31,34,58,73,102`
- **Evidence:** The workflow uses `actions/checkout@v7`, `actions/cache@v6`, `actions/upload-artifact@v7` and the third-party `ilammy/msvc-dev-cmd@v1` by tag, and `container: fedora:44` by tag rather than digest. The good parts: `permissions: contents: read` is set at the top, there is no `pull_request_target`, and no secrets are referenced. The nightly asan/tsan job follows the same pattern.
- **Impact:** If an action or the image tag were compromised upstream, CI would run that code. The damage would be limited by the read-only token.

### SEC-018 — Positive observations (no action)
- **Severity:** Info
- **Confidence:** High
- **Category:** Observations
- **Location:** `.github/workflows/ci.yml:3-4`, `tools/editor/EditorApp.cpp:1187-1192,1258-1260`, `engine/runtime/src/SaveGame.cpp:65-91`, `engine/assets/src/Skeleton.cpp:161-174`, `engine/asset_import/src/ImportUtil.cpp:101-113`, `engine/sdk/src/Project.cpp:24-42`, `engine/sdk/src/Manifest.cpp:63-70,643-650`
- **Evidence:**
  - Committed secrets: none found by the key/token/password regex sweep of tracked files, and the git history contains no key or credential files.
  - Network and multiplayer: first-party code has no socket, HTTP or `recv` usage, so no multiplayer trust boundary exists yet.
  - Python tools: no use of `subprocess`, `eval`, `pickle`, `yaml.load` or `extractall`.
  - `RunShell` runs the build command through `popen`. The user types that command in the Module Manager panel. It is not read from project or module files, so this is not an injection vector.
  - Save and cooked-file hashes are XXH3. They detect corruption only and are not tamper-proof. That is acceptable for single-player saves, but they must not be relied on for authenticity.
  - Input validation that is already sound:
    - `ResolveReference` and `NormalizeVirtualPath` correctly reject `..` and `:` after URI decoding.
    - `project.json` paths are confined to the project folder.
    - The manifest `library` field must be a plain base name.
    - Every `GetString` caller in `Manifest.cpp` checks `contains` first, even though `GetString` itself doesn't test for `end()`.
    - The texture, mesh, skeleton and clip validators bounds-check indices.
    - The JSON parsers use the non-throwing `parse(text, nullptr, false)`.
  - `ValidateSkin` doesn't range-check joint indices whose weight is 0. That is latent only, because no consumer reads `.iskin` yet.
- **Impact:** None today.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes; indexed @4631f35 = HEAD |
| 2 | cypher | Community by symbolCount LIMIT 30 | Top 30 are almost all third-party (SDL Joystick/Hidapi, Tracy Profiler, Manifold, ImGui docking); no first-party cluster in the top 30 |
| 3 | cypher | Process LIMIT 30 | All 30 are SDL3 buildtree flows (SDL_AppIterate/SDL_AppInit/CreateWindow); none first-party |
| 4 | query | "file load parse binary archive reader untrusted" | Only third-party hits (miniz, nlohmann binary_reader, ufbx fbxdom, tinyobj ObjReader); BM25 weak |
| 5 | query | "load module shared library dynamic library open" | ModuleHost, ModuleHost::OpenLibrary/Open, DynamicLibrary::Open, EditorModules, ProjectModules, EditorAppDesc |
| 6 | query | "save game slot load restore" | SaveGame.h/SaveRegistry::Apply, SaveSlots, ReadSave/Write/List, GameSession::SaveToSlot/LoadFromSlot |
| 7 | context | OpenLibrary (ModuleHost.cpp) | 1 caller (ModuleHost::Open); reads ModuleApi.abiVersion/toolchain; led to the shadow-copy review (SEC-003) |
| 8 | context | LoadFromSlot (GameSession.cpp) | No indexed callers (public API); calls StartScene and MakeError; save path → SaveSlots::Read → ReadSave |
| 9 | cypher | Functions in engine/ named Read*/Parse*/Load* | 150 parser entry points enumerated (assets, nav, chain, scene, hud, loc, sdk, values, anim, runtime); used as the trust-boundary inventory |
| 10 | impact | FromData (engine/nav/src/NavMesh.cpp), upstream | 2 direct callers: NavMesh::Read (file input) and Bake; risk LOW, epistemic exact (SEC-001) |
| 11 | cypher | Callers of ParseBinary/ParseUnary/ParsePrimary/Eval in Expression.cpp | Self-recursion ParseUnary→ParseUnary, Eval→Eval, ParsePrimary→ParseBinary; no depth guard (SEC-006) |
| 12 | context | RunShell (tools/editor/EditorApp.cpp) | 1 caller: RunModuleLoop; command comes from a UI field only (SEC-018) |

## Analysis limitations
- This was a static review only. Nothing was built or run, so the hardening state of real binaries (checksec, dumpbin `/DEPENDENTS`/`/HEADERS`) could not be checked (SEC-007).
- The status of dependency CVEs (stb_image, tinyobjloader rc13, miniaudio 0.11.25, cgltf 1.15, ufbx 0.23.1, recastnavigation 1.6.0, SDL3 3.4.18, Tracy 0.14.1, nlohmann-json 3.12.0, imgui) is based on knowledge as of 2026-10-19 and was not checked against a live advisory database. Confirm with OSV-Scanner or a vcpkg SBOM scan.
- `modules/` (Jolt Physics Engine, Destruction Module, including its `com.interi.destruction:state` save section) is not indexed and is out of scope.
- The index has no PDG layer, so `explain` and `pdg_query` taint analysis was unavailable. Source-to-sink paths were traced by hand.
- The GitNexus BM25 index had not been built, so concept queries mostly returned vendored code. Cypher name filters over `engine/` were used instead.
- Exploitability of SEC-001 and SEC-002 is inferred from reading the code. No proof-of-concept files were created.

## Cross-discipline notes
- C++/Robustness: `engine/` has no `catch` blocks. Any throw while handling malformed input (`std::bad_alloc` from SEC-010 or SEC-011, nlohmann `type_error` from an unchecked `get<>`, or `std::filesystem` calls without `error_code`, as in `EditorServices.cpp:142` and `EditorApp.cpp:90`) terminates the process.
- Testing: the libFuzzer and stress fuzz harness (`tests/asset_import/fuzz`) should be extended to the runtime formats (see SEC-008).
- Build/CI: no SBOM or dependency-scanning step exists in `.github/workflows/ci.yml`. The license check (`ctest -L license`) does not cover vulnerabilities.
