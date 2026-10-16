// SPDX-License-Identifier: MIT
#include <interi/gameplay/Character.h>
#include <interi/physics/JoltMath.h>

#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/ShapeFilter.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace interi::gameplay
{

namespace
{

constexpr std::size_t kTempAllocatorBytes = std::size_t{1} * 1024 * 1024;

float Radians(float degrees) noexcept
{
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

/// The capsule standing on the origin (character position = feet).
JPH::RefConst<JPH::Shape> MakeShape(const CharacterSettings& settings)
{
    const float halfCylinder = std::max(settings.height * 0.5f - settings.radius, 0.01f);
    const float centre = halfCylinder + settings.radius;
    return JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, centre, 0.0f), JPH::Quat::sIdentity(),
                                               new JPH::CapsuleShape(halfCylinder, settings.radius))
        .Create()
        .Get();
}

} // namespace

CharacterController::CharacterController(physics::PhysicsWorld& physics, const CharacterSettings& settings,
                                         const Vec3& footPosition)
    : m_physics(&physics)
    , m_settings(settings)
    , m_tempAllocator(kTempAllocatorBytes)
{
    JPH::CharacterVirtualSettings jolt;
    jolt.mShape = MakeShape(settings);
    jolt.mMass = settings.mass;
    jolt.mMaxStrength = settings.maxStrength;
    jolt.mMaxSlopeAngle = Radians(settings.maxSlopeDegrees);
    jolt.mUp = JPH::Vec3::sAxisY();
    // Contacts below the capsule's lower sphere centre count as ground.
    jolt.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -settings.radius);
    m_character =
        new JPH::CharacterVirtual(&jolt, physics::ToJolt(footPosition), JPH::Quat::sIdentity(), 0, &physics.System());
    m_character->RefreshContacts(
        physics.System().GetDefaultBroadPhaseLayerFilter(static_cast<JPH::ObjectLayer>(settings.layer)),
        physics.System().GetDefaultLayerFilter(static_cast<JPH::ObjectLayer>(settings.layer)), JPH::BodyFilter{},
        JPH::ShapeFilter{}, m_tempAllocator);
}

CharacterController::~CharacterController() = default;

void CharacterController::Update(float deltaSeconds, const CharacterInput& input)
{
    JPH::PhysicsSystem& system = m_physics->System();
    const JPH::Vec3 gravity = system.GetGravity();
    const JPH::Vec3 up = m_character->GetUp();

    Vec3 move(input.move.x, 0.0f, input.move.z);
    const float length = std::sqrt(move.x * move.x + move.z * move.z);
    if (length > 1.0f)
    {
        move = move * (1.0f / length);
    }
    const JPH::Vec3 desired = physics::ToJolt(move) * m_settings.walkSpeed;

    m_character->UpdateGroundVelocity();
    const JPH::Vec3 groundVelocity = m_character->GetGroundVelocity();
    const JPH::Vec3 current = m_character->GetLinearVelocity();
    const bool grounded = m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
    const bool movingAwayFromGround = (current - groundVelocity).Dot(up) > 0.1f;

    JPH::Vec3 velocity;
    if (grounded && !movingAwayFromGround)
    {
        // Walking: take the ground's velocity and the input's horizontal velocity directly.
        velocity = groundVelocity + desired;
        if (input.jump)
        {
            velocity += up * m_settings.jumpSpeed;
        }
    }
    else
    {
        // Airborne: keep momentum, steer towards the input with reduced authority.
        const JPH::Vec3 vertical = up * current.Dot(up);
        const JPH::Vec3 horizontal = current - vertical;
        const float blend = std::clamp(m_settings.airControl * deltaSeconds * 10.0f, 0.0f, 1.0f);
        velocity = vertical + horizontal + (desired - horizontal) * (length > 0.0f ? blend : 0.0f);
    }
    velocity += gravity * deltaSeconds;
    m_character->SetLinearVelocity(velocity);

    JPH::CharacterVirtual::ExtendedUpdateSettings update;
    update.mStickToFloorStepDown = -up * m_settings.stickToFloor;
    update.mWalkStairsStepUp = up * m_settings.stepHeight;
    const auto layer = static_cast<JPH::ObjectLayer>(m_settings.layer);
    m_character->ExtendedUpdate(deltaSeconds, gravity, update, system.GetDefaultBroadPhaseLayerFilter(layer),
                                system.GetDefaultLayerFilter(layer), JPH::BodyFilter{}, JPH::ShapeFilter{},
                                m_tempAllocator);
}

void CharacterController::Teleport(const Vec3& footPosition)
{
    m_character->SetPosition(physics::ToJolt(footPosition));
    m_character->SetLinearVelocity(JPH::Vec3::sZero());
    const auto layer = static_cast<JPH::ObjectLayer>(m_settings.layer);
    m_character->RefreshContacts(m_physics->System().GetDefaultBroadPhaseLayerFilter(layer),
                                 m_physics->System().GetDefaultLayerFilter(layer), JPH::BodyFilter{},
                                 JPH::ShapeFilter{}, m_tempAllocator);
}

Vec3 CharacterController::FootPosition() const
{
    return physics::FromJolt(m_character->GetPosition());
}

Vec3 CharacterController::EyePosition() const
{
    return FootPosition() + Vec3(0.0f, m_settings.height * 0.92f, 0.0f);
}

Vec3 CharacterController::Velocity() const
{
    return physics::FromJolt(m_character->GetLinearVelocity());
}

bool CharacterController::OnGround() const
{
    return m_character->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround;
}

} // namespace interi::gameplay
