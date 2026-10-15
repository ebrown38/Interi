// SPDX-License-Identifier: MIT
#include <interi/physics/PhysicsLayers.h>

#include <gtest/gtest.h>

#include <cstdint>

using interi::physics::BroadPhaseLayerOf;
using interi::physics::kObjectLayerCount;
using interi::physics::LayerFilters;
using interi::physics::ObjectLayer;
using interi::physics::ShouldCollide;
using interi::physics::ToJolt;
namespace BroadPhaseLayers = interi::physics::BroadPhaseLayers;

namespace
{

ObjectLayer Layer(std::uint32_t index)
{
    return static_cast<ObjectLayer>(index);
}

} // namespace

TEST(LayerFilter, DebrisSmallVsDebrisSmall_NoCollision)
{
    const LayerFilters filters;
    EXPECT_FALSE(ShouldCollide(ObjectLayer::DebrisSmall, ObjectLayer::DebrisSmall));
    EXPECT_FALSE(
        filters.ObjectPairs().ShouldCollide(ToJolt(ObjectLayer::DebrisSmall), ToJolt(ObjectLayer::DebrisSmall)));
}

TEST(LayerFilter, CollisionTable_MatchesDesign)
{
    using L = ObjectLayer;
    // Rows/columns: Static, Structure, Debris, DebrisSmall, Character, Vehicle, Projectile.
    constexpr bool kExpected[kObjectLayerCount][kObjectLayerCount] = {
        {false, false, true, true, true, true, true},  // Static
        {false, false, true, true, true, true, true},  // Structure
        {true, true, true, true, true, true, true},    // Debris
        {true, true, true, false, false, true, false}, // DebrisSmall
        {true, true, true, false, true, true, true},   // Character
        {true, true, true, true, true, true, true},    // Vehicle
        {true, true, true, false, true, true, false},  // Projectile
    };
    static_assert(kObjectLayerCount == 7);
    for (std::uint32_t a = 0; a < kObjectLayerCount; ++a)
    {
        for (std::uint32_t b = 0; b < kObjectLayerCount; ++b)
        {
            EXPECT_EQ(ShouldCollide(Layer(a), Layer(b)), kExpected[a][b]) << a << " vs " << b;
        }
    }
    EXPECT_FALSE(ShouldCollide(L::Static, L::Structure));
}

TEST(LayerFilter, JoltFilters_AgreeWithTable)
{
    const LayerFilters filters;
    for (std::uint32_t a = 0; a < kObjectLayerCount; ++a)
    {
        EXPECT_EQ(filters.BroadPhaseLayers().GetBroadPhaseLayer(ToJolt(Layer(a))), BroadPhaseLayerOf(Layer(a)));
        for (std::uint32_t b = 0; b < kObjectLayerCount; ++b)
        {
            const bool collide = ShouldCollide(Layer(a), Layer(b));
            EXPECT_EQ(filters.ObjectPairs().ShouldCollide(ToJolt(Layer(a)), ToJolt(Layer(b))), collide);
            // A colliding pair must never be rejected by the broadphase filter, or contacts would silently vanish.
            if (collide)
            {
                EXPECT_TRUE(filters.ObjectVsBroadPhase().ShouldCollide(ToJolt(Layer(a)), BroadPhaseLayerOf(Layer(b))))
                    << a << " vs " << b;
            }
        }
    }
}

TEST(LayerFilter, BroadPhase_DebrisSeparateFromMoving)
{
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::Static), BroadPhaseLayers::kStatic);
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::Structure), BroadPhaseLayers::kStatic);
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::Debris), BroadPhaseLayers::kDebris);
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::DebrisSmall), BroadPhaseLayers::kDebris);
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::Character), BroadPhaseLayers::kMoving);
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::Vehicle), BroadPhaseLayers::kMoving);
    EXPECT_EQ(BroadPhaseLayerOf(ObjectLayer::Projectile), BroadPhaseLayers::kMoving);
}
