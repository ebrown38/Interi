// SPDX-License-Identifier: MIT
#include <interi/chain/Executor.h>
#include <interi/nav/NavAgents.h>
#include <interi/nav/NavEvents.h>
#include <interi/sdk/ContributionRegistry.h>

#include <memory>
#include <optional>
#include <utility>

namespace interi::nav
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

chain::EventDefinition Action(std::vector<chain::EventParam> inputs, std::vector<chain::EventOutput> outputs,
                              std::string tooltip, chain::EventImpl impl)
{
    chain::EventDefinition definition;
    definition.inputs = std::move(inputs);
    definition.outputs = std::move(outputs);
    definition.category = "Navigation";
    definition.tooltip = std::move(tooltip);
    definition.implementation = std::make_shared<const chain::EventImpl>(std::move(impl));
    return definition;
}

chain::EventDefinition Trigger(std::string tooltip)
{
    chain::EventDefinition definition;
    definition.kind = chain::EventKind::Trigger;
    definition.outputs = {{.name = "Fired", .payload = {}}};
    definition.category = "Navigation";
    definition.tooltip = std::move(tooltip);
    return definition;
}

std::uint64_t TargetOf(const chain::EventCall& call, std::size_t input)
{
    const std::uint64_t id = call.Get<values::EntityRef>(input).id;
    return id != 0 ? id : call.Entity();
}

/// Applies `change(agent)` to the target's NavAgentComponent when the step ends.
template <typename Change>
void DeferAgent(chain::EventCall& call, std::uint64_t target, Change change)
{
    call.Defer(
        [target, change = std::move(change)](chain::ApplyContext& context)
        {
            const std::optional<Entity> entity = context.FindEntity(target);
            if (!entity)
            {
                return;
            }
            const auto* current = context.GetWorld().Get<NavAgentComponent>(*entity);
            if (current == nullptr)
            {
                return;
            }
            NavAgentComponent updated = *current;
            change(updated);
            context.Commands().Add(context.NextCommandKey(), *entity, updated);
        });
}

} // namespace

std::vector<std::string> NavigationContracts()
{
    return {std::string(kNavFindPath), std::string(kNavMoveTo), std::string(kNavStop), std::string(kTriggerArrived),
            std::string(kTriggerFailed)};
}

Result<void> RegisterNavigationLibrary(chain::ChainRegistry& registry, std::shared_ptr<NavSource> source)
{
    chain::EventRegistry& events = registry.Events();
    const std::string_view engine = sdk::kEngineProviderId;

    chain::EventDefinition findPath = Action(
        {Input("from", ValueType::Vec3, Vec3{}), Input("to", ValueType::Vec3, Vec3{})},
        {{.name = "Found",
          .payload = {{.name = "distance", .type = ValueType::Float}, {.name = "next", .type = ValueType::Vec3}}},
         {.name = "NotFound", .payload = {}}},
        "Finds a walking route over the navigation mesh.",
        [source](chain::EventCall& call)
        {
            const std::shared_ptr<const NavMesh> mesh = source->Get();
            auto path = mesh != nullptr ? mesh->FindPath(call.Get<Vec3>(0), call.Get<Vec3>(1))
                                        : Result<NavPath>(MakeError(ErrorCode::NotFound));
            if (!path)
            {
                call.Fire(1);
                return;
            }
            const Vec3 next = path->points.size() > 1 ? path->points[1] : path->points.front();
            std::vector<values::Value> payload;
            payload.emplace_back(path->Length());
            payload.emplace_back(next);
            call.Fire(0, std::move(payload));
        });
    INTERI_TRY(events.Provide(engine, kNavFindPath, std::move(findPath)));
    chain::EventParam speed = Input("speed", ValueType::Float, 3.0f);
    speed.schema.meta.min = 0.0;
    speed.schema.meta.unit = "m/s";
    INTERI_TRY(events.Provide(engine, kNavMoveTo,
                              Action({TargetInput(), Input("destination", ValueType::Vec3, Vec3{}), std::move(speed)},
                                     {{.name = "Out", .payload = {}}},
                                     "Starts the entity's navigation agent walking to a point.",
                                     [](chain::EventCall& call)
                                     {
                                         const Vec3 destination = call.Get<Vec3>(1);
                                         const float walkSpeed = call.Get<float>(2);
                                         DeferAgent(call, TargetOf(call, 0),
                                                    [destination, walkSpeed](NavAgentComponent& agent)
                                                    {
                                                        agent.destination = destination;
                                                        agent.speed = walkSpeed;
                                                        agent.state = AgentState::Moving;
                                                    });
                                         call.Fire(0);
                                     })));
    INTERI_TRY(
        events.Provide(engine, kNavStop,
                       Action({TargetInput()}, {{.name = "Out", .payload = {}}}, "Stops the entity's navigation agent.",
                              [](chain::EventCall& call)
                              {
                                  DeferAgent(call, TargetOf(call, 0),
                                             [](NavAgentComponent& agent) { agent.state = AgentState::Idle; });
                                  call.Fire(0);
                              })));
    INTERI_TRY(events.Provide(engine, kTriggerArrived, Trigger("Fires for an agent that reached its destination.")));
    INTERI_TRY(events.Provide(engine, kTriggerFailed, Trigger("Fires for an agent with no route to its destination.")));
    return {};
}

} // namespace interi::nav
