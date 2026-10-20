# Content-Pipeline-Tools — Goal State

## Vision
Any Interi project can be cooked headlessly, in CI or on a workstation, by one deterministic, incremental and parallel cooker. It runs the same code as the editor and turns sources into per-platform runtime formats. A shipped game loads a cooked package, not authoring JSON or `.meta` files. Assets are referenced by stable guids that survive moves and renames. Modules (destruction, navigation and others) plug their own importers into the same database, so their outputs share cook keys, dependencies, hot reload and packaging. In the editor, a source save re-cooks only what changed, off the UI thread, and every warning reaches the artist.

## Acceptance criteria
- [ ] AC-1 `interi_cook --project samples/modules/starter/templates/coins --platform pc --out <dir>` produces a package. `GameSession` runs the starter sample from it in an `integration` test, and the package contains no `.meta`, `project_data.json` or source model or image file. (resolves CPT-001)
- [ ] AC-2 `interi_cook` runs in Linux CI without a display, exits non-zero when any source fails to import, and a second invocation reports every source as skipped. (resolves CPT-002)
- [ ] AC-3 Two cooks of the same inputs on the same toolchain produce byte-identical packages (hash compared in a test). (resolves CPT-001, CPT-015)
- [ ] AC-4 A model instanced in a scene references its materials. Editing the `.material.json` and re-cooking changes the rendered or listed colour of the existing instance without re-dropping it. (resolves CPT-003)
- [ ] AC-5 A skinned glTF fixture imports, and its clip plays through `engine/anim` (by direct `.iskel/.ianim` support or a converter), with joint matrices within tolerance of the glTF reference. (resolves CPT-004)
- [ ] AC-6 An external `normal.png` referenced as a material's normal texture cooks to a Linear BC5 `.itex` with default settings, and embedded images are compressed by the same rule. (resolves CPT-005)
- [ ] AC-7 The cook takes a platform profile that is part of the cook key, and BC7 (alpha) and BC6H (HDR) encoders exist with decode error-bound tests. (resolves CPT-006)
- [ ] AC-8 A test that changes a second source during `CookAll` sees it re-imported on the next `AssetWatcher::Update()`. (resolves CPT-007)
- [ ] AC-9 With 5,000 sources, an `AssetWatcher::Update()` after an unrelated file change costs under 5 ms on the calling thread. Cooks run on jobs and the TSan preset is clean. (resolves CPT-008)
- [ ] AC-10 A `.meta` listing absolute or `..` product paths causes no deletion outside `imported/` during Cook or Prune, and it produces a warning. (resolves CPT-009)
- [ ] AC-11 Moving a source and its `.meta` to another folder and re-cooking leaves every scene, prefab and chain reference to its products resolving. (resolves CPT-010, CPT-018)
- [ ] AC-12 A test module registers an importer for a new extension, and Scan, Cook, Dependents, Prune and the asset browser handle it. (resolves CPT-011)
- [ ] AC-13 A nav bake from the CLI and the editor writes `content/navmesh.inav` from scene geometry, and it re-bakes when the scene changes. (resolves CPT-012)
- [ ] AC-14 Warnings from watcher-triggered cooks appear in the editor console. (resolves CPT-013)
- [ ] AC-15 `ReadMeta` refuses to rewrite a `.meta` whose version is newer than supported. Cook state lives outside the authored `.meta`, so an importer version bump changes no `.meta` file. (resolves CPT-014)
- [ ] AC-16 The cook key changes when any third-party importer library version changes. (resolves CPT-015)
- [ ] AC-17 Scan writes no `.meta` under hidden folders, module directories or paths outside the configured include list. (resolves CPT-016)
- [ ] AC-18 Model import can emit an LOD chain stored in `.imesh` v3, and v2 meshes still read. (resolves CPT-017)

## Guardrails to keep it that way
- CI job: `interi_cook` on the starter template and the import fixtures, failing on any import error, plus a "cook twice, all skipped" check and a byte-identical re-cook hash check.
- `AssetDatabaseTests` keeps tests for path validation (CPT-009), mid-cook edits (CPT-007) and meta versioning (CPT-014).
- Fuzz targets (`interi_asset_import_stress_tests`, `interi_import_fuzzer`) cover every new importer and cooked format added through the registry.
- Review checklist: every new cooked format uses the `WriteHashed`/`ReadHashed` container with a version constant that is part of the cook key, and every new asset reference is a guid-capable `AssetRef`.
- A test that fails when a cooked-format reader (`Read*Asset`, `ReadCookedData`, `NavMesh::Read`) has no non-test caller, so no format is left unconsumed.

## Out of scope
- Destructible fracture authoring, baking and validation internals (they live in the separate `modules/destruction` repository). Only their integration through CPT-011 is required here.
- Console-specific formats and packaging (private backend repositories). This goal requires only the platform-profile seam (AC-7).
- Runtime streaming performance and renderer UV/material sampling (Performance-Optimization and Rendering). This goal requires only that cooked data reaches those systems.
- Security hardening of importer parsing beyond CPT-009 (Security).
