// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>
#include <interi/physics/PhysicsSync.h>
#include <interi/testing/StateHasher.h>

#include "../physics/PhysicsTestUtil.h"
#include "RuntimeTestUtil.h"

// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLock.h>
// clang-format on

#include <bit>
#include <cstdint>
#include <memory>
#include <vector>

namespace interi::destruction::testing
{

inline constexpr float kScenarioStep = 1.0f / 60.0f;

/// Concrete limits for 1 m^2 faces (N) and 1 m^3 chunks of 2400 kg.
inline GridAssetOptions Concrete()
{
    GridAssetOptions options;
    options.mass = 2400.0f;
    options.fractureEnergy = 100.0f;
    options.limit = 3.0e6f; // tensile; compression and shear set below
    return options;
}

inline DestructibleAsset WithConcreteLimits(DestructibleAsset asset)
{
    for (Bond& bond : asset.bonds)
    {
        bond.compressiveLimit = 30.0e6f;
        bond.tensileLimit = 3.0e6f;
        bond.shearLimit = 4.0e6f;
    }
    return asset;
}

/// A 4 x 4 two-storey frame: corner columns (2 m tall) under a slab at y = 2, corner columns again, and a roof slab
/// at y = 5. Anchored at y = 0.
inline std::vector<Cell> TwoStoryBuildingCells()
{
    std::vector<Cell> cells;
    const auto columns = [&](int y0, int y1)
    {
        for (int y = y0; y <= y1; ++y)
        {
            for (const int x : {0, 3})
            {
                for (const int z : {0, 3})
                {
                    cells.push_back({x, y, z});
                }
            }
        }
    };
    const auto slab = [&](int y)
    {
        for (int z = 0; z < 4; ++z)
        {
            for (int x = 0; x < 4; ++x)
            {
                cells.push_back({x, y, z});
            }
        }
    };
    columns(0, 1);
    slab(2);
    columns(3, 4);
    slab(5);
    return cells;
}

/// Physics, ECS and destruction for one scenario, stepped like a game frame.
struct Scenario
{
    explicit Scenario(std::uint32_t workers, const DestructionSettings& settings = {})
        : jobs(physics::testing::Workers(workers))
        , physicsWorld(jobs)
        , sync(physicsWorld)
        , destruction(physicsWorld, settings)
    {
        physics::testing::AddGround(physicsWorld);
    }

    DestructibleId Spawn(DestructibleAsset asset)
    {
        return destruction.Spawn(world, jobs, std::make_shared<const DestructibleAsset>(std::move(asset)), {});
    }

    void Step()
    {
        sync.Step(world, jobs, kScenarioStep);
        destruction.Update(world, jobs);
    }

    /// World position of a chunk's centroid (through its piece's pose).
    [[nodiscard]] Vec3 ChunkPosition(DestructibleId id, std::uint32_t chunk) const
    {
        const Vec3 local = destruction.Asset(id).chunks[chunk].centroid;
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            for (const std::uint32_t c : destruction.PieceChunks(id, piece))
            {
                if (c == chunk)
                {
                    const Entity entity = destruction.PieceEntity(id, piece);
                    const Transform* pose = entity.IsValid() ? world.Get<Transform>(entity) : nullptr;
                    return pose != nullptr ? pose->TransformPoint(local) : local;
                }
            }
        }
        return local;
    }

    [[nodiscard]] bool ChunkAnchored(DestructibleId id, std::uint32_t chunk) const
    {
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            for (const std::uint32_t c : destruction.PieceChunks(id, piece))
            {
                if (c == chunk)
                {
                    return destruction.PieceAnchored(id, piece);
                }
            }
        }
        return false;
    }

    /// Kinetic plus potential energy (relative to y = 0) of every dynamic piece body of the instance.
    [[nodiscard]] double DynamicEnergy(DestructibleId id) const
    {
        double energy = 0.0;
        const Vec3 gravity = physics::FromJolt(physicsWorld.System().GetGravity());
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            const Entity entity = destruction.PieceEntity(id, piece);
            if (destruction.PieceAnchored(id, piece) || !world.IsAlive(entity))
            {
                continue;
            }
            const auto* handle = world.Get<physics::PhysicsBodyComponent>(entity);
            if (handle == nullptr)
            {
                continue;
            }
            const JPH::BodyLockRead lock(physicsWorld.System().GetBodyLockInterface(), handle->id);
            const JPH::Body& body = lock.GetBody();
            const JPH::MotionProperties& motion = *body.GetMotionProperties();
            const double mass = 1.0 / static_cast<double>(motion.GetInverseMass());
            const JPH::Vec3 v = body.GetLinearVelocity();
            const JPH::Vec3 w = body.GetAngularVelocity();
            const JPH::Vec3 iw = body.GetInverseInertia().Inversed3x3().Multiply3x3(w);
            const Vec3 com = physics::FromJolt(body.GetCenterOfMassPosition());
            energy += 0.5 * mass * static_cast<double>(v.LengthSq()) + 0.5 * static_cast<double>(w.Dot(iw)) -
                      mass * static_cast<double>(Dot(gravity, com));
        }
        return energy;
    }

    /// Physics state plus every instance's bond health and pieces.
    [[nodiscard]] std::uint64_t Hash(DestructibleId id) const
    {
        interi::testing::StateHasher hasher;
        hasher.Add(physics::testing::HashState(physicsWorld));
        for (const float health : destruction.BondHealth(id))
        {
            hasher.Add(std::bit_cast<std::uint32_t>(health));
        }
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            hasher.Add(static_cast<std::uint32_t>(destruction.PieceChunks(id, piece).size()));
            for (const std::uint32_t chunk : destruction.PieceChunks(id, piece))
            {
                hasher.Add(chunk);
            }
        }
        return hasher.Finish();
    }

    JobSystem jobs;
    physics::PhysicsWorld physicsWorld;
    physics::PhysicsSync sync;
    World world;
    DestructionWorld destruction;
};

/// Index of a cell in `cells`.
inline std::uint32_t CellIndex(const std::vector<Cell>& cells, const Cell& cell)
{
    for (std::uint32_t i = 0; i < cells.size(); ++i)
    {
        if (cells[i] == cell)
        {
            return i;
        }
    }
    return UINT32_MAX;
}

} // namespace interi::destruction::testing
