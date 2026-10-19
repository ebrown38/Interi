// SPDX-License-Identifier: MIT
#pragma once

// Chain Reaction events for navigation. Shared contract ids the engine declares (NavigationContracts).
//
//   nav.FindPath  from (vec3), to (vec3)             -> Found {distance: float, next: vec3} | NotFound
//                 distance is the walking distance, next the first waypoint after `from` (steer towards it).
//                 A partial path (goal unreachable) is Found with the distance to the closest reachable point.
//   nav.MoveTo    target, destination (vec3), speed  -> Out   starts the entity's NavAgent walking
//   nav.Stop      target                             -> Out   stops it (no trigger is raised)
//   nav.Arrived   (trigger, raised by RaiseNavTriggers for the agent's entity)
//   nav.Failed    (trigger, the same when there is no route)
//
// nav.FindPath reads the mesh from a NavSource while the wave runs (possibly on a worker); the host swaps the mesh
// between steps, never during one.

#include <interi/chain/Event.h>
#include <interi/core/Result.h>
#include <interi/nav/NavMesh.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace interi::nav
{

inline constexpr std::string_view kNavFindPath = "nav.FindPath";
inline constexpr std::string_view kNavMoveTo = "nav.MoveTo";
inline constexpr std::string_view kNavStop = "nav.Stop";

/// The current navigation mesh of a game, shared between the host and the events.
class NavSource
{
public:
    void Set(std::shared_ptr<const NavMesh> mesh) noexcept { m_mesh.store(std::move(mesh)); }
    [[nodiscard]] std::shared_ptr<const NavMesh> Get() const noexcept { return m_mesh.load(); }

private:
    std::atomic<std::shared_ptr<const NavMesh>> m_mesh;
};

[[nodiscard]] std::vector<std::string> NavigationContracts();

/// Registers the events and triggers as the engine. The registry's ModuleSet must declare NavigationContracts().
/// `source` must outlive every chain that can run (the events hold a reference).
Result<void> RegisterNavigationLibrary(chain::ChainRegistry& registry, std::shared_ptr<NavSource> source);

} // namespace interi::nav
