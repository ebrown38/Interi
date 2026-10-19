# Module Manager

The editor's **Module Manager** (menu bar, or `level_editor --module-manager 1`) manages the open project's modules.
Its model has no UI and is tested headless: `engine/editor/ModuleManager.h`, `ModuleConflicts.h` and
`PluginAuthoring.h`; the window is `engine/editor_ui/ModuleManagerPanel.h`.

## Modules

Every installed module is listed by state:

| State        | Meaning |
|--------------|---------|
| Enabled      | In the project's list. The order is the priority: the top module wins where two provide the same item. Drag a row, or use Up/Down, to reorder. |
| Implicit     | Pulled in as a dependency of an enabled module ("required by ..."). |
| Engine       | A Physics Engine. It is selected from the modules' `physicsEngines`, never enabled by hand. |
| Available    | Installed and could be enabled. |
| Incompatible | The engine or the project refuses it; the reason is shown (engine version, toolchain, a conflict, a missing dependency, no Physics Engine every module supports). |

Enabling, disabling and reordering are **checked against the module graph before anything is applied**
(`EnableModule`, `DisableModule`, `MoveModule` return the resulting project and what it adds, removes and changes in the
Physics Engine, or the reason it is refused). Disabling a module another loaded module requires is refused ("required
by A, B"). The project is saved and reopened only after the check passes.

## Dependency graph

Modules by load order in columns (dependencies to the left), with required, optional and Physics Engine edges.

## Conflicts

Every item more than one module provides (Events, Functions, hooks, Values, attributes, archetypes, tables) with the
winner and the shadowed providers, and every `overrides` entry of a manifest with whether it took effect. The lists come
from the registries, so Events and hooks appear once the project's libraries are loaded. "Plugin..." on a hook or Event
opens the Plugin tab for it; so does right-clicking an event in the Chain Editor palette.

## Plugin authoring

The Plugin tab generates a Plugin stub (module.json, CMakeLists.txt, source) that overrides a hook or an Event.

- A local id (`com.author.base:Fire`) gets an `overrides` entry for the owner's major version (minor while 0.x) and uses
  `INTERI_OVERRIDE` (hooks) or `Events().Override` (Events).
- A shared id is provided instead (the highest provider wins).
- An Event stub copies the existing definition so the pins stay the same, and **replaces** its implementation: the
  owner's code must not outlive the owner.
- The hook's C++ signature is read back from the registry (`sdk::ExtractHookSignature`, Clang, GCC and MSVC spellings).

The generator's output for fixed inputs is committed in `tests/editor/plugin_stubs`, built, loaded over the starter
module and checked (`PluginStub_*_LoadsOverStarter`); `INTERI_UPDATE_GOLDEN=1` rewrites it.

**Hot-reload loop**: stop play, run the build command, reload the project's module libraries, play again. The shell
runs in the editor, never in the engine library (`RunDevLoop` takes callbacks). With "reload and replay when a module
library is rebuilt" on, the editor also watches the libraries and runs the loop (without the build) when one changes.
A failed build or reload leaves the previous modules loaded and play stopped.

## Docs

The Docs tab and `interi_module_check --docs <dir>` write a Markdown page per module (`hotreload::GenerateModuleDocs`):
metadata, dependencies, conflicts, contracts and overrides, the Events, Functions and hook points the module's code
registers, and the Values and Data Tables of its module.json. The editor button works for modules loaded in the open
project; the command line loads modules itself, so it needs only the module folders.

## Trust

**Native code runs with full trust. There is no sandboxing.** A module is a shared library loaded into the editor and
the game process: it can read and write any file, open sockets and run anything you can. The engine checks the
toolchain fingerprint (so the ABI matches), not intent.

The Module Manager therefore asks before native code is first enabled:

- The confirmation dialog shows the author and license of **every** module the change brings in: the one you chose, the
  Extensions it pulls in, and a Physics Engine the change newly selects. Content-only modules (no `library`) need none.
- Confirming records `id`, `author` and `license` in `trusted_modules.json` next to the editor preferences. A new author
  or license asks again; the id alone is not enough.
- The Project menu's "Add module" goes through the same check.
- Opening a project that already lists modules does not ask: a project you did not write can load native code as soon as
  it opens. Open projects from sources you trust, as you would build their code.
