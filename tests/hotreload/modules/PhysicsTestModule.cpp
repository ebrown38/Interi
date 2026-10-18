// SPDX-License-Identifier: MIT
// The libraries of the Physics Engine selection fixtures (tests/hotreload/physics), one source
// (INTERI_PHYSICS_TEST_MODULE):
//   1 engine test.physics.alpha:      provides service.PhysicsQueries (x + 1).
//   2 engine test.physics.beta:       provides service.PhysicsQueries (x + 100).
//   3 engine test.physics.alpha_plus: extends alpha; provides service.PhysicsQueries as 2 * the engine it replaced.
//   4 a game module:                  declares <module>:Engine, "<selected engine id>@<version>" as it was told.
//   5 game adapter for alpha:         declares <module>:Adapter, "alpha".
//   6 game adapter for beta:          declares <module>:Adapter, "beta".
//   7 a module that wrongly provides the reserved service.PhysicsQueries.
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

#include <string>

#ifndef INTERI_PHYSICS_TEST_MODULE
#error "define INTERI_PHYSICS_TEST_MODULE"
#endif

namespace
{

using interi::sdk::HookNext;
using Query = int(int);
using Name = std::string(int);

[[maybe_unused]] std::string g_engine; // module global: lives as long as the library is loaded

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
#if INTERI_PHYSICS_TEST_MODULE == 1
    return scope.DeclareHook<Query>("service.PhysicsQueries", [](const HookNext<Query>&, int x) { return x + 1; })
        .has_value();
#elif INTERI_PHYSICS_TEST_MODULE == 2
    return scope.DeclareHook<Query>("service.PhysicsQueries", [](const HookNext<Query>&, int x) { return x + 100; })
        .has_value();
#elif INTERI_PHYSICS_TEST_MODULE == 3
    return scope
        .DeclareHook<Query>("service.PhysicsQueries", [](const HookNext<Query>& next, int x) { return 2 * next(x); })
        .has_value();
#elif INTERI_PHYSICS_TEST_MODULE == 4
    const auto engine = registrar.PhysicsEngine();
    g_engine = std::string(engine.id) + "@" + std::string(engine.version);
    return scope.DeclareHook<Name>(scope.Local("Engine"), [](const HookNext<Name>&, int) { return g_engine; })
        .has_value();
#elif INTERI_PHYSICS_TEST_MODULE == 5
    return scope
        .DeclareHook<Name>(scope.Local("Adapter"), [](const HookNext<Name>&, int) { return std::string("alpha"); })
        .has_value();
#elif INTERI_PHYSICS_TEST_MODULE == 6
    return scope
        .DeclareHook<Name>(scope.Local("Adapter"), [](const HookNext<Name>&, int) { return std::string("beta"); })
        .has_value();
#elif INTERI_PHYSICS_TEST_MODULE == 7
    return scope.DeclareHook<Query>("service.PhysicsQueries", [](const HookNext<Query>&, int x) { return x; })
        .has_value();
#endif
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
