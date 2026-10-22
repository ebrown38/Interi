# Content-Pipeline-Tools — Issues

**Project:** Interi @ 4631f35
**Analyzed:** 2026-10-19
**Scope:** `engine/assets` (Guid, Texture, Material, Skeleton, private `AssetIo.h`), `engine/asset_import` (AssetDatabase, glTF/OBJ/FBX/texture importers, ModelProcess, ModelManifest), the cooked or authored formats in `engine/scene` (Mesh, SceneFile), `engine/values` (CookData / IVAL), `engine/nav` (.inav), `engine/anim` (.skeleton.json / .clip.json), `engine/game` (GameSession, ProjectModules: the runtime consumer), the editor asset paths (`engine/editor` Prefabs/InstantiateModel, Thumbnails, AssetBrowser; `engine/editor_ui` extension API), `tools/` (editor, module_check, new_module, license_check), `data/`, `samples/modules/starter/templates`, `tests/asset_import` and `tests/assets` (test lists only), `vcpkg.json`, `.gitignore`. I only skimmed `modules/destruction/README.md` (a separate, unindexed repo) for context and did not audit it.
**Summary:** 18 issues — Critical: 0 · High: 2 · Medium: 10 · Low: 5 · Info: 1

The importer and container layer is solid. Every cooked file is a magic+version+XXH3 container that is read all-or-nothing (`engine/assets/src/AssetIo.h:20-55`). Cook keys are content hashes that include importer/format versions, settings, the recorded reads and the referenced texture guids (`AssetDatabase.cpp:408-437`). Sub-asset guids are stable, importers are fuzzed, and `AssetDatabaseTests.cpp` has 26 tests. The problems are in the stages after import: shipping, CI and per-platform cooking, getting cooked data to the runtime, and the editor's watch loop.

## Findings

### CPT-001 — No shipping cook or package step: the game runtime reads authoring files from the project folder
- **Severity:** High
- **Confidence:** High
- **Category:** Pipeline shape / packaging
- **Location:** `engine/game/src/GameSession.cpp:25-38`, `engine/game/src/GameSession.cpp:71`, `engine/game/src/ProjectModules.cpp:52-60`, `engine/scene/src/SceneRuntime.cpp:79,128`, `engine/anim/src/Animator.cpp:314-336`, `engine/values/include/interi/values/ProjectData.h:121-125`
- **Evidence:** `GameSession::Open` mounts the raw project directory (`m_files.Mount("", *NativeFileSystem(project.directory))`). It then parses `project.json`, every `module.json`, the project data JSON (`values::LoadProjectData(text, ...)`), the `.scene.json` (`scene::LoadScene`), and each `.chain.json` / `.chainfn.json` (`chain::LoadChain`, `LoadChainFunction`) at run time. Animation sets are parsed from `.skeleton.json` / `.clip.json` (`ParseSkeleton`, `ParseClip`). `CookData`, `WriteCookedData` and `ReadCookedData` (the IVAL cooked Values format) have no caller outside `engine/values` and its tests: a grep of `engine/`, `tools/` and `samples/` finds only their own header and source. `project.content` (`sdk/Project.h:43`) is used only to create folders (`engine/editor/src/Projects.cpp:77-80`), never to choose what ships. Nothing in `tools/` packages a project.
- **Impact:** A shipped game would contain the editable sources, the `.meta` sidecars and the editor-written `imported/` tree. It would pay JSON parsing, schema validation and chain compilation on every load, and could not strip editor-only data. Cooked formats that already exist (IVAL) are dead code. This blocks the console backends (PS5/Xbox) and any "ship a build" milestone.

