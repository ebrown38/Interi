# Content-Pipeline-Tools — Remediation

How to move from the issues in Issues.md to the end state in Goal.md.
Baseline plan written before milestone M-01. These are instructions, not changes.

**Status update:** CPT-009 was applied by WI-002 (milestone M-01); the steps below for it are kept for reference. Everything else here is unapplied.

## Recommended order
1. CPT-009, CPT-007, CPT-013: small, local fixes to correctness and safety in the existing database and watcher.
2. CPT-002: a headless cooker CLI. Every later item gets a CI gate through it.
3. CPT-008, CPT-016, CPT-014, CPT-015: make the cook scale and keep its state honest before more content arrives.
4. CPT-011: open the database to module-owned kinds. This is needed before CPT-001 (packaging) and CPT-012 (nav bake).
5. CPT-010, CPT-003: guid-based references and material references in scenes. Both change scene data, so do them together, with one migration.
6. CPT-005, CPT-006, CPT-017: texture usage inference, platform profiles and new formats, then mesh LODs.
7. CPT-012, CPT-004: nav bake and skeletal conversion as new cookers on the CPT-011 interface.
8. CPT-001: the cook-for-platform and package step that consumes all of the above.

## CPT-001 — No shipping cook or package step
- **Severity / Effort:** High / XL
- **Depends on:** CPT-002, CPT-011, CPT-010; benefits from CPT-006
- **Steps:**
  1. Define a "cooked project" layout: cooked scenes (binary scene v2 or the JSON pre-validated), compiled chain programs (or pre-validated chain JSON plus a hash), `CookData` IVAL for Values/tables in place of `project_data.json`, cooked anim sets, `.inav`, `.itex/.imat/.imesh`, and a manifest mapping guid to offset.
  2. Add `interi_cook --project <dir> --platform <pc|...> --out <dir>` (see CPT-002) that walks the startup scene and everything it references (dependency closure via the AssetDatabase and the scene/chain/HUD references), cooks it and writes a package (start with a flat folder plus `package.json`; add a single-file archive with an offset table later).
  3. Give `GameSession` a `GameDesc::cooked` mode. In that mode it mounts the package and reads `ReadCookedData` instead of `LoadProjectData`, and loads cooked forms. Keep the authoring path for play-in-editor.
  4. Leave `.meta`, `imported/` intermediates, editor-only reflection names and source models out of the package.
- **Illustrative change (not applied):**
  ```cpp
  // GameSession::Open
  if (desc.cookedPackage) {
      INTERI_TRY(session->m_files.Mount("", *OpenPackage(desc.cookedPackage)));
      INTERI_TRY_ASSIGN(cooked, values::ReadCookedData(bytes));   // replaces LoadProjectData
  }
  ```
- **Verification:** an `integration` test cooks `samples/modules/starter/templates/coins` and runs `interi_starter_sample_tests` against the package. The package contains no `.json` authoring file that the cooked loader does not need, and two cooks produce byte-identical output.
- **Risks / trade-offs:** the cooked and authoring paths can drift. Keep one loader per format with a "pre-validated" fast path, rather than two parsers.

## CPT-002 — No headless or CLI cook
- **Severity / Effort:** High / M
- **Depends on:** None
- **Steps:**
  1. Add `tools/cook/main.cpp` (`interi_cook`) linking `interi::asset_import`. It runs `Scan`, optionally `Prune`, and `CookAll`, prints the `CookReport`, and exits non-zero on any `failed` entry (and on warnings with `--werror`).
  2. Add `--jobs N` (after CPT-008), `--root`, `--imported` and `--json-report <file>` options.
  3. Register an `integration` CTest that cooks the import fixtures (`ImportFixtures.h`) in a temp dir, then cooks again and expects all skipped.
  4. Install it with the `Interi` package so module repos (destruction) can call it from their CI.
- **Verification:** `ctest -L integration -R Cook_` passes in Linux CI with no display. A deliberately broken fixture makes the tool exit with code 1.
- **Risks / trade-offs:** the tool must not depend on `editor_ui` or `rhi_vulkan`, so keep thumbnails out of it.

