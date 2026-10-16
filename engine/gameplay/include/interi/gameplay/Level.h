// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Transform.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include <string>
#include <string_view>
#include <vector>

namespace interi
{
class IFileSystem;
}

namespace interi::gameplay
{

struct StaticBoxDef
{
    Transform transform; ///< Position and rotation (scale ignored).
    Vec3 halfExtents;
};

struct DestructibleDef
{
    std::string asset; ///< Virtual path of a cooked `.idest`.
    Transform transform;
};

struct SpawnPoint
{
    std::string name;
    Vec3 position;    ///< Feet.
    float yaw = 0.0f; ///< Radians (FollowCamera convention: 0 looks along -Z).
};

/// A level file:
///
///     { "staticBoxes":  [ { "position": [0, -0.5, 0], "halfExtents": [50, 0.5, 50], "rotation": [0, 0, 0, 1] } ],
///       "destructibles": [ { "asset": "levels/tower.idest", "position": [0, 0, -10], "rotation": [0, 0, 0, 1] } ],
///       "spawnPoints":  [ { "name": "player", "position": [0, 0, 8], "yawDegrees": 0 } ] }
///
/// Every list is optional; rotation is an (x, y, z, w) quaternion, normalised on load (default identity).
struct LevelDef
{
    std::vector<StaticBoxDef> staticBoxes;
    std::vector<DestructibleDef> destructibles;
    std::vector<SpawnPoint> spawnPoints;
    std::vector<std::string> warnings; ///< Unknown fields (each also logged).
};

/// InvalidArgument for malformed JSON, wrong types, a missing required field (halfExtents, asset, position, a spawn
/// point name), non-positive half extents or a zero rotation.
[[nodiscard]] Result<LevelDef> ParseLevel(std::string_view json);
[[nodiscard]] Result<LevelDef> LoadLevel(const IFileSystem& files, std::string_view path);

/// What SpawnLevel created.
struct LoadedLevel
{
    std::vector<Entity> staticEntities;
    std::vector<destruction::DestructibleId> destructibles; ///< In file order.
    std::vector<SpawnPoint> spawnPoints;

    /// The spawn point called `name`, or the first one, or nullptr if there are none.
    [[nodiscard]] const SpawnPoint* FindSpawnPoint(std::string_view name) const noexcept;
};

/// Creates the level's static boxes (ECS entities with static bodies) and spawns its destructibles (each asset read
/// once through `files`). Everything is created in file order, so body creation stays deterministic. Every asset is
/// read before anything is created, so an asset error creates nothing.
Result<LoadedLevel> SpawnLevel(const LevelDef& level, const IFileSystem& files, World& world, JobSystem& jobs,
                               destruction::DestructionWorld& destruction);

/// Removes everything SpawnLevel created (for reloading a level).
void DespawnLevel(LoadedLevel& level, World& world, destruction::DestructionWorld& destruction);

} // namespace interi::gameplay
