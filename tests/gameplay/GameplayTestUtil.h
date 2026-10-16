// SPDX-License-Identifier: MIT
#pragma once

// A small physics scene for gameplay tests: ECS world, Jolt through PhysicsSync, and helpers for boxes.

#include <interi/core/math/Transform.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>

#include <cstdint>

namespace interi::testing
{

inline constexpr float kGameplayStep = 1.0f / 60.0f;

class GameplayScene
{
public:
    explicit GameplayScene(std::uint32_t workers = 2)
        : jobs(Desc(workers))
        , physics(jobs)
        , sync(physics)
    {
    }

    /// Static box; ground by default: top face at y = 0, 100 m wide.
    Entity AddStaticBox(const Vec3& center, const Vec3& halfExtents,
                        physics::ObjectLayer layer = physics::ObjectLayer::Static)
    {
        Transform pose;
        pose.position = center;
        physics::RigidBodyComponent body;
        body.motion = physics::MotionType::Static;
        body.layer = layer;
        return world.CreateEntity(pose, body, physics::ColliderComponent::Box(halfExtents));
    }

    Entity AddGround() { return AddStaticBox(Vec3(0.0f, -0.5f, 0.0f), Vec3(50.0f, 0.5f, 50.0f)); }

    Entity AddDynamicBox(const Vec3& center, const Vec3& halfExtents, float mass,
                         physics::ObjectLayer layer = physics::ObjectLayer::Debris)
    {
        Transform pose;
        pose.position = center;
        physics::RigidBodyComponent body;
        body.layer = layer;
        body.mass = mass;
        return world.CreateEntity(pose, body, physics::ColliderComponent::Box(halfExtents));
    }

    /// One fixed step of the ECS bodies.
    void Step() { sync.Step(world, jobs, kGameplayStep); }

    [[nodiscard]] Vec3 PositionOf(Entity entity) const { return world.Get<Transform>(entity)->position; }

    JobSystem jobs;
    physics::PhysicsWorld physics;
    physics::PhysicsSync sync;
    World world;

private:
    static JobSystemDesc Desc(std::uint32_t workers)
    {
        JobSystemDesc desc;
        desc.workerCount = workers;
        return desc;
    }
};

} // namespace interi::testing
