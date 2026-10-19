// SPDX-License-Identifier: MIT
// Event Plugin (com.interi.test.eventplugin): an Interi Plugin.
// It overrides the event "com.interi.starter:LevelComplete".
// Register runs when the engine loads (or reloads) the module; see
// interi/hotreload/ModuleApi.h for the rules.
#include <interi/chain/Event.h>
#include <interi/hotreload/ModuleApi.h>

#include <memory>
#include <string_view>

namespace
{

bool Register(interi::hotreload::ModuleRegistrar& registrar)
{
    using namespace interi::chain;
    const interi::sdk::ModuleScope& scope = registrar.Scope();
    EventRegistry& events = registrar.Chains().Events();
    constexpr std::string_view id = "com.interi.starter:LevelComplete";

    // Start from the event as it is (same pins: an override must keep the signature).
    const EventDefinition* existing = events.Resolve(id);
    if (existing == nullptr)
    {
        return false;
    }
    EventDefinition definition = *existing;
    if (definition.kind == EventKind::Action)
    {
        // Replace the implementation: the original's code belongs to its module and must not outlive it.
        // Inputs: none. Outputs: 0 Out.
        definition.implementation = std::make_shared<const EventImpl>([](EventCall& call) { call.Fire(0); });
    }
    return events.Override(scope.Provider(), id, std::move(definition)).has_value();
}

} // namespace

INTERI_GAMEPLAY_MODULE(Register)
