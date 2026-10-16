// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/destruction/Damage.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/gameplay/VehicleDef.h>
#include <interi/physics/PhysicsWorld.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
// clang-format on

#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{
class DebrisSystem;
}

namespace interi::gameplay
{

/// On a vehicle's entity (with an interi::Transform that Update keeps at the chassis pose). The Jolt body's user
/// data is this entity, so contacts with the vehicle name it. The entity has no RigidBodyComponent: the Vehicle owns
/// its body, not PhysicsSync.
struct VehicleComponent
{
    static constexpr const char* kComponentName = "interi.gameplay.Vehicle";
    float health = 0.0f;
};

static_assert(Component<VehicleComponent>);

/// On each piece of a destroyed vehicle (the chassis and the wheels): dynamic Debris-layer bodies.
struct WreckComponent
{
    static constexpr const char* kComponentName = "interi.gameplay.Wreck";
    bool wheel = false;
};

static_assert(Component<WreckComponent>);

/// One frame of driver intent.
struct VehicleInput
{
    float forward = 0.0f;   ///< -1 (reverse) .. 1 (full throttle).
    float right = 0.0f;     ///< -1 (left) .. 1 (right).
    float brake = 0.0f;     ///< 0 .. 1.
    float handBrake = 0.0f; ///< 0 .. 1.
};

/// A four-wheeled vehicle on Jolt's VehicleConstraint with a WheeledVehicleController (engine, gears, differentials,
/// suspension, tyre friction) and a cylinder-cast wheel collision tester.
///
/// Ramming a destructible damages it through the ordinary contact path: a contact's impulse estimate is the
/// effective mass times the approach speed, so damage scales with mass x relative velocity. Hard contacts and blasts
/// damage the vehicle; at zero health it is destroyed and turns into wreck debris: the chassis and each wheel become
/// dynamic Debris-layer entities (Transform, RigidBodyComponent, ColliderComponent, WreckComponent) moving with the
/// vehicle's velocity at those points.
///
/// Like CharacterController this is a sync point: SetInput between steps, Update after PhysicsSync::Step, on the
/// simulation thread. The VehicleDef is copied.
class Vehicle
{
public:
    Vehicle(physics::PhysicsWorld& physics, World& world, const VehicleDef& def, const Transform& pose);
    /// Removes the vehicle's body and constraint (if not destroyed) and its entity. Wreck entities stay.
    ~Vehicle();

    Vehicle(const Vehicle&) = delete;
    Vehicle& operator=(const Vehicle&) = delete;
    Vehicle(Vehicle&&) = delete;
    Vehicle& operator=(Vehicle&&) = delete;

    /// Driver input for the next steps (clamped). Wakes the body when any input is non-zero.
    void SetInput(const VehicleInput& input);

    /// After PhysicsSync::Step: takes damage from the step's contacts on the vehicle, turns a vehicle without health
    /// into wreck debris (adopted by the DebrisSystem, if one is set), and writes the entity's Transform.
    void Update();

    /// Loses `amount` health (applied as wreckage at the next Update).
    void ApplyDamage(float amount);
    /// Blast damage: the energy at the chassis centre (RadialFalloff) times explosionDamagePerJoule.
    void ApplyRadialDamage(const destruction::RadialDamage& damage);

    /// Wreck pieces are adopted by `debris` (nullptr: they stay plain entities).
    void SetDebrisSystem(destruction::DebrisSystem* debris) noexcept { m_debris = debris; }

    /// Moves the vehicle without simulation (spawning, resetting), at rest. No effect once destroyed.
    void Teleport(const Transform& pose);

    [[nodiscard]] const VehicleDef& Def() const noexcept { return m_def; }
    [[nodiscard]] Entity GetEntity() const noexcept { return m_entity; }
    /// Invalid once destroyed.
    [[nodiscard]] JPH::BodyID BodyId() const noexcept { return m_body; }
    [[nodiscard]] bool IsDestroyed() const noexcept { return m_destroyed; }
    [[nodiscard]] float Health() const noexcept { return m_health; }

    /// Chassis pose (body centre of the chassis box, not the centre of mass). Last pose once destroyed.
    [[nodiscard]] Transform Pose() const;
    [[nodiscard]] Vec3 LinearVelocity() const;
    /// Speed along the vehicle's forward (+Z) axis, m/s (negative when reversing).
    [[nodiscard]] float ForwardSpeed() const;
    [[nodiscard]] float EngineRpm() const;
    [[nodiscard]] int CurrentGear() const;
    [[nodiscard]] std::uint32_t WheelCount() const noexcept { return 4; }
    /// World pose of a wheel, its rotation axis along local X (for rendering).
    [[nodiscard]] Transform WheelPose(std::uint32_t wheel) const;
    [[nodiscard]] bool WheelInContact(std::uint32_t wheel) const;

    /// The wreck pieces (chassis first, then the wheels) once destroyed.
    [[nodiscard]] std::span<const Entity> WreckEntities() const noexcept { return m_wreck; }

private:
    void Destroy();
    void RemoveFromPhysics();

    physics::PhysicsWorld* m_physics;
    World* m_world;
    destruction::DebrisSystem* m_debris = nullptr;
    VehicleDef m_def;
    Entity m_entity;
    JPH::BodyID m_body;
    JPH::Ref<JPH::VehicleConstraint> m_constraint;
    JPH::Ref<JPH::VehicleCollisionTester> m_tester;
    Transform m_lastPose;
    float m_health = 0.0f;
    bool m_destroyed = false;
    std::vector<Entity> m_wreck;
};

} // namespace interi::gameplay
