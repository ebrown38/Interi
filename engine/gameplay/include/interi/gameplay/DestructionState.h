// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/core/math/Transform.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/PhysicsWorld.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace interi::gameplay
{

/// A falling piece: identified by its smallest chunk (pieces never share chunks).
struct PieceState
{
    std::uint32_t firstChunk = 0;
    Transform pose;
    Vec3 linearVelocity;
    Vec3 angularVelocity;
};

struct InstanceDamageState
{
    std::vector<float> bondHealth;             ///< Per bond; 0 = broken.
    std::vector<std::uint32_t> releasedChunks; ///< Leaves handed to the DebrisSystem (they don't come back).
    std::vector<PieceState> pieces;            ///< Dynamic pieces still owned by the instance, by firstChunk.
};

/// The damage of every destructible, in DestructionWorld::Instances() order: broken bonds (the bond health array)
/// and the pose and motion of the pieces that have broken off. Load the same level first, then restore into it.
///
/// Not saved: entities owned by the DebrisSystem (small debris, rubble piles), which are gone after a load, and the
/// stress solver's warm start (the next solve starts cold, so the simulation continues equivalently, not
/// bit-identically).
struct DestructionState
{
    std::vector<InstanceDamageState> instances;
};

inline constexpr FourCC kDestructionStateMagic = MakeFourCC("IDSS");
/// Bump when the saved layout changes (fields added later are read behind `Version() >= N`).
inline constexpr std::uint32_t kDestructionStateVersion = 1;

[[nodiscard]] DestructionState CaptureDestructionState(const World& world,
                                                       const destruction::DestructionWorld& destruction,
                                                       physics::PhysicsWorld& physics);

/// Restores `state` into a world whose instances match it (same assets, in the same order, no more damaged than
/// the save). Falling pieces get their saved pose and velocities (also when their bodies already exist).
/// InvalidArgument if the instance count or an instance's sizes differ; restoring stops at the first bad instance.
Result<> RestoreDestructionState(World& world, JobSystem& jobs, destruction::DestructionWorld& destruction,
                                 physics::PhysicsWorld& physics, const DestructionState& state);

/// Little-endian binary: magic IDSS, version, payload hash (XXH3), payload. Bit-exact.
[[nodiscard]] std::vector<std::byte> WriteDestructionState(const DestructionState& state);
/// Corrupt for a damaged or truncated file, VersionMismatch for one written by a newer build.
[[nodiscard]] Result<DestructionState> ReadDestructionState(std::span<const std::byte> data);

} // namespace interi::gameplay
