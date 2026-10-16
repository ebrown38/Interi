// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/MathTypes.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/physics/PhysicsLayers.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
// clang-format on

#include <cstdint>
#include <span>

namespace interi::physics
{

enum class MotionType : std::uint8_t
{
    Static,    ///< Never moves; its Transform is read once at creation.
    Kinematic, ///< Moved by its Transform every step (MoveKinematic); pushes dynamic bodies, is never pushed.
    Dynamic,   ///< Simulated; the body's pose is written back to its Transform.
};

/// How an entity's body behaves. With ColliderComponent and interi::Transform, PhysicsSync creates a Jolt body for
/// it. Changing this after creation has no effect (destroy and recreate the body instead).
struct RigidBodyComponent
{
    MotionType motion = MotionType::Dynamic;
    ObjectLayer layer = ObjectLayer::Debris;
    Vec3 linearVelocity;  ///< m/s at body creation (e.g. debris thrown off by a blast); dynamic bodies only.
    Vec3 angularVelocity; ///< rad/s at body creation; dynamic bodies only.
    float mass = 0.0f;    ///< kg; 0 = from the shape's volume at density 1000 kg/m^3.
    float friction = 0.5f;
    float restitution = 0.0f;
    float linearDamping = 0.05f;
    float angularDamping = 0.05f;
    bool startAsleep = false; ///< Dynamic bodies only: add the body asleep (e.g. debris settled when cooked).
    bool continuous = false;  ///< Linear-cast collision for fast bodies (projectiles).
};

/// The body's collision shape, shared and immutable. Build it with one of the factories below. The body's
/// interi::Transform scale is ignored: bake scale into the shape.
struct ColliderComponent
{
    JPH::RefConst<JPH::Shape> shape;

    [[nodiscard]] static ColliderComponent Box(const Vec3& halfExtents);
    [[nodiscard]] static ColliderComponent Sphere(float radius);
    /// Cylinder along the local Y axis (wheels).
    [[nodiscard]] static ColliderComponent Cylinder(float halfHeight, float radius);
    /// Convex hull of the points (local space). Degenerate or too few points is an error.
    [[nodiscard]] static Result<ColliderComponent> ConvexHull(std::span<const Vec3> points);

    /// Static compound of the parts (local space). Empty is an error.
    [[nodiscard]] static Result<ColliderComponent> Compound(std::span<const struct ColliderPart> parts);
};

/// One child of a compound collider, placed in the compound's local space.
struct ColliderPart
{
    Vec3 position;
    Quat rotation;
    ColliderComponent collider;
};

/// The Jolt body of an entity, added by PhysicsSync once the body exists. Don't add it yourself.
struct PhysicsBodyComponent
{
    JPH::BodyID id;
    bool wasActive = false; ///< Active at the previous transform pull (so the pose it fell asleep at is pulled).
};

/// A body's user data holds its entity (PhysicsSync sets it; 0, an invalid entity, for other bodies).
[[nodiscard]] constexpr std::uint64_t BodyUserData(Entity entity) noexcept
{
    return entity.Packed();
}

[[nodiscard]] constexpr Entity EntityFromBodyUserData(std::uint64_t userData) noexcept
{
    return Entity::FromParts(static_cast<std::uint32_t>(userData), static_cast<std::uint32_t>(userData >> 32));
}

static_assert(Component<RigidBodyComponent>);
static_assert(Component<ColliderComponent>);
static_assert(Component<PhysicsBodyComponent>);

} // namespace interi::physics
