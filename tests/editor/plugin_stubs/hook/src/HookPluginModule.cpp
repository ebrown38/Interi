// SPDX-License-Identifier: MIT
// Hook Plugin (com.interi.test.hookplugin): an Interi Plugin.
// It overrides the hook "com.interi.starter:Example".
// Register runs when the engine loads (or reloads) the module; see
// interi/hotreload/ModuleApi.h for the rules.
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

namespace
{

using interi::sdk::HookNext;
using Target = int(int);

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    // `next` is the implementation this one replaces; call it to keep the original behaviour, or don't.
    const auto implementation = [](const HookNext<Target>& next, int a0) { return next(a0); };
    return INTERI_OVERRIDE(scope, "com.interi.starter:Example", implementation).has_value();
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
