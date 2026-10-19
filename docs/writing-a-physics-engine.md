# Writing a Physics Engine

A Physics Engine is a module of kind `PhysicsEngine` (for example `com.interi.physics.jolt`). The engine repository
ships none: anyone can write one. It is a sort of Extension: other modules are built against it, and it provides the
reserved `service.Physics*` contracts (`interi::sdk::PhysicsContracts()`, currently `service.PhysicsQueries`, whose signature is
`sdk::PhysicsQueriesService` in `interi/sdk/PhysicsQueries.h`).

## Scaffold it

```sh
python3 tools/new_module/new_module.py --kind PhysicsEngine --id com.author.physics.mine --name "Mine" \
    --author "Author" --output path/to/mine
# or one that builds on another engine:
python3 tools/new_module/new_module.py --kind PhysicsEngine --id com.author.physics.soft --extends com.interi.physics.jolt@^5 ...
```

You get `module.json`, a `CMakeLists.txt` (`find_package(Interi)`), a source file with a `Register` that provides the
contract as a hook, and a load test. `interi_module_check` pins an engine module given on its command line, so the
load test loads it even though no module declares it. An engine that extends another needs the base engine's folder
for its load test: configure with `-DINTERI_BASE_ENGINE_DIR=<folder with the base's module.json and library>`.

## Rules

- `module.json` has `"kind": "PhysicsEngine"` and a `library`. It declares no `dependencies` and no `physicsEngines`.
- Nobody lists an engine in a project's `modules` or in `dependencies`. Modules name the engines they support in
  `physicsEngines` (`{ id, version, default, library }`), and the project may pin one (`"physicsEngine": { "id": ...,
  "version": ... }` in `project.json`).
- Only an engine provides `service.Physics*`. Any other module that tries is refused at load.
- **Extending.** `"extends": { "id": "com.interi.physics.jolt", "version": "^5" }` names one base (chains are fine,
  cycles are an error). The base loads below your engine and provides everything it does; what you provide replaces
  the base's item by item (your hook receives the base's as `next`). Your engine counts as its base: a module that
  supports `jolt ^5` accepts an engine that extends Jolt 5.x. Only one engine is selected; the base is part of it.
- A module that supports several engines can name an adapter `library` per entry. Only the adapter of the entry the
  selected engine satisfies loads, after the module's main library, and it is told the engine
  (`registrar.PhysicsEngine()`).

## How a project picks its engine

1. The project's pin, if any, is the only candidate. Otherwise the candidates are the engines named by modules that
   declare `physicsEngines`, in priority order (a module's `default` first).
2. The first candidate every declaring module supports wins, at the highest installed version inside every range.
3. If none works the project doesn't resolve, and the error names the modules blocking each candidate. The editor
   refuses to add such a module (Project menu), and the project stays as it was.

Changing the engine (pin, module order, enabled modules) re-resolves the project; it is never swapped during play.

## Worked example: the Jolt module

`com.interi.physics.jolt` (the first real Physics Engine, kept in a separate repository folder, `modules/physics/`)
shows every part:

- `module.json`: kind `PhysicsEngine`, Values (`gravity`, `stepRate`, `bodyBudget`) and data tables (`Layers`,
  `PhysicsMaterials`).
- The entry library provides `service.PhysicsQueries` as a hook of signature `sdk::PhysicsQueriesService`
  (`interi/sdk/PhysicsQueries.h`: `IPhysicsQueries` with `Raycast`, `SphereCast`, `OverlapSphere` and a layer mask), so
  physics-agnostic modules query any engine the same way. Local `hook.physics.*` points can be overridden by higher
  modules, and chain events (`OnContact`, `OnOverlap`, `ApplyImpulse`, `Raycast`) use the Chain Reaction registries.
- The code the engine and the modules built against it share (the world, the character controller) lives in one shared
  library, so a process has one copy of the engine's global state; the entry library only registers.
