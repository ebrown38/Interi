// SPDX-License-Identifier: MIT
#include <interi/ecs/Query.h>
#include <interi/nav/NavAgents.h>

#include <algorithm>
#include <utility>

namespace interi::nav
{

namespace
{

struct Walker
{
    std::uint64_t id;
    Entity entity;
};

/// Moves along `points` (the first is the agent's position) by `distance`; true when the end was reached.
bool Advance(const std::vector<Vec3>& points, float distance, Vec3& position)
{
    position = points.front();
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const float segment = Length(points[i] - points[i - 1]);
        if (distance <= segment && segment > 0.0f)
        {
            position = points[i - 1] + (points[i] - points[i - 1]) * (distance / segment);
            return false;
        }
        distance -= segment;
        position = points[i];
    }
    return true;
}

} // namespace

NavStepResult StepNavAgents(World& world, const NavMesh* mesh, float dt)
{
    std::vector<Walker> walkers;
    Query().With<NavAgentComponent, chain::StableIdComponent, Transform>().ForEachChunk(
        world,
        [&](ChunkView& view)
        {
            const auto agents = view.Read<NavAgentComponent>();
            const auto ids = view.Read<chain::StableIdComponent>();
            const auto entities = view.Entities();
            for (std::size_t i = 0; i < agents.size(); ++i)
            {
                if (agents[i].state == AgentState::Moving)
                {
                    walkers.push_back({.id = ids[i].id, .entity = entities[i]});
                }
            }
        });
    std::ranges::sort(walkers, {}, &Walker::id);

    NavStepResult result;
    for (const Walker& walker : walkers)
    {
        auto* agent = world.GetMut<NavAgentComponent>(walker.entity);
        auto* transform = world.GetMut<Transform>(walker.entity);
        const auto fail = [&]
        {
            agent->state = AgentState::Idle;
            result.failed.push_back(walker.id);
        };
        if (mesh == nullptr)
        {
            fail();
            continue;
        }
        auto path = mesh->FindPath(transform->position, agent->destination);
        if (!path || path->partial)
        {
            fail();
            continue;
        }
        Vec3 position;
        const bool done = Advance(path->points, agent->speed * dt, position);
        const float remaining = Length(agent->destination - position);
        transform->position = position;
        result.moves.push_back({.entity = walker.id, .position = position});
        if (done || remaining <= agent->arriveRadius)
        {
            agent->state = AgentState::Idle;
            result.arrived.push_back(walker.id);
        }
    }
    return result;
}

void RaiseNavTriggers(chain::ChainExecutor& executor, const NavStepResult& result)
{
    for (const std::uint64_t id : result.arrived)
    {
        executor.RaiseTrigger(kTriggerArrived, id);
    }
    for (const std::uint64_t id : result.failed)
    {
        executor.RaiseTrigger(kTriggerFailed, id);
    }
}

} // namespace interi::nav
