// SPDX-License-Identifier: MIT
#include <interi/physics/PhysicsLayers.h>

#include <array>
#include <cstddef>

namespace interi::physics
{

namespace
{

using L = ObjectLayer;

constexpr std::size_t Index(ObjectLayer layer) noexcept
{
    return static_cast<std::size_t>(layer);
}

/// Which layers each layer collides with; the table is symmetric.
constexpr std::array<std::array<bool, kObjectLayerCount>, kObjectLayerCount> kCollides = []
{
    std::array<std::array<bool, kObjectLayerCount>, kObjectLayerCount> table{};
    const auto set = [&](ObjectLayer a, ObjectLayer b)
    {
        table[Index(a)][Index(b)] = true;
        table[Index(b)][Index(a)] = true;
    };
    // Static and Structure never move on their own, so they don't collide with each other.
    for (const ObjectLayer fixed : {L::Static, L::Structure})
    {
        for (const ObjectLayer moving : {L::Debris, L::DebrisSmall, L::Character, L::Vehicle, L::Projectile})
        {
            set(fixed, moving);
        }
    }
    for (const ObjectLayer other : {L::Debris, L::DebrisSmall, L::Character, L::Vehicle, L::Projectile})
    {
        set(L::Debris, other);
    }
    // Rubble only rests on things and is pushed around by vehicles: thousands of small pieces must not collide with
    // each other, trip characters or stop bullets.
    set(L::DebrisSmall, L::Vehicle);
    for (const ObjectLayer other : {L::Character, L::Vehicle, L::Projectile})
    {
        set(L::Character, other);
    }
    set(L::Vehicle, L::Vehicle);
    set(L::Vehicle, L::Projectile);
    return table;
}();

} // namespace

JPH::BroadPhaseLayer BroadPhaseLayerOf(ObjectLayer layer) noexcept
{
    switch (layer)
    {
    case L::Static:
    case L::Structure:
        return BroadPhaseLayers::kStatic;
    case L::Debris:
    case L::DebrisSmall:
        return BroadPhaseLayers::kDebris;
    case L::Character:
    case L::Vehicle:
    case L::Projectile:
    case L::Count:
        break;
    }
    return BroadPhaseLayers::kMoving;
}

bool ShouldCollide(ObjectLayer a, ObjectLayer b) noexcept
{
    return kCollides[Index(a)][Index(b)];
}

detail::LayerTables::LayerTables()
    : pairs(kObjectLayerCount)
    , broadPhase(kObjectLayerCount, BroadPhaseLayers::kCount)
{
    for (std::uint32_t a = 0; a < kObjectLayerCount; ++a)
    {
        const auto layerA = static_cast<ObjectLayer>(a);
        broadPhase.MapObjectToBroadPhaseLayer(ToJolt(layerA), BroadPhaseLayerOf(layerA));
        for (std::uint32_t b = 0; b < kObjectLayerCount; ++b)
        {
            if (ShouldCollide(layerA, static_cast<ObjectLayer>(b)))
            {
                pairs.EnableCollision(ToJolt(layerA), static_cast<JPH::ObjectLayer>(b));
            }
        }
    }
}

LayerFilters::LayerFilters()
    : m_objectVsBroadPhase(m_tables.broadPhase, BroadPhaseLayers::kCount, m_tables.pairs, kObjectLayerCount)
{
}

} // namespace interi::physics
