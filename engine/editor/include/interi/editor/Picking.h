// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Geometry.h>
#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorScene.h>

#include <functional>
#include <optional>
#include <string_view>

namespace interi::editor
{

/// How the viewport shows a mesh object whose asset can't be read: a 1 m cube on its position.
inline constexpr Vec3 kPlaceholderHalfExtents{0.5f, 0.5f, 0.5f};

/// Bounds of a mesh asset by path, or nothing if it can't be read (modules that draw their own mesh assets leave
/// the object a placeholder cube).
using AssetBoundsFn = std::function<std::optional<AABB>(std::string_view asset)>;

/// An object's bounds in its own space: its primitive's or its mesh asset's (a placeholder cube when the asset can't
/// be read). Nothing for an object without a shape (a group).
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

/// The entity whose pick box the ray enters first.
[[nodiscard]] std::optional<PickHit> PickEntity(const EditorScene& scene, const Ray& ray, const AssetBoundsFn& bounds);

} // namespace interi::editor
