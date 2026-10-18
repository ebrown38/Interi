// SPDX-License-Identifier: MIT
#include <interi/chain/CoreLibrary.h>
#include <interi/chain/Executor.h>
#include <interi/core/Random.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/sdk/ContributionRegistry.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
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

EventDefinition World(std::vector<EventParam> inputs, std::string tooltip, EventImpl implementation)
{
    EventDefinition definition;
    definition.inputs = std::move(inputs);
    definition.outputs = {{.name = "Out", .payload = {}}};
    definition.category = "World";
    definition.tooltip = std::move(tooltip);
    definition.implementation = std::make_shared<const EventImpl>(std::move(implementation));
    return definition;
}

/// The stable id an input names: its own, or (0) the activation's entity.
[[nodiscard]] std::uint64_t TargetOf(const EventCall& call, std::size_t input)
{
    const std::uint64_t id = call.Get<values::EntityRef>(input).id;
    return id != 0 ? id : call.Entity();
}

EventParam TargetInput()
{
    EventParam target = Input("target", ValueType::EntityRef, values::EntityRef{});
    target.schema.meta.tooltip = "The entity; none means the activation's entity.";
    return target;
}

EventParam TypedFrom(std::string name, std::string source)
{
    EventParam param;
    param.name = std::move(name);
    param.typeFrom = std::move(source);
    return param;
}

/// An event that applies `apply(context, entity)` to its target entity (skipped when it doesn't exist).
template <typename Apply>
EventImpl OnTarget(Apply apply)
{
    return [apply](EventCall& call)
    {
        const std::uint64_t id = TargetOf(call, 0);
        call.Defer(
            [apply, id](ApplyContext& context)
            {
                if (const std::optional<Entity> entity = context.FindEntity(id))
                {
                    apply(context, *entity, id);
                }
            });
        call.Fire(0);
    };
}

