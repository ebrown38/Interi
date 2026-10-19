// SPDX-License-Identifier: MIT
#include <interi/anim/AnimEvents.h>
#include <interi/anim/Animator.h>
#include <interi/chain/Executor.h>
#include <interi/sdk/ContributionRegistry.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <utility>

namespace interi::anim
{

namespace
{

using values::ValueType;

chain::EventParam Input(std::string name, ValueType type, values::Value defaultValue)
{
    chain::EventParam param;
    param.name = std::move(name);
    param.schema.type = type;
    param.schema.defaultValue = std::move(defaultValue);
    return param;
}

chain::EventParam TargetInput()
{
    chain::EventParam target = Input("target", ValueType::EntityRef, values::EntityRef{});
    target.schema.meta.tooltip = "The entity; none means the activation's entity.";
    return target;
}

chain::EventDefinition Action(std::vector<chain::EventParam> inputs, std::string tooltip, chain::EventImpl impl)
{
    chain::EventDefinition definition;
    definition.inputs = std::move(inputs);
    definition.outputs = {{.name = "Out", .payload = {}}};
    definition.category = "Animation";
    definition.tooltip = std::move(tooltip);
    definition.implementation = std::make_shared<const chain::EventImpl>(std::move(impl));
    return definition;
}

/// Records `request` on the target's animator when the step ends.
template <typename Request>
void Defer(chain::EventCall& call, std::uint64_t target, Request request)
{
    call.Defer(
        [target, request = std::move(request)](chain::ApplyContext& context)
        {
            const std::optional<Entity> entity = context.FindEntity(target);
            if (!entity)
            {
                return;
            }
            const auto* current = context.GetWorld().Get<AnimatorComponent>(*entity);
            if (current == nullptr)
            {
                return;
            }
            AnimatorComponent updated = *current;
            request(updated);
            context.Commands().Add(context.NextCommandKey(), *entity, updated);
        });
}

} // namespace

std::vector<std::string> AnimationContracts()
{
    return {std::string(kAnimPlay), std::string(kAnimTrigger)};
}

Result<void> RegisterAnimationLibrary(chain::ChainRegistry& registry)
{
    chain::EventRegistry& events = registry.Events();
    const std::string_view engine = sdk::kEngineProviderId;
    chain::EventParam blend = Input("blend", ValueType::Float, 0.2f);
    blend.schema.meta.min = 0.0;
    blend.schema.meta.unit = "s";
    INTERI_TRY(
        events.Provide(engine, kAnimPlay,
                       Action({TargetInput(), Input("state", ValueType::String, std::string()), std::move(blend)},
                              "Fades the entity's animator to a state of its animation graph.",
                              [](chain::EventCall& call)
                              {
                                  const std::uint64_t id = call.Get<values::EntityRef>(0).id;
                                  const std::uint32_t state = AnimNameHash(call.Get<std::string>(1));
                                  const float blend = call.Get<float>(2);
                                  if (!call.Get<std::string>(1).empty())
                                  {
                                      Defer(call, id != 0 ? id : call.Entity(),
                                            [state, blend](AnimatorComponent& a)
                                            {
                                                a.requestState = state;
                                                a.requestBlend = blend;
                                            });
                                  }
                                  call.Fire(0);
                              })));
    INTERI_TRY(events.Provide(engine, kAnimTrigger,
                              Action({TargetInput(), Input("trigger", ValueType::String, std::string())},
                                     "Takes the animation graph transition the trigger names.",
                                     [](chain::EventCall& call)
                                     {
                                         const std::uint64_t id = call.Get<values::EntityRef>(0).id;
                                         const std::uint32_t trigger = AnimNameHash(call.Get<std::string>(1));
                                         if (!call.Get<std::string>(1).empty())
                                         {
                                             Defer(call, id != 0 ? id : call.Entity(),
                                                   [trigger](AnimatorComponent& a) { a.requestTrigger = trigger; });
                                         }
                                         call.Fire(0);
                                     })));
    return {};
}

} // namespace interi::anim
