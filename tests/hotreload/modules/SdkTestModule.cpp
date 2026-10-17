// SPDX-License-Identifier: MIT
// The libraries of the shared SDK test project (tests/sdk/modules), one source (INTERI_SDK_TEST_MODULE):
//   1 com.test.ext:    declares the shared hook service.Greeter ("hello <name>").
//   2 com.test.a:      declares com.test.a:Fire (damage + 1) and com.test.a:Loads (how often this module was
//                      registered, carried across reloads in its saved state); a system adds 1 to every counter.
//   3 com.test.b:      provides service.Greeter (appends "!"), overrides com.test.a:Fire (x2), declares
//                      com.test.b:Reload (identity); a system adds 10 to every counter.
//   4 com.test.plugin: overrides com.test.a:Fire (-100) and com.test.b:Reload (+1).
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

#include "TestComponents.h"

#include <cstring>
#include <string>
#include <vector>

#ifndef INTERI_SDK_TEST_MODULE
#error "define INTERI_SDK_TEST_MODULE"
#endif

namespace
{

using interi::ChunkView;
using interi::Query;
using interi::SystemAccess;
using interi::SystemContext;
using interi::hotreload::ModuleRegistrar;
using interi::sdk::HookNext;
using interi::testing::HrCounter;

using Fire = int(int);
using Greet = std::string(const std::string&);

[[maybe_unused]] void AddToCounters(SystemContext& context, std::int32_t amount)
{
    static const Query query = Query{}.With<HrCounter>();
    context.ForEachChunk(query,
                         [amount](ChunkView& chunk)
                         {
                             for (HrCounter& counter : chunk.Write<HrCounter>())
                             {
                                 counter.value += amount;
                             }
                         });
}

#if INTERI_SDK_TEST_MODULE == 2
int g_loads = 0; // module-global state, carried across reloads
#endif

bool Register(ModuleRegistrar& registrar)
{
    const interi::sdk::ModuleScope& scope = registrar.Scope();
#if INTERI_SDK_TEST_MODULE == 1
    return scope
        .DeclareHook<Greet>("service.Greeter",
                            [](const HookNext<Greet>&, const std::string& name) { return "hello " + name; })
        .has_value();
#elif INTERI_SDK_TEST_MODULE == 2
    const std::span<const std::byte> saved = registrar.SavedState();
    g_loads = 0;
    if (saved.size() == sizeof(g_loads))
    {
        std::memcpy(&g_loads, saved.data(), sizeof(g_loads));
    }
    ++g_loads;
    registrar.SetStateSaver(
        []
        {
            std::vector<std::byte> bytes(sizeof(g_loads));
            std::memcpy(bytes.data(), &g_loads, sizeof(g_loads));
            return bytes;
        });
    interi::hotreload::DeclareComponent<HrCounter>(registrar);
    registrar.AddSystem("AddOne", SystemAccess{}.Write<HrCounter>(), [](SystemContext& c) { AddToCounters(c, 1); });
    return scope.DeclareHook<Fire>(scope.Local("Fire"), [](const HookNext<Fire>&, int damage) { return damage + 1; })
               .has_value() &&
           scope.DeclareHook<Fire>(scope.Local("Loads"), [](const HookNext<Fire>&, int) { return g_loads; })
               .has_value();
#elif INTERI_SDK_TEST_MODULE == 3
    interi::hotreload::DeclareComponent<HrCounter>(registrar);
    registrar.AddSystem("AddTen", SystemAccess{}.Write<HrCounter>(), [](SystemContext& c) { AddToCounters(c, 10); });
    return scope
               .DeclareHook<Greet>("service.Greeter", [](const HookNext<Greet>& next, const std::string& name)
                                   { return next(name) + "!"; })
               .has_value() &&
           INTERI_OVERRIDE(scope, "com.test.a:Fire",
                           [](const HookNext<Fire>& next, int damage) { return 2 * next(damage); })
               .has_value() &&
           scope.DeclareHook<Fire>(scope.Local("Reload"), [](const HookNext<Fire>&, int x) { return x; }).has_value();
#elif INTERI_SDK_TEST_MODULE == 4
    return INTERI_OVERRIDE(scope, "com.test.a:Fire",
                           [](const HookNext<Fire>& next, int damage) { return next(damage) - 100; })
               .has_value() &&
           INTERI_OVERRIDE(scope, "com.test.b:Reload", [](const HookNext<Fire>& next, int x) { return next(x) + 1; })
               .has_value();
#endif
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
