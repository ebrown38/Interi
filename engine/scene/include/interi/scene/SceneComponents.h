// SPDX-License-Identifier: MIT
#pragma once

// The components every scene object has. An object is an entity with a chain::StableIdComponent (its id in the
// scene file, the id chains name it by), a NameComponent, a ParentComponent (0: a root), a LocalTransformComponent
// (authored, relative to the parent) and an interi::Transform (its world transform, derived by PropagateTransforms;
// what renderers and everything else read). Any other reflected component is the object's content.

#include <interi/chain/Executor.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/values/Reflection.h>
#include <interi/values/Value.h>

#include <cstdint>
#include <string>

namespace interi::scene
{

using chain::StableIdComponent;

/// Stable id of a scene object (0: none).
using ObjectId = std::uint64_t;

struct NameComponent
{
    static constexpr const char* kComponentName = "interi.Name";
    std::string name;
};

struct ParentComponent
{
    static constexpr const char* kComponentName = "interi.Parent";
    values::EntityRef parent; ///< 0: a root.
};

/// The authored transform, relative to the parent (the world transform for a root).
struct LocalTransformComponent
{
    static constexpr const char* kComponentName = "interi.LocalTransform";
    Vec3 position;
    Quat rotation;
    Vec3 scale{1.0f};

    [[nodiscard]] Transform ToTransform() const noexcept
    {
        return {.position = position, .rotation = rotation, .scale = scale};
    }
    [[nodiscard]] static LocalTransformComponent From(const Transform& transform) noexcept
    {
        return {.position = transform.position, .rotation = transform.rotation, .scale = transform.scale};
    }
};

/// Registers the components above (StableIdComponent and the world Transform are not reflected: the id is the
/// object's key and the world transform is derived).
void RegisterSceneReflection(values::ReflectionRegistry& registry);

/// Creates an object with the standard components (its world Transform equals `local` until PropagateTransforms).
Entity CreateObject(World& world, ObjectId id, std::string name, ObjectId parent = 0, const Transform& local = {});

} // namespace interi::scene
