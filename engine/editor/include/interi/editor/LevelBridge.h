// SPDX-License-Identifier: MIT
#pragma once

// The bridge between scenes and the legacy level format (gameplay::LevelDef), which the sandbox, the destruction
// play session and replays still read. It moves to the Destruction Module's importer in M19.
//
// Import: each static box becomes a Box primitive (size = 2 x half extents), each destructible an object with a
// DestructiblePlacementComponent, each spawn point an object with a SpawnPointComponent (yaw = rotation about +Y).
// Export: every Box primitive becomes a static box (its world position and rotation; the world scale multiplies its
// size), plus every destructible and spawn point, in id order. Other objects are skipped with a warning.

#include <interi/editor/EditorScene.h>
#include <interi/gameplay/Level.h>

#include <string>
#include <vector>

namespace interi::editor
{

/// A destructible instance (level `destructibles`).
struct DestructiblePlacementComponent
{
    static constexpr const char* kComponentName = "interi.editor.Destructible";
    std::string asset; ///< Virtual path of a cooked `.idest`.
};

/// A spawn point (level `spawnPoints`). Its yaw is the world rotation about +Y.
struct SpawnPointComponent
{
    static constexpr const char* kComponentName = "interi.editor.SpawnPoint";
    std::string name;
};

void RegisterLevelReflection(ReflectionRegistry& registry);

/// Adds the level's entries to the scene as new objects: static boxes, then destructibles, then spawn points.
void ImportLevel(const gameplay::LevelDef& level, EditorScene& scene);

/// The scene as a level. `warnings` (optional) names each object that has no level equivalent.
[[nodiscard]] gameplay::LevelDef ExportLevel(const EditorScene& scene, std::vector<std::string>* warnings = nullptr);

EditorEntityId CreateDestructible(EditorScene& scene, const Transform& transform, std::string asset);
EditorEntityId CreateSpawnPoint(EditorScene& scene, const Transform& transform, std::string name);

} // namespace interi::editor