## CPT-003 — Cooked textures and materials never reach rendering
- **Severity / Effort:** Medium / L
- **Depends on:** CPT-010; the Rendering discipline's UV and material work
- **Steps:**
  1. Add a `material` AssetRef (or per-slot `materials` list) to `MeshComponent`. An empty `colors` entry means "use the material"; a colour is an explicit override only when a per-slot override marker says so. Bump the scene component version and migrate existing scenes: a legacy instance's slot colour that equals the referenced material's cooked `baseColor` (a copy `InstantiateModel` made) becomes unset; a colour that differs is kept with the override marker, so genuinely authored overrides survive.
  2. Change `InstantiateModel` to store material references (guid or path) and leave `colors` unset instead of copying `baseColor`, so later `.material.json` edits propagate to every instance without an override.
  3. In the editor renderer, resolve material to texture: load `.itex` through a texture cache that uploads with `UploadTexture` per mip and passes bindless indices to `ForwardRenderer`. Invalidate on `AssetWatcher` cooked paths.
- **Verification:** an editor test edits a `.material.json` base colour, cooks, and checks that a previously instantiated model (new and migrated legacy) reflects the new colour, while a slot with an authored override keeps it. A golden image shows a textured cube once UVs land.
- **Risks / trade-offs:** blocked partly by the renderer having no UV path. The material reference can land first, with base colour only.

## CPT-004 — Imported skeletons, clips and skins are orphaned
- **Severity / Effort:** Medium / L
- **Depends on:** CPT-011 (if done as a cooker step)
- **Steps:**
  1. Choose one runtime skeletal format. Either have `engine/anim` read `.iskel/.ianim` directly (binary, already versioned and hashed), or add `ConvertSkeleton(assets::Skeleton) -> anim::Skeleton` and `ConvertClip` and emit `.skeleton.json/.clip.json` as extra model products.
  2. Let `InstantiateModel` attach an `AnimatorComponent` and skin binding when the manifest mesh has `skin`.
  3. Add round-trip tests: import the fixture glTF with a skin, sample the clip, and compare joint matrices with the glTF reference within tolerance.
- **Verification:** `Model_SkinnedImport_PlaysInAnim` test passes. Grep shows `ReadSkeletonAsset` or the converter used outside tests.
- **Risks / trade-offs:** anim requires nlerp, bit-identical maths. The converter must not introduce trigonometry.

## CPT-005 — Texture usage is not inferred
- **Severity / Effort:** Medium / M
- **Depends on:** None
- **Steps:**
  1. Record usage while cooking materials and models: `ResolveTexture` receives the slot (BaseColor, Emissive → sRGB; Normal → linear BC5; MetallicRoughness/Occlusion → linear, BC1 or BC5). Store a `usage` in the referenced texture's record.
  2. Give `TextureImportSettings` an `Auto` mode (default) that picks colour space and compression from the recorded usage. Explicit settings in `.meta` still win.
  3. Compress embedded images by the same rule (`AssetDatabase.cpp:576`).
  4. Warn when one texture is used as both colour and data.