### CPT-002 — No headless or CLI cook: only the editor UI drives the AssetDatabase
- **Severity:** High
- **Confidence:** High
- **Category:** Build mechanics / CI
- **Location:** `tools/editor/EditorApp.cpp:569-600` (`OpenAssetDatabase`, called at lines 167 and 668), `tools/` (contents: bench_compare, editor, license_check, module_check, new_module, platform_audit), `tools/editor/main.cpp:61-120` (flags)
- **Evidence:** The only production caller of `AssetDatabase::Scan`/`CookAll` is `EditorApp::OpenAssetDatabase`, which runs when `level_editor` starts or opens a project. `level_editor` has no `--cook` flag (its flags are `--level`, `--screenshot`, `--video-driver`, `--frames`, `--validation`, `--modules`, `--project`, `--chain`, `--module-manager`, `--chain-breakpoint`, `--play`, `--module-libs`, `--project-file`, `--project-data`), and there is no cooker executable. A failed import only produces a log line (`INTERI_LOG_ERROR("Editor", "import {}", failure)`), so no process exits non-zero.
- **Impact:** CI cannot cook a project, check that every source imports, or produce cooked output without a window and GPU. Broken assets surface only when a person opens the editor. The cook cache cannot be shared between machines, and the samples' content is never cooked in CI. The Destruction Module ships its own `tools/fracture_cooker` CLI (`modules/destruction/README.md:18`), which shows the need.

### CPT-003 — Cooked textures and materials never reach rendering; model instancing bakes base colour at drop time
- **Severity:** Medium
- **Confidence:** High
- **Category:** Runtime contract
- **Location:** `engine/editor/src/Prefabs.cpp:191-252`, `engine/render/include/interi/render/ForwardRenderer.h:35`
- **Evidence:** Outside `engine/assets` itself, `LoadTextureAsset` is used only by `engine/editor/src/Thumbnails.cpp`. `LoadMaterialAsset` is used by Thumbnails, `AssetPanels.cpp` and `Prefabs.cpp`. No render or `tools/editor` code reads a `.itex`. The renderer's only texture input is a raw bindless index (`albedoTexture`). `InstantiateModel` reads each slot's `.imat` once and copies `baseColor` into the scene object's `MeshComponent.colors`. The scene stores no material reference (`Prefabs.cpp:228-242`).
- **Impact:** Texture compression, mips, the material's metallic/roughness/normal/emissive data and texture references are cooked and then discarded. A re-import or a Material Editor change never updates models already placed in scenes, because the colour was copied when the model was dropped. Content authors get no feedback, and fixing this later means migrating every saved scene.

