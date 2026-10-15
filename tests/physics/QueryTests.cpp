// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/physics/Queries.h>
#include <interi/testing/ExpectNear.h>

#include "PhysicsTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using interi::Entity;
using interi::JobSystem;
using interi::Transform;
using interi::Vec3;
using interi::World;
using namespace interi::physics;
using namespace interi::physics::testing;

namespace
{

class QueryTest : public ::testing::Test
{
protected:
    Entity Spawn(const Vec3& position, ObjectLayer layer = ObjectLayer::Debris, float halfExtent = 0.5f)
    {
        RigidBodyComponent body;
        body.motion = MotionType::Static; // queries only: keep everything where it was put
        body.layer = layer;
        Transform transform;
        transform.position = position;
        const Entity entity = m_world.CreateEntity(body, ColliderComponent::Box(Vec3(halfExtent)), transform);
        m_sync.CreateBodies(m_world);
        return entity;
    }

    JobSystem m_jobs{Workers(1)};
    PhysicsWorld m_physics{m_jobs};
    World m_world;
    PhysicsSync m_sync{m_physics};
};

} // namespace

TEST_F(QueryTest, Raycast_HitsNearestBody)
{
    const Entity near = Spawn(Vec3(5.0f, 0.0f, 0.0f));
    Spawn(Vec3(10.0f, 0.0f, 0.0f));
    Spawn(Vec3(-5.0f, 0.0f, 0.0f)); // behind the ray

    const auto hit = Raycast(m_physics, Vec3(0.0f, 0.0f, 0.0f), Vec3(20.0f, 0.0f, 0.0f));

    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, near);
    INTERI_EXPECT_VEC_NEAR(hit->point, Vec3(4.5f, 0.0f, 0.0f), 1e-3f);
    INTERI_EXPECT_VEC_NEAR(hit->normal, Vec3(-1.0f, 0.0f, 0.0f), 1e-3f);
    EXPECT_NEAR(hit->fraction, 4.5f / 20.0f, 1e-4f);
}

TEST_F(QueryTest, Raycast_TooShortOrMasked_Misses)
{
    Spawn(Vec3(5.0f, 0.0f, 0.0f), ObjectLayer::DebrisSmall);
    EXPECT_FALSE(Raycast(m_physics, Vec3(0.0f), Vec3(4.0f, 0.0f, 0.0f)).has_value());
    EXPECT_FALSE(
        Raycast(m_physics, Vec3(0.0f), Vec3(20.0f, 0.0f, 0.0f), kAllLayers & ~LayerBit(ObjectLayer::DebrisSmall))
            .has_value());
    EXPECT_TRUE(
        Raycast(m_physics, Vec3(0.0f), Vec3(20.0f, 0.0f, 0.0f), LayerBit(ObjectLayer::DebrisSmall)).has_value());
}

TEST_F(QueryTest, SphereCast_HitsWhereSphereFirstTouches)
{
    const Entity target = Spawn(Vec3(5.0f, 0.0f, 0.0f));
    // A 1 m sphere whose bottom passes 0.1 m above the box's top face misses it.
    const auto miss = SphereCast(m_physics, Vec3(0.0f, 1.6f, 0.0f), 1.0f, Vec3(10.0f, 0.0f, 0.0f));
    EXPECT_FALSE(miss.has_value());

    const auto hit = SphereCast(m_physics, Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(10.0f, 0.0f, 0.0f));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->entity, target);
    EXPECT_NEAR(hit->fraction, 3.5f / 10.0f, 1e-3f); // sphere surface reaches x = 4.5 after moving 3.5 m
    INTERI_EXPECT_VEC_NEAR(hit->normal, Vec3(-1.0f, 0.0f, 0.0f), 1e-3f);
}

TEST_F(QueryTest, SphereOverlap_ReturnsAllInRadius)
{
    std::vector<Entity> inside;
    for (int i = 0; i < 6; ++i)
    {
        // Ring of boxes at distance 3 (inner face at 2.5): inside a radius-3 sphere.
        const float angle = static_cast<float>(i) * 1.047f;
        inside.push_back(Spawn(Vec3(3.0f * std::cos(angle), 0.0f, 3.0f * std::sin(angle)), ObjectLayer::Debris));
    }
    Spawn(Vec3(0.0f, 0.0f, 4.0f), ObjectLayer::DebrisSmall); // inner face at 3.5: outside
    Spawn(Vec3(8.0f, 0.0f, 0.0f));                           // far away
    const Entity centre = Spawn(Vec3(0.0f, 0.0f, 0.0f), ObjectLayer::Structure);
    inside.push_back(centre);

    std::vector<QueryHit> hits;
    OverlapSphere(m_physics, Vec3(0.0f), 3.0f, hits);

    std::vector<Entity> found;
    for (const QueryHit& hit : hits)
    {
        found.push_back(hit.entity);
    }
    std::ranges::sort(inside);
    std::ranges::sort(found);
    EXPECT_EQ(found, inside);
    // Sorted by body ID, one entry per body.
    EXPECT_TRUE(std::ranges::is_sorted(hits, {}, [](const QueryHit& h) { return h.body.GetIndexAndSequenceNumber(); }));

    OverlapSphere(m_physics, Vec3(0.0f), 3.0f, hits, LayerBit(ObjectLayer::Structure));
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].entity, centre);
}

TEST_F(QueryTest, SphereOverlap_CompoundReportedOnce)
{
    RigidBodyComponent body;
    body.motion = MotionType::Static;
    const ColliderPart parts[] = {
        {.position = Vec3(-1.0f, 0.0f, 0.0f),
         .rotation = interi::Quat::Identity(),
         .collider = ColliderComponent::Box(Vec3(0.5f))},
        {.position = Vec3(1.0f, 0.0f, 0.0f),
         .rotation = interi::Quat::Identity(),
         .collider = ColliderComponent::Box(Vec3(0.5f))},
    };
    auto compound = ColliderComponent::Compound(parts);
    ASSERT_TRUE(compound.has_value());
    m_world.CreateEntity(body, std::move(*compound), Transform{});
    m_sync.CreateBodies(m_world);

    std::vector<QueryHit> hits;
    OverlapSphere(m_physics, Vec3(0.0f), 2.0f, hits); // touches both parts
    EXPECT_EQ(hits.size(), 1u);
}
