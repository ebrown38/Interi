// SPDX-License-Identifier: MIT
// Starter (com.interi.starter): an Interi Module. Register runs when the engine loads (or reloads) the module; see
// interi/hotreload/ModuleApi.h for the rules and interi/sdk/Hooks.h for hooks.
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

namespace
{

using interi::sdk::HookNext;
using Example = int(int);

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    // A hook that Plugins and higher modules can override ("com.interi.starter:Example").
    return scope.DeclareHook<Example>(scope.Local("Example"), [](const HookNext<Example>&, int value) { return value; })
        .has_value();
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
