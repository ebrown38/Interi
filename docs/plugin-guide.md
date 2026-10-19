# Plugin guide

A **Plugin** changes how another module behaves without that module knowing about it: a balance tweak, a different
reaction to an event, a replacement for a hook. A Plugin only overrides; it adds no ids of its own that others depend on.

## What can be overridden

| Target | How | The override must |
| --- | --- | --- |
| A hook (`com.author.mod:Fire`) | `INTERI_OVERRIDE(scope, id, implementation)` | keep the hook's signature (compared by compiler spelling) |
| A chain Event or Function | `registrar.Chains().Events().Override(provider, id, definition)` | keep the signature (inputs, outputs, payloads); the new definition usually keeps the old pins and swaps the implementation |
| A Value, attribute, archetype or table | the `overrides` array in module.json data sections | keep the type |
| A shared id (no `:`) | just provide it | be a contract the engine or an Extension declared |

Local ids (`<module id>:<name>`) are protected: a Plugin must list them in its manifest first.

```json
{
  "id": "com.author.coins_x2", "kind": "Plugin", "version": "1.0.0", "engine": "^0.1",
  "library": "coins_x2",
  "overrides": [ { "target": "com.interi.starter:LevelComplete", "version": "^0.1" } ]
}
```

`version` is a range of the *owner's* version: when the owner moves out of the range, the override is refused instead of
silently misbehaving, and the Module Manager reports it.

## Writing one

The editor generates the boilerplate: right-click an Event in the chain palette (or pick a hook) and choose **Create
Plugin override...** (`GeneratePluginStub`, Module Manager > Plugin). The stub declares the override and builds against
the installed engine. By hand:

```cpp
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

namespace
{
using Target = int(int);

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    // `next` is what this implementation replaces; call it to keep the original behaviour.
    const auto implementation = [](const interi::sdk::HookNext<Target>& next, int value) { return next(value) * 2; };
    return INTERI_OVERRIDE(scope, "com.interi.starter:Example", implementation).has_value();
}
} // namespace

INTERI_GAMEPLAY_MODULE(Register)
```

For an Event, resolve the existing definition, copy it, replace `implementation` (the original's code belongs to its
module and must not outlive it) and `Override` it. Tested stubs for both are in `tests/editor/plugin_stubs`; CI builds
and loads them over the starter module.

## Order and conflicts

A Plugin ranks by its place in the project list like any module. When two Plugins override the same item the higher one
wins and the other is shadowed; the Conflicts tab lists both. Hooks chain: the winner receives `HookNext` for the
implementation it replaced, so stacked Plugins compose. A reload of the owner reloads its Plugins.

## Develop with the loop

The Module Manager's Plugin tab runs **build, reload, replay** (`RunDevLoop`): it stops play, builds the library,
hot-reloads it and replays the scene, stopping at the first failure. `LibraryWatcher` reloads a rebuilt library on its
own. A failed reload (ABI or toolchain mismatch, a layout change without a migrator, a refused registration) restores
what was loaded and leaves the world untouched.

## Trust

A library is native code that runs with the game's full privileges and no sandbox. The editor asks before enabling a
module that has one (keyed by id, author and license), including implicit Extensions and a newly selected Physics
Engine. Only enable modules you trust.