### CPT-004 — Imported skeletons, clips and skins are orphaned (the importer and engine/anim use different formats)
- **Severity:** Medium
- **Confidence:** High
- **Category:** Pipeline shape (import to runtime gap)
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:620-643`, `engine/assets/src/Skeleton.cpp:184-223`, `engine/anim/src/Animator.cpp:314-336`
- **Evidence:** `CookModel` writes `.iskel`, `.ianim` and `.iskin` products. `ReadSkeletonAsset`, `ReadClipAsset` and `ReadSkinAsset` have no caller outside `engine/assets` and tests (grep). `engine/anim` contains no reference to `iskel`, `ianim` or `assets/Skeleton.h`, and loads its own `.skeleton.json` / `.clip.json` text formats. `InstantiateModel` ignores `manifest.skeletons`, `clips` and each mesh's `skin`. CLAUDE.md records this as "nothing converts these importer outputs".
- **Impact:** glTF and FBX animation cannot be used in a game. Animators must hand-write JSON skeletons and clips, so two incompatible skeletal formats are maintained. Skinned meshes placed in scenes render in bind pose.

### CPT-005 — Texture usage is not inferred: normal and data maps default to sRGB and uncompressed
- **Severity:** Medium
- **Confidence:** High
- **Category:** Per-platform variants / texture cooking
- **Location:** `engine/asset_import/include/interi/asset_import/TextureImport.h:35-41`, `engine/asset_import/src/AssetDatabase.cpp:460-476`, `engine/asset_import/src/AssetDatabase.cpp:544-587`
- **Evidence:** A standalone texture source gets `colorSpace = Srgb` and `compression = None` by default. `ResolveTexture` rewrites a material's texture reference to the cooked path and guid, but never feeds the slot (normal, metallic-roughness, occlusion) back into the texture's import settings. Embedded model images do get their colour space inferred, but are always imported with `{.colorSpace = ...}` only, so they are uncompressed RGBA8 with no BC5 for normals and no BC4 for masks (`AssetDatabase.cpp:576-577`).
- **Impact:** External normal and roughness maps are cooked with sRGB decoding (wrong shading once the renderer samples them), and every texture is 4 bytes per texel unless someone edits each `.meta` by hand. This multiplies memory use and disk size and depends on the artist remembering per-file settings.

### CPT-006 — No platform or target dimension in cooking; formats limited to BC1/BC4/BC5 and uncompressed Rgba32F HDR
- **Severity:** Medium
- **Confidence:** High
- **Category:** Per-platform variants
- **Location:** `engine/assets/include/interi/assets/Texture.h:22-30`, `engine/assets/include/interi/assets/Texture.h:88-101`, `engine/asset_import/src/TextureImport.cpp:110-128`, `engine/asset_import/src/AssetDatabase.cpp:408-437`
- **Evidence:** `TextureFormat` is Rgba8, Rgba32F, Bc1, Bc4 or Bc5. There is no BC3/BC7 for colour with alpha and no BC6H for HDR. HDR images always become `Rgba32F` (16 B/texel; `BuildTexture(image.hdr, generateMips)`). `ComputeKey` and `AssetDatabase::Options` have no platform or profile input, so there is one output per source under `imported/`.
- **Impact:** Alpha textures (foliage, decals, UI) and HDR environment maps cannot be compressed. A 4K HDR with mips is about 341 MB. Console backends (in private repos) will need different formats or alignment and have no place in the cook to request them. Adding that later changes every product path and cook key.

### CPT-007 — AssetWatcher drops source edits made while a cook runs
- **Severity:** Medium
- **Confidence:** High
- **Category:** Hot reload / incremental correctness
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:1012-1034`, `engine/hotreload/src/FileWatcher.cpp:17-24`
- **Evidence:** After `Scan()` + `CookAll()`, `Update` calls `(void)m_impl->watcher.Poll(); // the cook's own writes are not changes`. `Poll` replaces the stored snapshot with the current tree, so any file a user (or a DCC exporter) saves during the cook goes into the baseline without being reported.
- **Impact:** A texture or model saved while a long cook is running is not re-imported until some unrelated file changes. The editor shows stale content, which is hard to diagnose. Exporters that write several files in sequence (glTF + .bin + images) are especially likely to hit this.

### CPT-008 — Every file change triggers a full Scan, a full re-hash and a serial CookAll on the editor UI thread
- **Severity:** Medium
- **Confidence:** High
- **Category:** Build mechanics / editor scalability
- **Location:** `tools/editor/EditorApp.cpp:541-559`, `engine/asset_import/src/AssetDatabase.cpp:408-451`, `engine/asset_import/src/AssetDatabase.cpp:657-664`, `engine/asset_import/src/AssetDatabase.cpp:876-896`
- **Evidence:** `UpdateServices` calls `m_assetWatcher->Update()` every 0.5 s from the frame loop. On any change under the root (including scene saves and `<scene>.autosave`), it runs `Scan()` (walks the tree and parses every `.meta`) and then `CookAll()`. For each record, `CookAll` reads the whole source, re-reads and hashes every recorded `reads` file (`ComputeKey`), and re-reads and hashes every cooked product (`ProductsIntact`), all serially on the calling thread. No job-system use, mtime pre-check or per-file dirty set exists. `FileWatcher` also `stat`s the whole tree every poll.
- **Impact:** Editor hitches grow with total project bytes, not with the size of the change. On a production-size project (GBs of sources), every save or autosave freezes the UI for seconds, and real re-imports block the frame for the whole import.

