// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/sdk/ContributionRegistry.h>

#include <utility>

namespace interi::chain
{

namespace
{

using values::ValueType;

EventParam Input(std::string name, ValueType type, values::Value defaultValue)
{
    EventParam param;
    param.name = std::move(name);
    param.schema.type = type;
    param.schema.defaultValue = std::move(defaultValue);
    return param;
}

EventDefinition Trigger(std::vector<EventParam> inputs, std::vector<PayloadField> payload, std::string tooltip)
{
    EventDefinition definition;
    definition.kind = EventKind::Trigger;
    definition.inputs = std::move(inputs);
    definition.outputs = {{.name = "Fired", .payload = std::move(payload)}};
    definition.category = "Triggers";
    definition.tooltip = std::move(tooltip);
    return definition;
}

} // namespace

std::vector<std::string> CoreContracts()
{
    return {std::string(kTriggerStart),    std::string(kTriggerTick),         std::string(kTriggerInputAction),
            std::string(kTriggerTimer),    std::string(kTriggerValueChanged), std::string(kTriggerSpawned),
            std::string(kTriggerDestroyed)};
}

Result<void> RegisterCoreLibrary(ChainRegistry& registry)
{
    EventRegistry& events = registry.Events();
    const std::string_view engine = sdk::kEngineProviderId;
    INTERI_TRY(events.Provide(engine, kTriggerStart, Trigger({}, {}, "Fires once, on the first step.")));
    INTERI_TRY(
        events.Provide(engine, kTriggerTick,
                       Trigger({}, {{.name = "step", .type = ValueType::Int}, {.name = "dt", .type = ValueType::Float}},
                               "Fires every fixed step.")));
    INTERI_TRY(events.Provide(engine, kTriggerInputAction,
                              Trigger({Input(std::string(kTriggerKeyInput), ValueType::String, std::string())}, {},
                                      "Fires when the input action named by key is pressed.")));
    EventParam interval = Input("interval", ValueType::Float, 1.0f);
    interval.schema.meta.min = 0.0;
    interval.schema.meta.unit = "s";
    INTERI_TRY(events.Provide(
        engine, kTriggerTimer,
        Trigger({std::move(interval)}, {{.name = "count", .type = ValueType::Int}}, "Fires every interval seconds.")));
    INTERI_TRY(events.Provide(engine, kTriggerValueChanged,
                              Trigger({Input(std::string(kTriggerKeyInput), ValueType::String, std::string())}, {},
                                      "Fires the step after a chain changed the Value named by key.")));
    INTERI_TRY(events.Provide(
        engine, kTriggerSpawned,
        Trigger({}, {{.name = "entity", .type = ValueType::EntityRef}}, "Fires when an entity is spawned.")));
    INTERI_TRY(events.Provide(
        engine, kTriggerDestroyed,
        Trigger({}, {{.name = "entity", .type = ValueType::EntityRef}}, "Fires when an entity is destroyed.")));
    return {};
}

} // namespace interi::chain
