// SPDX-License-Identifier: MIT
#pragma once

// Navigation agents (M24): entities that walk to a destination over a NavMesh. Chains order a move (nav.MoveTo,
// nav.Stop); the host steps the agents once per fixed step and raises the outcome as triggers for the chains
// (nav.Arrived, nav.Failed), which is how AI behaviours that modules contribute learn how a walk ended.
//
// An agent follows the straight path to its destination at `speed` metres per second, re-planning every step from
// where it stands, so a moved destination or a blocked route needs no extra handling. Agents are visited in stable-id
// order, so results do not depend on archetype or chunk order.

#include <interi/chain/Executor.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/nav/NavMesh.h>

#include <cstdint>
#include <string_view>
#include <vector>

namespace interi::nav
{

inline constexpr std::string_view kTriggerArrived = "nav.Arrived";
inline constexpr std::string_view kTriggerFailed = "nav.Failed";

enum class AgentState : std::uint8_t
{
    Idle,
    Moving
};

struct NavAgentComponent
{
    static constexpr const char* kComponentName = "interi.NavAgent";

    Vec3 destination;
    float speed = 3.0f;         ///< Metres per second.
    float arriveRadius = 0.15f; ///< Within this distance of the destination the walk is done.
    AgentState state = AgentState::Idle;
};

struct AgentMove
{
    std::uint64_t entity = 0; ///< Stable id.
    Vec3 position;
};

/// What one step did, in stable-id order.
struct NavStepResult
{
    std::vector<AgentMove> moves;       ///< Every moving agent's new position (also written to its Transform).
    std::vector<std::uint64_t> arrived; ///< Stable ids of agents whose walk ended at the destination.
    std::vector<std::uint64_t> failed;  ///< No route: off the mesh, or the destination is unreachable.
};

/// Steps every Moving agent (NavAgentComponent + chain::StableIdComponent + Transform) by `dt` seconds. Without a mesh
/// every moving agent fails.
NavStepResult StepNavAgents(World& world, const NavMesh* mesh, float dt);

/// Raises nav.Arrived / nav.Failed for the next chain step.
void RaiseNavTriggers(chain::ChainExecutor& executor, const NavStepResult& result);

} // namespace interi::nav
