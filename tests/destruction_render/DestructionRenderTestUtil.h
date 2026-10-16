// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Transform.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/physics/PhysicsWorld.h>

#include "../physics/PhysicsTestUtil.h"
#include <gtest/gtest.h>

#include <memory>
#include <utility>
#include <vector>

namespace interi::destruction::testing
{

/// A concrete wall standing on y = 0: x in [-2, 2], y in [0, 3], z in [-0.15, 0.15]. Fractured into 4 regions,
/// then up to 10 pieces per region (deterministic seeds). Exterior faces are surface material 0, fracture faces
/// interior material 0.
inline std::shared_ptr<const DestructibleAsset> MakeFracturedWall(JobSystem& jobs)
{
    HierarchySettings hierarchy;
    for (const auto& [count, seed] : {std::pair{4u, 11u}, std::pair{10u, 12u}})
    {
        SeedSettings level;
        level.count = count;
        level.randomSeed = seed;
        hierarchy.levels.push_back(level);
    }
    const ChunkHierarchy chunks =
        BuildHierarchy(MakeBoxMesh({-2.0f, 0.0f, -0.15f}, {2.0f, 3.0f, 0.15f}), hierarchy, jobs);
    std::vector<DestructionMaterial> materials(1);
    materials[0].name = "concrete";
    BondSettings bonds;
    bonds.groundHeight = 0.0f;
    BondGraph graph = BuildBondGraph(chunks.chunks, materials, bonds);
    Result<DestructibleAsset> asset = AssembleAsset(chunks.chunks, std::move(graph), materials, {}, jobs);
    EXPECT_TRUE(asset) << asset.error().message;
    return std::make_shared<const DestructibleAsset>(std::move(*asset));
}

/// Physics, ECS and destruction for a few fixed steps.
struct DestructionScene
{
    explicit DestructionScene(const DestructionSettings& settings = {}, std::uint32_t workers = 2)
        : jobs(physics::testing::Workers(workers))
        , physics(jobs)
        , sync(physics)
        , destruction(physics, settings)
    {
        physics::testing::AddGround(physics);
    }

    DestructibleId Spawn(std::shared_ptr<const DestructibleAsset> asset, const Vec3& position = {})
    {
        Transform transform;
        transform.position = position;
        return destruction.Spawn(world, jobs, std::move(asset), transform);
    }

    void Step(int count = 1)
    {
        for (int i = 0; i < count; ++i)
        {
            sync.Step(world, jobs, 1.0f / 60.0f);
            destruction.Update(world, jobs);
            if (debris != nullptr)
            {
                debris->Update(world, jobs, DebrisView{}, 1.0f / 60.0f);
            }
        }
    }

    JobSystem jobs;
    physics::PhysicsWorld physics;
    physics::PhysicsSync sync;
    DestructionWorld destruction;
    std::unique_ptr<DebrisSystem> debris;
    World world;
};

} // namespace interi::destruction::testing