- **Verification:** an AssetDatabase test cooks a glTF with an external `normal.png` and asserts `.itex` format Bc5 and Linear. Embedded images are compressed.
- **Risks / trade-offs:** usage depends on cook order, so do a usage pass over all materials before cooking textures, or re-cook textures when their usage changes (include usage in the texture's key).

## CPT-006 — No platform or target dimension in cooking
- **Severity / Effort:** Medium / L
- **Depends on:** CPT-002
- **Steps:**
  1. Add `AssetDatabase::Options::platform` (a profile name and a format table), include it in `ComputeKey` and in the output root (`imported/<platform>/...`), or keep platform-neutral intermediates plus per-platform cooked output.
  2. Add BC7 (colour+alpha) and BC6H (HDR) encoders. A permissively licensed encoder (e.g. bc7enc_rdo, MIT/Unlicense) is an option, subject to `license_policy.json`. Add ASTC only if a target needs it.
  3. Put per-platform overrides (max size, format) in `.meta` settings.
- **Verification:** cooking the same project for two profiles gives different `.itex` formats and separate keys. Decode-error tests with error bounds cover BC7 and BC6H, as they do for BC1/4/5.
- **Risks / trade-offs:** a new dependency triggers license notices. Encoder speed matters, so pair this with parallel cooking (CPT-008).

## CPT-007 — AssetWatcher drops edits made during a cook
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. Record the snapshot before `Scan()`, not after the cook. Then filter out the paths the cook itself wrote (product paths plus `.meta` files) when computing the next change set, instead of re-baselining blindly.
  2. Alternatively, make `FileWatcher::Poll` return the changed paths. `AssetWatcher` ignores those under `importedFolder` or ending in `.meta` that match the cook's writes, and re-queues any others.
- **Verification:** a test with a `GuidGenerator` or importer hook that modifies a second source during `CookAll` sees that source re-imported on the next `Update()`.
- **Risks / trade-offs:** a user editing a `.meta` during a cook could be missed, so compare against the bytes the cook wrote.

## CPT-008 — Full Scan, re-hash and serial CookAll on the UI thread
- **Severity / Effort:** Medium / L
- **Depends on:** CPT-007
- **Steps:**
  1. Cache `(size, mtime) → content hash` per source, reads file and product in memory, and optionally in a gitignored `imported/.cookcache`. Re-hash only when size or mtime changed. Content hashing remains the authority.
  2. Have `FileWatcher` report changed paths, and cook only the changed records plus `Dependents(path, true)`.
  3. Run cooks on Low-priority jobs (like `AsyncFileReader`), apply results on the main thread, and cook independent records in parallel (models write disjoint product folders).
  4. Ignore editor-owned files (`*.autosave`, `.scene.json` saves) in the asset watcher.
- **Verification:** a benchmark with 5,000 dummy sources: a no-op `Update()` after touching one unrelated file costs under 5 ms, and a single texture change re-hashes only that texture. TSan stays clean for the job-based cook.
- **Risks / trade-offs:** mtime caches can be fooled by tools that preserve mtime, so keep content hashing at cook time and the full `--verify` mode in the CLI.

## CPT-009 — Cook and Prune delete unvalidated paths from `.meta`
- **Severity / Effort:** Medium / S
- **Depends on:** None
- **Steps:**
  1. In `ReadMeta`, drop any product whose path fails `NormalizeVirtualPath` or does not start with `importedFolder + "/"`, and add a scan warning.
  2. Route deletes through a checked helper (`RemoveUnderImported(path)`) and never call `fs::remove(root / untrusted)` directly.
- **Illustrative change (not applied):**
  ```cpp
  const auto norm = NormalizeVirtualPath(product.path);
  if (!norm || !norm->starts_with(options.importedFolder + "/")) { warnings.push_back(...); continue; }
  ```
- **Verification:** a test with a `.meta` listing `/tmp/x` and `../outside.txt` products: after Cook and Prune, both sentinel files still exist and warnings are reported.
- **Risks / trade-offs:** none significant.

## CPT-010 — Path-string asset identity in scenes
- **Severity / Effort:** Medium / L
- **Depends on:** CPT-002 (for a migration tool)
- **Steps:**
  1. Extend `values::AssetRef` with an optional `guid` (serialised as `{"guid": "...", "path": "..."}`, path kept as a hint). Bump the Values archive version and accept the old string form.
  2. Add an asset registry (from the AssetDatabase in the editor, from the package manifest at run time) that resolves a guid to its current path. Fall back to the path if the guid is unknown.
  3. Have `InstantiateModel`, the asset browser drag and drop, and the Material Editor write guids.
  4. Add a "Find references / fix up" tool and a redirect table for deleted or merged assets.
- **Verification:** a test instantiates a model, moves the source and its `.meta` to another folder, re-cooks, reopens the scene, and the mesh still resolves.
- **Risks / trade-offs:** this is a format change across scenes, prefabs, chains and HUD documents, so migrate them in one milestone.

## CPT-011 — The AssetDatabase is closed to module-owned kinds
- **Severity / Effort:** Medium / L
- **Depends on:** None
- **Steps:**
  1. Replace `SourceKind` with an importer registry: `{ id, extensions, version, settingsSchema (values), cook(reader, record, outputs) }`. The built-ins (model, texture, material) register at startup.
  2. Expose registration through `IEditorExtensions` (editor) and through the cooker CLI's plugin loading (module libraries via `ModuleHost`). Include each importer's id and version in the cook key.
  3. Extend `AssetKind` similarly, so the browser lists module products.
  4. Move the destruction `.idest` cook onto it, in the module's repo.
- **Verification:** a test module registers a `.testasset` importer, and Scan, Cook, Dependents and Prune all handle it. The reload or unload of the module removes the importer (same rule as other module entries).
- **Risks / trade-offs:** module code runs in the cook, so the trust model is the same as the Module Manager's and should be documented.

## CPT-012 — No navmesh bake tool
- **Severity / Effort:** Medium / M
- **Depends on:** CPT-011 (preferred), CPT-002
- **Steps:**
  1. Add a nav cooker that takes a scene, collects `MeshComponent`/`PrimitiveComponent` world triangles (optionally filtered by a `NavWalkable` tag) and `NavBakeSettings` from project data, and writes `content/navmesh.inav`.
  2. Record the scene and its mesh assets as reads, so the bake re-runs when they change.
  3. Add an editor "Bake navigation" button plus a debug overlay of the polygons.
- **Verification:** the starter or a test project bakes `navmesh.inav` from CLI. `GameSession` loads it with no warning, and an agent reaches a target.
- **Risks / trade-offs:** Recast output differs per toolchain, so cook on the target toolchain or ship one cooked mesh, as documented.

## CPT-013 — The editor drops warnings from watcher cooks
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. In `EditorApp::UpdateServices`, log `outcome.report.warnings` with `INTERI_LOG_WARN("Editor", "import: {}", w)`, matching `OpenAssetDatabase`.
  2. Optionally surface them in the Import panel next to the asset.
- **Verification:** re-export a model with a missing texture while the editor runs, and a warning appears in the console panel.
- **Risks / trade-offs:** none.

## CPT-014 — `.meta` version unchecked; derived state in the sidecar
- **Severity / Effort:** Low / M
- **Depends on:** CPT-008 (where the cache lives)
- **Steps:**
  1. Read `version` in `ReadMeta`. For a version newer than supported, keep the record read-only, warn, and never rewrite the file.
  2. Move `cook` (key, products, reads, references) to a gitignored `imported/<source>.cook.json` or the cache from CPT-008. Leave guid, importer and settings in the authored `.meta`.
- **Verification:** a test writes a `"version": 2` meta, and Scan warns without rewriting it. Re-cooking after an importer version bump does not change any `.meta`.
- **Risks / trade-offs:** existing `.meta` files need a one-time migration that drops the `cook` section and forces a re-cook.

## CPT-015 — The cook key ignores third-party importer versions
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Hash `CGLTF_VERSION` (if defined), `ufbx_source_version`, `MESHOPTIMIZER_VERSION`, `STBI_VERSION` (or the vcpkg port versions written into a generated header at configure time) into `ComputeKey`.
- **Verification:** a unit test asserts that the key changes when the generated version header changes (inject it through a seam).
- **Risks / trade-offs:** baseline bumps cause a full re-cook, which is intended.

## CPT-016 — Scan has no include or exclude rules
- **Severity / Effort:** Low / S
- **Depends on:** None
- **Steps:**
  1. Add `Options::include` (default: the project's `content` folders when opened as a project, else root) and `Options::exclude` (default: hidden folders, `imported`, module dirs, `build*`).
  2. Use them in `Scan`, `Prune` and `FileWatcher` roots.
- **Verification:** a test with a `.hidden/x.png` and a `modules/m/tex.png` under the root shows no `.meta` created for either.
- **Risks / trade-offs:** projects that keep assets outside `content` need to list them.

## CPT-017 — No mesh LOD, meshlet or overdraw processing
- **Severity / Effort:** Low / M
- **Depends on:** CPT-006 (platform profiles decide the LOD count)
- **Steps:**
  1. Add `ModelImportSettings::lods` (count, target error). Generate it with `meshopt_simplify` and apply `meshopt_optimizeOverdraw` per LOD.
  2. Bump `kMeshVersion` to 3 with an optional LOD array guarded by `archive.Version() >= 3`.
- **Verification:** importing a dense fixture with `lods=3` yields 3 index ranges with non-increasing triangle counts. Older `.imesh` v2 files still read.
- **Risks / trade-offs:** meshoptimizer simplification is deterministic, but pin the version (see CPT-015).

## CPT-018 — Sub-asset guids depend on DCC names (Info)
No action required. Document the rule in CLAUDE.md or the artist docs, and revisit it alongside CPT-010 (a redirect table covers renames).
