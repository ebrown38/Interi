// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/physics/PhysicsLayers.h>
#include <interi/physics/PhysicsWorld.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
// clang-format on

#include <cstdint>
#include <optional>
#include <vector>

// Scene queries for weapons and explosions. They lock the bodies they touch, so they may run on any thread and in
// parallel with each other, but not during PhysicsWorld::Step.
namespace interi::physics
{

/// Bit set of ObjectLayers a query considers.
using LayerMask = std::uint32_t;

[[nodiscard]] constexpr LayerMask LayerBit(ObjectLayer layer) noexcept
{
    return LayerMask{1} << static_cast<std::uint32_t>(layer);
}

inline constexpr LayerMask kAllLayers = (LayerMask{1} << kObjectLayerCount) - 1;

struct QueryHit
{
    JPH::BodyID body;
    Entity entity; ///< Invalid for bodies not created by PhysicsSync.
    std::uint32_t subShape = 0;
    Vec3 point;         ///< World space, on the hit body's surface.
    Vec3 normal;        ///< Unit surface normal of the hit body at `point`.
    float fraction = 0; ///< Of the ray / sweep: hit position = start + fraction * displacement.
};

/// Nearest body hit by the segment from `origin` to `origin + displacement`.
[[nodiscard]] std::optional<QueryHit> Raycast(const PhysicsWorld& world, const Vec3& origin, const Vec3& displacement,
                                              LayerMask layers = kAllLayers);

/// Nearest body hit by a sphere swept from `start` to `start + displacement`. Bodies the sphere already overlaps at
/// `start` are hit at fraction 0.
[[nodiscard]] std::optional<QueryHit> SphereCast(const PhysicsWorld& world, const Vec3& start, float radius,
                                                 const Vec3& displacement, LayerMask layers = kAllLayers);

/// Every body whose shape intersects the sphere, once each, sorted by body ID. Replaces the contents of `out`.
/// QueryHit::point is the deepest point on the body, `normal` points out of it and `fraction` is 0.
void OverlapSphere(const PhysicsWorld& world, const Vec3& center, float radius, std::vector<QueryHit>& out,
                   LayerMask layers = kAllLayers);

} // namespace interi::physics
