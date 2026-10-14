// SPDX-License-Identifier: MIT
#pragma once

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceTable.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterTable.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterTable.h>
// clang-format on

#include <cstdint>

namespace interi::physics
{

/// Object layers, tuned for destruction. Which pairs collide is `ShouldCollide` below.
enum class ObjectLayer : std::uint8_t
{
    Static,      ///< Terrain and never-moving level geometry.
    Structure,   ///< Intact building parts (static or kinematic) before they break off.
    Debris,      ///< Broken-off chunks large enough to matter for gameplay.
    DebrisSmall, ///< Rubble: no collision with other rubble, characters or projectiles.
    Character,
    Vehicle,
    Projectile,
    Count
};

inline constexpr std::uint32_t kObjectLayerCount = static_cast<std::uint32_t>(ObjectLayer::Count);

/// Broadphase layers (one bounding-volume tree each). Static and Structure share the rarely-changing tree; debris
/// gets its own, which mostly holds sleeping bodies; the few always-moving bodies (characters, vehicles,
/// projectiles) stay in a small tree that is cheap to update and query.
namespace BroadPhaseLayers
{
inline constexpr JPH::BroadPhaseLayer kStatic{0};
inline constexpr JPH::BroadPhaseLayer kDebris{1};
inline constexpr JPH::BroadPhaseLayer kMoving{2};
inline constexpr std::uint32_t kCount = 3;
} // namespace BroadPhaseLayers

[[nodiscard]] constexpr JPH::ObjectLayer ToJolt(ObjectLayer layer) noexcept
{
    return static_cast<JPH::ObjectLayer>(layer);
}

[[nodiscard]] JPH::BroadPhaseLayer BroadPhaseLayerOf(ObjectLayer layer) noexcept;

/// True if bodies in these layers collide (symmetric).
[[nodiscard]] bool ShouldCollide(ObjectLayer a, ObjectLayer b) noexcept;

namespace detail
{
/// The object pair table and broadphase mapping, filled from ShouldCollide and BroadPhaseLayerOf.
struct LayerTables
{
    LayerTables();

    JPH::ObjectLayerPairFilterTable pairs;
    JPH::BroadPhaseLayerInterfaceTable broadPhase;
};
} // namespace detail

/// The three filters Jolt needs, built from ShouldCollide and BroadPhaseLayerOf. The object-vs-broadphase filter is
/// derived from the pair table, so the two can never disagree.
class LayerFilters
{
public:
    LayerFilters();

    [[nodiscard]] const JPH::BroadPhaseLayerInterface& BroadPhaseLayers() const noexcept { return m_tables.broadPhase; }
    [[nodiscard]] const JPH::ObjectVsBroadPhaseLayerFilter& ObjectVsBroadPhase() const noexcept
    {
        return m_objectVsBroadPhase;
    }
    [[nodiscard]] const JPH::ObjectLayerPairFilter& ObjectPairs() const noexcept { return m_tables.pairs; }

private:
    detail::LayerTables m_tables;
    JPH::ObjectVsBroadPhaseLayerFilterTable m_objectVsBroadPhase; ///< Built from m_tables.
};

} // namespace interi::physics
