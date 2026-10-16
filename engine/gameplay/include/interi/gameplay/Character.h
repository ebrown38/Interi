// SPDX-License-Identifier: MIT
#pragma once

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
// clang-format on

#include <interi/core/math/Vec.h>
#include <interi/physics/PhysicsLayers.h>
#include <interi/physics/PhysicsWorld.h>

namespace interi::gameplay
{

struct CharacterSettings
{
    float radius = 0.3f;        ///< m, of the capsule.
    float height = 1.8f;        ///< m, feet to top of the capsule.
    float mass = 80.0f;         ///< kg, used when pushing bodies and standing on them.
    float maxStrength = 600.0f; ///< N: the largest force the character pushes dynamic bodies with.
    float walkSpeed = 5.0f;     ///< m/s.
    float airControl = 0.3f;    ///< Fraction of walk acceleration available in the air.
    float jumpSpeed = 5.0f;     ///< m/s upward at take-off.
    float maxSlopeDegrees = 50.0f;
    float stepHeight = 0.4f;   ///< m: ledges up to this are walked up (rubble, kerbs).
    float stickToFloor = 0.5f; ///< m: snap down over this when walking off a ledge or down a slope.
    physics::ObjectLayer layer = physics::ObjectLayer::Character;
};

/// One frame of player intent.
struct CharacterInput
{
    Vec3 move;         ///< World-space horizontal direction (y ignored), length <= 1 (clamped).
    bool jump = false; ///< Jumps if on the ground.
};

/// A player-controlled capsule on Jolt's CharacterVirtual: walks, jumps, walks up steps up to stepHeight, sticks to
/// the floor, slides along walls, rides moving platforms and pushes dynamic bodies with up to maxStrength (light
/// debris moves, heavy pieces don't).
///
/// The character is not a body in the PhysicsSystem, so Update is a sync point: call it after PhysicsSync::Step,
/// on the simulation thread, not while the physics world steps.
class CharacterController
{
public:
    CharacterController(physics::PhysicsWorld& physics, const CharacterSettings& settings, const Vec3& footPosition);
    ~CharacterController();

    CharacterController(const CharacterController&) = delete;
    CharacterController& operator=(const CharacterController&) = delete;
    CharacterController(CharacterController&&) = delete;
    CharacterController& operator=(CharacterController&&) = delete;

    /// Advances the character by `deltaSeconds` with `input`.
    void Update(float deltaSeconds, const CharacterInput& input);

    /// Moves the character without sweeping (spawning, loading), with zero velocity.
    void Teleport(const Vec3& footPosition);

    [[nodiscard]] Vec3 FootPosition() const;
    /// Where the eyes are (for first-person aiming and the camera target).
    [[nodiscard]] Vec3 EyePosition() const;
    [[nodiscard]] Vec3 Velocity() const;
    [[nodiscard]] bool OnGround() const;
    [[nodiscard]] const CharacterSettings& Settings() const noexcept { return m_settings; }

private:
    physics::PhysicsWorld* m_physics;
    CharacterSettings m_settings;
    JPH::TempAllocatorImpl m_tempAllocator;
    JPH::Ref<JPH::CharacterVirtual> m_character;
};

} // namespace interi::gameplay
