// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/core/Random.h>
#include <interi/sdk/ContributionRegistry.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
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

EventDefinition Flow(std::vector<EventParam> inputs, std::vector<EventOutput> outputs, std::string tooltip,
                     EventImpl implementation, bool latent = false)
{
    EventDefinition definition;
    definition.inputs = std::move(inputs);
    definition.outputs = std::move(outputs);
    definition.latent = latent;
    definition.category = "Flow";
    definition.tooltip = std::move(tooltip);
    definition.implementation = std::make_shared<const EventImpl>(std::move(implementation));
    return definition;
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

Result<void> RegisterFlow(EventRegistry& events)
{
    const std::string_view engine = sdk::kEngineProviderId;

    EventParam seconds = Input("seconds", ValueType::Float, 1.0f);
    seconds.schema.meta.min = 0.0;
    seconds.schema.meta.unit = "s";
    INTERI_TRY(events.Provide(engine, kFlowDelay,
                              Flow(
                                  {seconds}, {{.name = "Finished", .payload = {}}},
                                  "Fires Finished after a delay (rounded up to whole fixed steps).", [](EventCall& call)
                                  { call.FireAfter(SecondsToNanoseconds(call.Get<float>(0)), 0); }, true)));

    EventParam interval = Input("interval", ValueType::Float, 1.0f);
    interval.schema.meta.min = 0.0;
    interval.schema.meta.unit = "s";
    EventParam count = Input("count", ValueType::Int, std::int32_t{1});
    count.schema.meta.min = 1.0;
    count.schema.meta.max = static_cast<double>(kMaxTimerCount);
    INTERI_TRY(events.Provide(engine, kFlowTimer,
                              Flow(
                                  {std::move(interval), std::move(count)},
                                  {{.name = "Fired", .payload = {{.name = "count", .type = ValueType::Int}}}},
                                  "Fires count times, every interval seconds.",
                                  [](EventCall& call)
                                  {
                                      const std::int64_t every = SecondsToNanoseconds(call.Get<float>(0));
                                      const std::int32_t times =
                                          std::clamp(call.Get<std::int32_t>(1), 1, kMaxTimerCount);
                                      constexpr std::int64_t kLimit = std::numeric_limits<std::int64_t>::max() / 2;
                                      for (std::int32_t k = 1; k <= times; ++k)
                                      {
                                          call.FireAfter(every > kLimit / k ? kLimit : every * k, 0, {k});
                                      }
                                  },
                                  true)));

    INTERI_TRY(
        events.Provide(engine, kFlowBranch,
                       Flow({Input("condition", ValueType::Bool, false)},
                            {{.name = "True", .payload = {}}, {.name = "False", .payload = {}}}, "Fires True or False.",
                            [](EventCall& call) { call.Fire(call.Get<bool>(0) ? 0 : 1); })));

    INTERI_TRY(events.Provide(engine, kFlowSequence,
                              Flow({},
                                   {{.name = "Then0", .payload = {}},
                                    {.name = "Then1", .payload = {}},
                                    {.name = "Then2", .payload = {}},
                                    {.name = "Then3", .payload = {}}},
                                   "Fires each output in order.",
                                   [](EventCall& call)
                                   {
                                       for (std::size_t output = 0; output < 4; ++output)
                                       {
                                           call.Fire(output);
                                       }
                                   })));

    INTERI_TRY(events.Provide(
        engine, kFlowRandom,
        Flow({Input("min", ValueType::Int, std::int32_t{0}), Input("max", ValueType::Int, std::int32_t{1})},
             {{.name = "Out", .payload = {{.name = "value", .type = ValueType::Int}}}},
             "Fires Out with a random integer in [min, max] (deterministic: seeded per activation).",
             [](EventCall& call)
             {
                 std::int32_t low = call.Get<std::int32_t>(0);
                 std::int32_t high = call.Get<std::int32_t>(1);
                 if (high < low)
                 {
                     std::swap(low, high);
                 }
                 Random random(call.Seed());
                 // The span in u32 can't overflow; a full span (wrapping to 0) takes any value.
                 const std::uint32_t span = static_cast<std::uint32_t>(high) - static_cast<std::uint32_t>(low) + 1u;
                 const std::uint32_t offset =
                     span == 0 ? static_cast<std::uint32_t>(random.NextU64()) : random.NextBelow(span);
                 call.Fire(0, {static_cast<std::int32_t>(static_cast<std::uint32_t>(low) + offset)});
             })));
    return {};
}

} // namespace

std::vector<std::string> CoreContracts()
{
    return {std::string(kFlowDelay),           std::string(kFlowTimer),          std::string(kFlowBranch),
            std::string(kFlowSequence),        std::string(kFlowRandom),         std::string(kTriggerStart),
            std::string(kTriggerTick),         std::string(kTriggerInputAction), std::string(kTriggerTimer),
            std::string(kTriggerValueChanged), std::string(kTriggerSpawned),     std::string(kTriggerDestroyed)};
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
    return RegisterFlow(events);
}

} // namespace interi::chain