Result<void> RegisterWorld(EventRegistry& events)
{
    const std::string_view engine = sdk::kEngineProviderId;

    INTERI_TRY(events.Provide(engine, kWorldSetValue,
                              World({Input("id", ValueType::String, std::string()), TypedFrom("value", "id")},
                                    "Sets a Value (the last write in a step wins).",
                                    [](EventCall& call)
                                    {
                                        call.Defer([id = call.Get<std::string>(0), value = call.Input(1)](
                                                       ApplyContext& context) { context.SetValue(id, value); });
                                        call.Fire(0);
                                    })));
    INTERI_TRY(events.Provide(engine, kWorldModifyValue,
                              World({Input("id", ValueType::String, std::string()), TypedFrom("delta", "id")},
                                    "Adds to a numeric Value (deltas in one step accumulate).",
                                    [](EventCall& call)
                                    {
                                        call.Defer([id = call.Get<std::string>(0), delta = call.Input(1)](
                                                       ApplyContext& context) { context.ModifyValue(id, delta); });
                                        call.Fire(0);
                                    })));
    INTERI_TRY(events.Provide(engine, kWorldDestroy,
                              World({TargetInput()}, "Destroys the entity.",
                                    OnTarget(
                                        [](ApplyContext& context, Entity entity, std::uint64_t id)
                                        {
                                            context.Commands().DestroyEntity(context.NextCommandKey(), entity);
                                            context.RaiseTrigger(kTriggerDestroyed, id, {},
                                                                 {values::EntityRef{.id = id}});
                                        }))));
    INTERI_TRY(events.Provide(
        engine, kWorldMove,
        World({TargetInput(), Input("delta", ValueType::Vec3, Vec3{})}, "Moves the entity by delta (world space).",
              [](EventCall& call)
              {
                  const std::uint64_t id = TargetOf(call, 0);
                  const Vec3 delta = call.Get<Vec3>(1);
                  call.Defer(
                      [id, delta](ApplyContext& context)
                      {
                          const std::optional<Entity> entity = context.FindEntity(id);
                          std::optional<Transform> transform = entity ? context.GetTransform(*entity) : std::nullopt;
                          if (transform)
                          {
                              transform->position =
                                  Vec3(transform->position.x + delta.x, transform->position.y + delta.y,
                                       transform->position.z + delta.z);
                              context.SetTransform(*entity, *transform);
                          }
                      });
                  call.Fire(0);
              })));
    INTERI_TRY(events.Provide(
        engine, kWorldRotate,
        World({TargetInput(), Input("axis", ValueType::Vec3, Vec3(0.0f, 1.0f, 0.0f)),
               Input("degrees", ValueType::Float, 0.0f)},
              "Rotates the entity about a world axis.",
              [](EventCall& call)
              {
                  const std::uint64_t id = TargetOf(call, 0);
                  const Vec3 axis = NormalizeOrZero(call.Get<Vec3>(1));
                  const float radians = call.Get<float>(2) * (3.14159265358979f / 180.0f);
                  call.Defer(
                      [id, axis, radians](ApplyContext& context)
                      {
                          const std::optional<Entity> entity = context.FindEntity(id);
                          std::optional<Transform> transform = entity ? context.GetTransform(*entity) : std::nullopt;
                          if (transform && !(axis == Vec3{}))
                          {
                              transform->rotation = Normalize(Quat::FromAxisAngle(axis, radians) * transform->rotation);
                              context.SetTransform(*entity, *transform);
                          }
                      });
                  call.Fire(0);
              })));
    INTERI_TRY(events.Provide(
        engine, kWorldShow,
        World({TargetInput()}, "Shows the entity.",
              OnTarget([](ApplyContext& context, Entity entity, std::uint64_t)
                       { context.Commands().Remove<HiddenComponent>(context.NextCommandKey(), entity); }))));
    INTERI_TRY(events.Provide(
        engine, kWorldHide,
        World({TargetInput()}, "Hides the entity.",
              OnTarget([](ApplyContext& context, Entity entity, std::uint64_t)
                       { context.Commands().Add(context.NextCommandKey(), entity, HiddenComponent{}); }))));
    EventParam volume = Input("volume", ValueType::Float, 1.0f);
    volume.schema.meta.min = 0.0;
    INTERI_TRY(events.Provide(engine, kWorldPlaySound,
                              World({Input("cue", ValueType::String, std::string()), TargetInput(), std::move(volume)},
                                    "Asks the host to play a sound cue at the entity.",
                                    [](EventCall& call)
                                    {
                                        SoundRequest request{.cue = call.Get<std::string>(0),
                                                             .entity = TargetOf(call, 1),
                                                             .volume = call.Get<float>(2)};
                                        call.Defer([request = std::move(request)](ApplyContext& context)
                                                   { context.PlaySound(request); });
                                        call.Fire(0);
                                    })));
    INTERI_TRY(events.Provide(
        engine, kWorldSpawnPrefab,
        World({Input("prefab", ValueType::AssetRef, values::AssetRef{}), Input("position", ValueType::Vec3, Vec3{}),
               Input("rotation", ValueType::Quat, Quat{})},
              "Spawns a prefab at a world position (after the step; trigger.Spawned fires for it next step).",
              [](EventCall& call)
              {
                  Transform transform;
                  transform.position = call.Get<Vec3>(1);
                  transform.rotation = call.Get<Quat>(2);
                  std::string prefab = call.Get<values::AssetRef>(0).path;
                  if (!prefab.empty())
                  {
                      call.Defer([prefab = std::move(prefab), transform](ApplyContext& context)
                                 { (void)context.SpawnPrefab(prefab, transform); });
                  }
                  call.Fire(0);
              })));
    return {};
}

} // namespace

std::vector<std::string> CoreContracts()
{
    return {std::string(kWorldSetValue),       std::string(kWorldModifyValue),   std::string(kWorldDestroy),
            std::string(kWorldMove),           std::string(kWorldRotate),        std::string(kWorldShow),
            std::string(kWorldHide),           std::string(kWorldPlaySound),     std::string(kWorldSpawnPrefab),
            std::string(kFlowDelay),           std::string(kFlowTimer),          std::string(kFlowBranch),
            std::string(kFlowSequence),        std::string(kFlowRandom),         std::string(kTriggerStart),
            std::string(kTriggerTick),         std::string(kTriggerInputAction), std::string(kTriggerTimer),
            std::string(kTriggerValueChanged), std::string(kTriggerSpawned),     std::string(kTriggerDestroyed),
            std::string(kTriggerOverlap),      std::string(kTriggerOverlapEnd)};
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
    for (const std::string_view overlap : {kTriggerOverlap, kTriggerOverlapEnd})
    {
        INTERI_TRY(events.Provide(engine, overlap,
                                  Trigger({Input(std::string(kTriggerKeyInput), ValueType::String, std::string())},
                                          {{.name = "other", .type = ValueType::EntityRef}},
                                          overlap == kTriggerOverlap
                                              ? "Fires when an actor enters the trigger volume tagged key."
                                              : "Fires when an actor leaves the trigger volume tagged key.")));
    }
    INTERI_TRY(RegisterFlow(events));
    return RegisterWorld(events);
}

} // namespace interi::chain
