// SPDX-License-Identifier: MIT
#pragma once

#include <interi/ecs/World.h>
#include <interi/scene/SceneComponents.h>

#include <map>
#include <vector>

namespace interi::scene
{

/// The object tree of a world: every entity with a StableIdComponent, ordered by id.
struct HierarchyIndex
{
    struct Node
    {
        Entity entity;
        ObjectId parent = 0;            ///< 0: a root (also for a broken parent link).
        std::vector<ObjectId> children; ///< Ascending.
    };

    std::map<ObjectId, Node> nodes;
    std::vector<ObjectId> roots; ///< Ascending.
    /// Objects whose parent doesn't exist or whose parent chain loops; they are treated as roots.
    std::vector<ObjectId> broken;

    /// Parents before children: roots in id order, each followed by its subtree (depth first, children by id).
    [[nodiscard]] std::vector<ObjectId> PreOrder() const;
    /// The object and every descendant, in PreOrder.
    [[nodiscard]] std::vector<ObjectId> Subtree(ObjectId id) const;
    /// True if `id` is `ancestor` or below it.
    [[nodiscard]] bool IsWithin(ObjectId id, ObjectId ancestor) const;
};

[[nodiscard]] HierarchyIndex BuildHierarchy(World& world);

/// The world transform of `id` composed from the local transforms up its parent chain (identity if unknown).
[[nodiscard]] Transform WorldTransformOf(World& world, const HierarchyIndex& index, ObjectId id);

/// Sets every object's world Transform from its local transform and its parent's world transform, parents first.
/// Deterministic (id order); objects in `broken` are placed as roots. Returns the index it used.
HierarchyIndex PropagateTransforms(World& world);

} // namespace interi::scene
