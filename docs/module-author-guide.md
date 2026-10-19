# Module author guide

A **Module** is how gameplay gets into Interi. The engine is generic (ECS, jobs, rendering, audio, scenes, chains, HUD,
saves); a game is a *project* that lists modules, and the modules supply the rules. This guide walks from an empty
folder to an exported game. The reference module is `samples/modules/starter` ("Coin Run").

## The four kinds

| Kind | Use it for | Rules |
| --- | --- | --- |
| Module | A game or a feature: Values, Events, systems, data, templates | Owns local ids `<module id>:<name>`; may provide shared ids only if an Extension or the engine declared them |
| Extension | A contract many modules build on (a shared id other modules provide) | Declares `contracts`; dependents reach it through hooks and registry items, never by linking its library |
| Plugin | Changes another module without being a dependency | Only overrides: a shared id, or a local id its manifest lists in `overrides` (see the [Plugin guide](plugin-guide.md)) |
| PhysicsEngine | A physics backend | See [writing-a-physics-engine.md](writing-a-physics-engine.md) |

## Scaffold, build, test

```sh
cmake --install build/linux-clang-release --prefix /opt/interi          # once: the engine as a package
python3 tools/new_module/new_module.py --kind Module --id com.author.coins --name "Coins" --author "Me" --output coins
cmake -S coins -B coins/build -DCMAKE_PREFIX_PATH=/opt/interi && cmake --build coins/build && ctest --test-dir coins/build
```

The scaffold is a `module.json`, a standalone `CMakeLists.txt` (`find_package(Interi)` and `interi_add_gameplay_module`),
`src/<Name>Module.cpp` with a stub `Register`, and a load test (`interi_add_module_load_test`) that loads the folder
through the engine's module host. A module is built from source against the engine: the loader refuses a library whose
toolchain fingerprint (compiler, standard library, debug mode, C++ standard, ABI version, CPU baseline) differs from the
host's, which is what makes C++ types safe across the boundary.

## module.json

Metadata (id `com.author.name`, semver `version`, `author`, SPDX `license`, `engine` range), `kind`, `library` (the base
name; no prefix or extension), `toolchain`, `dependencies`, `conflicts`, `overrides`, and the **data sections**:

- `values`: typed, validated settings and counters (`bool`, `int`, `u32`, `float`, `vec3`, `quat`, `string`, `color`,
  `enum`, `assetRef`, `entityRef`, `list`) with `default`, `min`/`max`, `category`, `tooltip`. An `"id"` is a local name
  unless `"shared": true`.
- `attributes`, `attributeSets`, `archetypes`: character stats (an archetype lists attribute sets; defaults go
  attribute < archetype < project).
- `dataTables`: columns (`renamedFrom` keeps renames compatible); the project supplies the rows.
- `templates`: starter project folders the editor offers.
- `behaviours`, `computerPlayers`, `animationSets`, `localization`: AI, animation and string-table content.

Data is declared, not coded: the editor shows it, projects override it, and saves and the cooked game read it. Module
data changes reload with the module; project data that no longer fits stays stored and reads as the default.

## Register

`INTERI_GAMEPLAY_MODULE(Register)` exports the entry point. `Register(ModuleRegistrar&)` runs on load and on every hot
reload, and returns false to refuse. Through the registrar a module can:

- **Systems** (`registrar.AddSystem(name, access, fn)`): declare `SystemAccess` reads and writes; iterate with
  `ParallelForEachChunk`; record structural changes in `CommandBuffers`, never directly.
- **Components**: trivially copyable, with a `kComponentName` (`DeclareComponent<T>` checks both). Changing a layout
  needs a `ComponentMigrator` (`DeclareComponent<T>(registrar, migrator)`), otherwise the reload is refused and nothing changes.
- **Events and functions for chains** (`registrar.Chains()`): see the [Chain Reaction guide](chain-reaction-guide.md).
- **Hooks** (`registrar.Scope()`): `DeclareHook<R(Args...)>(id, default)`; Plugins and higher modules override them, and
  each implementation receives `HookNext`, the one it replaced.
- **State** across reloads: `DeclareState(registrar, g_state, {Field(...)})`, or raw bytes with `SetStateSaver`.
- **Save sections** (`registrar.Saves().Add(...)`): `<module id>:<name>`, versioned, capture/restore.
- **Services**: `registrar.Service("editor.extensions")`, `registrar.Data()` for the project's resolved data.

Rules that keep reloads safe: never leave the host anything from module code that outlives the module (systems, hooks
and the state saver are destroyed before the library closes); don't put globals in headers; use `service.Physics*`
(`sdk::IPhysicsQueries`) or the selected Physics Engine's API for physics, never a physics library directly; a module
never links another module's library.

## Priority and the identity rule

A project lists modules top (highest priority) to bottom. Each item id holds a stack ordered by rank. Local ids are only
provided by their owner; a higher module or Plugin replaces one only with a declared `overrides` entry. Shared ids (no
`:`) must be declared by the engine or an Extension, and the highest provider wins. The editor's Module Manager shows
who provides and who is shadowed; `interi_module_check` prints the same from the command line:

```sh
interi_module_check --search coins/build --docs docs-out coins
```

## Memory budgets

Allocate through `TrackedAllocator(allocator, MemoryTag::Gameplay)` to be counted per tag. A game sets
`SetMemoryBudget(tag, bytes)`; an allocation that would pass it asks the out-of-budget policy
(`ScopedOutOfBudgetHandler`: answer `Allow` after freeing a cache, or `Deny` for a null allocation). A module that
installs a handler restores the previous one before it unloads (the scoped helper does).

## Ship it

`interi_export <project dir> <out dir> --lib <build dir> --runtime <bin>/interi_player --runtime <engine libs>` writes a
package: `package.json` with every file's size and hash, the project, the module folders with their libraries, the
cooked data and launchers (`<game>.sh`, `<game>.bat`). `interi_player --package <out dir>` verifies it and plays it; see
[export.md](export.md).