### CPT-009 — Cook and Prune delete file paths taken from `.meta` without validating them
- **Severity:** Medium
- **Confidence:** High
- **Category:** Pipeline robustness (Security cross-note)
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:233-253`, `engine/asset_import/src/AssetDatabase.cpp:694-701`, `engine/asset_import/src/AssetDatabase.cpp:965-971`
- **Evidence:** `ReadMeta` accepts any string as `product.path`. A re-cook then runs `fs::remove(options.root / old.path, ignored)` for products not produced again, and `Prune` runs `fs::remove(impl.options.root / product.path, error)`. With `std::filesystem`, `root / "/abs/path"` yields `/abs/path`, and `..` segments are not rejected. Writes go through `NativeFileSystem` (normalised virtual paths), but deletes bypass it.
- **Impact:** A `.meta` that is hand-edited, mis-merged or hostile (shared projects, downloaded templates, mods) can make an ordinary re-import or Prune delete files outside the project.

### CPT-010 — Scenes reference cooked assets by path string; guids are not used for references and there are no redirects
- **Severity:** Medium
- **Confidence:** High
- **Category:** Asset identity
- **Location:** `engine/values/include/interi/values/Value.h:64-70`, `engine/scene/include/interi/scene/Mesh.h:80-86`, `engine/editor/src/Prefabs.cpp:240-241`, `engine/asset_import/src/AssetDatabase.cpp:392-395,530-537`
- **Evidence:** `AssetRef` is `{ std::string path; }`. `InstantiateModel` stores `AssetRef{mesh.mesh}`, the cooked path `imported/<source path>/<sanitised node name>.imesh`. Product paths are derived from the source path (`OutputRoot`) and the DCC node name. The derived guid (`DeriveGuid`) is written into the `.meta` but nothing resolves a guid to a path at load time.
- **Impact:** Moving or renaming a source file (which the `.meta` guid is designed to survive), or renaming a node in the DCC tool, silently breaks every scene that uses it. There is no redirect table and no "find references" tool, and the guid design gives no benefit at runtime.

### CPT-011 — The AssetDatabase is closed to module-owned asset kinds
- **Severity:** Medium
- **Confidence:** High
- **Category:** Tools architecture / extensibility
- **Location:** `engine/asset_import/include/interi/asset_import/AssetDatabase.h:35-40`, `engine/asset_import/src/AssetDatabase.cpp:258-273,670-681`, `engine/editor/include/interi/editor/AssetBrowser.h:15-26`, `engine/editor_ui/include/interi/editor_ui/EditorExtensionsApi.h:81-93`
- **Evidence:** `SourceKind` is the fixed enum {Model, Texture, Material}, dispatched in a `switch` in `CookRecord`. `AssetKind` is a fixed list of 8 kinds. `IEditorExtensions` lets modules add panels, tools and render features, but has no way to register an importer, cooker or asset kind. The Destruction Module therefore cooks `.idest` destructibles through its own panel and `tools/fracture_cooker` (`modules/destruction/README.md:18,40`), outside the cook key, `.meta` guids, dependency graph, watcher and Prune.
- **Impact:** Gameplay modules (destruction, navigation, audio banks, future VFX) each need a separate pipeline with its own incrementality and identity rules. Their outputs do not re-cook when shared sources (a glTF building) change, and they cannot be packaged with the rest (CPT-001).

### CPT-012 — Nothing in the editor or tools bakes a navmesh, but the game expects `content/navmesh.inav`
- **Severity:** Medium
- **Confidence:** High
- **Category:** Baking / tools coverage
- **Location:** `engine/game/include/interi/game/GameSession.h:43-45`, `engine/nav/include/interi/nav/NavMesh.h:62-70`, `tests/game/GameSessionTests.cpp:174`
- **Evidence:** `GameDesc::navMesh` defaults to the cooked `content/navmesh.inav`. `NavMesh::Bake` is called only in `tests/nav/*` and `tests/game/GameSessionTests.cpp`. No editor panel, `level_editor` flag or tool collects scene geometry and writes `.inav`. CLAUDE.md notes that Recast output is not cross-platform reproducible ("ship the cooked mesh").
- **Impact:** Designers cannot produce the navigation data the runtime reads, short of writing C++. The bake is also not tied to scene changes, so a cooked mesh goes stale silently when a level is edited.

### CPT-013 — The editor drops warnings from watcher-triggered cooks
- **Severity:** Low
- **Confidence:** High
- **Category:** Tool UX / error reporting
- **Location:** `tools/editor/EditorApp.cpp:546-558` (compare `:589-592`)
- **Evidence:** In `UpdateServices`, the cook loop logs `outcome.report.cooked` and `outcome.report.failed`, but not `outcome.report.warnings` (missing textures, dropped degenerate triangles, undecodable embedded images). Only the startup cook in `OpenAssetDatabase` logs warnings.
- **Impact:** During iteration (the common case), an artist who re-exports a model with a broken texture link gets no message. The asset just looks wrong.

### CPT-014 — `.meta` version is written but never checked; derived cook state is stored in the source-controlled sidecar
- **Severity:** Low
- **Confidence:** High
- **Category:** Asset identity / versioning
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:153-180`, `engine/asset_import/src/AssetDatabase.cpp:183-256`
- **Evidence:** `MetaToJson` writes `{"version", 1}`, and `ReadMeta` never reads it. An older editor reading a newer `.meta` keeps the fields it knows and rewrites the file as version 1. The `.meta` also holds machine-derived cook state (`cook.key`, product hashes, `reads`, `references`) next to the authored guid and settings.
- **Impact:** A future `.meta` schema change can be silently downgraded by a teammate on an older build, losing settings. Each importer or format version bump rewrites every `.meta` in the project, causing VCS churn and merge conflicts in files that are meant to be authored.

### CPT-015 — The cook key ignores third-party importer library versions
- **Severity:** Low
- **Confidence:** High
- **Category:** Incremental cooking correctness
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:412-414`, `engine/asset_import/include/interi/asset_import/AssetDatabase.h:32-33`, `vcpkg.json:6`
- **Evidence:** The key mixes `kImporterVersion` (a manual constant, 1) with the cooked-format versions only. cgltf, ufbx, tinyobjloader, stb_image and meshoptimizer versions (pinned by `builtin-baseline`) are not part of it.
- **Impact:** A vcpkg baseline bump that changes meshoptimizer ordering or ufbx conversion leaves stale cooked output that does not match a clean cook, unless someone remembers to bump `kImporterVersion`.

### CPT-016 — Scan has no include or exclude rules and writes `.meta` files anywhere under the root
- **Severity:** Low
- **Confidence:** High
- **Category:** Pipeline scope
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:771-840`
- **Evidence:** `Scan` walks the whole root recursively and skips only `importedFolder`. Every image or model it finds (including inside module directories, hidden folders, or test fixtures in a project folder) gets a `.meta` written next to it (`INTERI_TRY(impl.WriteMeta(record))`) and is cooked. `project.content` is not consulted.
- **Impact:** Opening a project folder that contains module sources or other repositories writes `.meta` files into them and cooks irrelevant files, polluting other git histories and slowing the startup cook.

### CPT-017 — No mesh LOD, meshlet or overdraw processing
- **Severity:** Low
- **Confidence:** High
- **Category:** Per-platform variants / mesh optimisation
- **Location:** `engine/asset_import/src/ModelProcess.cpp:505-551`
- **Evidence:** The meshoptimizer step runs only `meshopt_optimizeVertexCache` and `meshopt_optimizeVertexFetchRemap`. There is no `meshopt_simplify` LOD chain, no overdraw optimisation and no meshlet build. `.imesh` holds a single LOD.
- **Impact:** Large imported scenes have no LOD path, which matters once the GPU-driven renderer handles authored content at scale. Adding LODs later means a `.imesh` version bump.

### CPT-018 — Sub-asset guids depend on DCC node names and import order
- **Severity:** Info
- **Confidence:** High
- **Category:** Asset identity
- **Location:** `engine/asset_import/src/AssetDatabase.cpp:351-360`, `engine/asset_import/src/AssetDatabase.cpp:524-537`
- **Evidence:** `DeriveGuid(record.guid, kind, name)` hashes the sanitised and de-duplicated node name. Two nodes named `Mesh` become `Mesh` and `Mesh_2` in import order.
- **Impact:** This is expected behaviour, but renaming or reordering same-named nodes in the DCC tool changes sub-asset identity. Combined with CPT-010, this breaks references. Document it for artists.

## GitNexus exploration log
**Access:** mcp · **Index:** refreshed (@4631f35 = HEAD)

| # | Tool | Argument | Result (one line) |
|---|---|---|---|
| 1 | list_repos | — | Interi: 37,920 files, 365,792 nodes, 4,973 communities, 614 processes, indexed @ 4631f35 = HEAD |
| 2 | cypher | Community by symbolCount LIMIT 30 | All top-30 clusters third-party (Joystick, Hidapi, Profiler, Manifold, Tracy, Test...) |
| 3 | cypher | Process LIMIT 30 | All 30 SDL3 build-tree flows; no first-party flows |
| 4 | cypher | Process with entry under engine/ or tools/ | 7 flows: 5 `CookModel → ...` (AssetDatabase.cpp), 2 EditorApp facade |
| 5 | query | "cook asset import database" | AssetDatabase, Cook, AssetWatcher, MetaToJson, ReadMeta, EditorApp::OpenAssetDatabase/UpdateServices, ImportPanel |
| 6 | query | "texture encode BC1 mip" | BuildTexture, EncodeBc1, DecodeMip, ValidateTexture, TextureCompression, Thumbnails TextureReader |
| 7 | query | "hot reload asset watcher" | Mostly third-party; LibraryWatcher, ModuleApi DeclareComponent; no asset hot-reload flow |
| 8 | context | Cook (AssetDatabase.cpp) | 0 incoming resolved, calls MakeError only (dispatch to Impl not resolved) |
| 9 | impact | ReadMesh upstream | Not found (name is ReadMeshAsset) |
| 10 | cypher | CALLS into cooked readers (LoadMesh, ReadTexture, CookData...) | 0 rows: names wrong; refined below |
| 11 | impact | AssetDatabase (Class) upstream | 0 callers, risk UNKNOWN; confirmed by text search (only tools/editor) |
| 12 | cypher | Function names Read*/Load*/Cook*/Write* under engine/ | 127 rows: format readers/writers per module (Texture, Material, Skeleton, Mesh, CookedData, nav, anim, hud, chain) |
| 13 | context | BuildTexture | Ambiguous (Image8 / ImageF overloads) |
| 14 | cypher | CALLS into Load/Read*Asset, CookData, ReadCookedData (non-test) | Only self-wrappers (LoadXAsset → ReadXAsset); no runtime consumers; confirmed by grep |
| 15 | context | BuildTexture (Image8 uid) | Calls EncodeBc1/Bc4/Bc5, GenerateMips; 0 incoming resolved |

