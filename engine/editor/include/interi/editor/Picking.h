// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Geometry.h>
#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorScene.h>

#include <functional>
#include <optional>
#include <string_view>

namespace interi::destruction
{
struct DestructibleAsset;
struct FractureMesh;
} // namespace interi::destruction

namespace interi::editor
{

/// How the viewport shows objects without a mesh: spawn points as a 0.6 x 1.8 x 0.3 m block standing on their
/// position, destructibles whose asset can't be read as a 1 m cube on theirs.
inline constexpr Vec3 kSpawnMarkerHalfExtents{0.3f, 0.9f, 0.15f};
inline constexpr Vec3 kPlaceholderHalfExtents{0.5f, 0.5f, 0.5f};

/// Asset-space bounds of a chunk's render mesh (invalid if it has none).
[[nodiscard]] AABB ChunkBounds(const destruction::DestructibleAsset& asset, std::uint32_t chunk);

/// Bounds of an asset by path (a destructible's chunk 0, or a mesh asset), or nothing if it can't be read.
using AssetBoundsFn = std::function<std::optional<AABB>(std::string_view asset)>;

/// An object's bounds in its own space: its primitive's, its mesh asset's or destructible's (a placeholder cube when
/// the asset can't be read), or a spawn point's marker. Nothing for an object without a shape (a group).
[[nodiscard]] std::optional<AABB> LocalBounds(const EditorScene& scene, EditorEntityId id, const AssetBoundsFn& bounds);

/// The pick shape of an object (its LocalBounds under its world transform): an oriented box (centre, rotation, half
/// extents) in the world.
struct PickBox
{
    Vec3 centre;
    Quat rotation;
    Vec3 halfExtents;
};
[[nodiscard]] std::optional<PickBox> PickBoxOf(const EditorScene& scene, EditorEntityId id,
                                               const AssetBoundsFn& bounds);

struct PickHit
{
    EditorEntityId id = 0;
    float distance = 0.0f;
};

/// Distance along the ray to the nearest triangle of a mesh it hits (either side), or nothing.
[[nodiscard]] std::optional<float> RayMesh(const Ray& ray, const destruction::FractureMesh& mesh) noexcept;

/// The entity whose pick box the ray enters first.
[[nodiscard]] std::optional<PickHit> PickEntity(const EditorScene& scene, const Ray& ray, const AssetBoundsFn& bounds);

} // namespace interi::editor
