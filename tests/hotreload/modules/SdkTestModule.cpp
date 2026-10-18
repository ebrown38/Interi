// SPDX-License-Identifier: MIT
// The libraries of the shared SDK test project (tests/sdk/modules), one source (INTERI_SDK_TEST_MODULE):
//   1 com.test.ext:    declares the shared hook service.Greeter ("hello <name>").
//   2 com.test.a:      declares com.test.a:Fire (damage + 1), com.test.a:Loads (how often this module was
//                      registered, carried across reloads in its reflected state) and com.test.a:Jump (10x the value
//                      com.test.a:JumpHeight, read from the project's data at call time); a system adds 1 to every
//                      counter. Chain event com.test.a:Hit(amount: int = 1) -> Out {total: int} fires
//                      total = 10 * amount + loads.
//   3 com.test.b:      provides service.Greeter (appends "!"), overrides com.test.a:Fire (x2), declares
//                      com.test.b:Reload (identity); a system adds 10 to every counter.
//   4 com.test.plugin: overrides com.test.a:Fire (-100) and com.test.b:Reload (+1).
#include <interi/ecs/Query.h>
#include <interi/ecs/Scheduler.h>
#include <interi/hotreload/ModuleApi.h>
#include <interi/sdk/Hooks.h>

#include "TestComponents.h"

#include <cstdint>
#include <memory>
#include <string>

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
struct ModuleState
{
    std::int32_t loads = 0; ///< carried across reloads as reflected state
};
ModuleState g_state;
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
    g_state = {};
    interi::hotreload::DeclareState(registrar, g_state, {interi::values::Field("loads", &ModuleState::loads)});
    ++g_state.loads;
    interi::hotreload::DeclareComponent<HrCounter>(registrar);
    registrar.AddSystem("AddOne", SystemAccess{}.Write<HrCounter>(), [](SystemContext& c) { AddToCounters(c, 1); });
    const interi::values::ValueRegistry& values = registrar.Data().Values();
    interi::chain::EventDefinition hit;
    interi::chain::EventParam amount;
    amount.name = "amount";
    amount.schema.type = interi::values::ValueType::Int;
    amount.schema.defaultValue = std::int32_t{1};
    hit.inputs = {amount};
    hit.outputs = {{.name = "Out", .payload = {{.name = "total", .type = interi::values::ValueType::Int}}}};
    // The load count is read when the module registers: each instance's implementation fires its own.
    hit.implementation = std::make_shared<const interi::chain::EventImpl>(
        [loads = g_state.loads](interi::chain::EventCall& call)
        { call.Fire(0, {std::int32_t{10 * call.Get<std::int32_t>(0) + loads}}); });
    if (!registrar.Chains().Events().Provide(scope.Provider(), scope.Local("Hit"), std::move(hit)).has_value())
    {
        return false;
    }
    return scope.DeclareHook<Fire>(scope.Local("Fire"), [](const HookNext<Fire>&, int damage) { return damage + 1; })
               .has_value() &&
           scope.DeclareHook<Fire>(scope.Local("Loads"), [](const HookNext<Fire>&, int) { return g_state.loads; })
               .has_value() &&
           scope
               .DeclareHook<Fire>(
                   scope.Local("Jump"), [&values](const HookNext<Fire>&, int)
                   { return static_cast<int>(10.0f * values.Get<float>("com.test.a:JumpHeight").value_or(0.0f)); })
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