The index has no BM25 (keyword queries are slow and weak), and caller edges into `engine/assets` were often unresolved. I used grep to confirm or refute every "no caller" claim (CPT-001, -003, -004, -012) and did not rely on a graph zero.

## Analysis limitations
- Static reading only. I did not build, run `level_editor`, cook a project or measure cook or hitch times, so the scale estimates in CPT-008 are inferred from the code paths.
- I did not audit `modules/destruction` and `modules/physics` (gitignored, separate repositories, not indexed). The destructible authoring, fracture baking and validation checklist (skill §4) is out of scope for this repo; CPT-011 covers only the engine-side integration gap.
- The console backends are in private repos, so I could not check per-platform requirements (CPT-006).
- GitNexus: the BM25 index is missing and third-party code dominates clusters and processes, so I discovered first-party code mainly through filtered `cypher` and targeted reads.

## Cross-discipline notes
- Security: `.meta`-driven `fs::remove` without path validation (`engine/asset_import/src/AssetDatabase.cpp:699,967`) is a delete-outside-root primitive for untrusted projects (CPT-009).
- Performance-Optimization: synchronous Scan+CookAll and whole-tree `stat` on the editor frame thread (`tools/editor/EditorApp.cpp:541-559`, `engine/hotreload/src/FileWatcher.cpp:17-60`).
- Rendering: the renderer has no UV or material-texture path (`ForwardRenderer.h:35`), so cooked `.itex`/`.imat` content cannot be shown (CPT-003).
- Animation: `engine/anim` defines JSON skeleton and clip formats that duplicate `engine/assets/Skeleton.h` (CPT-004).
