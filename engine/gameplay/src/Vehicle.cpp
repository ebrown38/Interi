// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/gameplay/Vehicle.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Vehicle/WheeledVehicleController.h>
// clang-format on

#include <algorithm>
#include <cmath>
#include <numbers>

namespace interi::gameplay
{

namespace
{

constexpr std::uint32_t kFrontLeft = 0;
constexpr std::uint32_t kFrontRight = 1;
constexpr std::uint32_t kRearLeft = 2;
constexpr std::uint32_t kRearRight = 3;
constexpr std::uint32_t kWheels = 4;

JPH::WheeledVehicleController& Controller(JPH::VehicleConstraint& constraint)
{
    return *static_cast<JPH::WheeledVehicleController*>(constraint.GetController());
}

JPH::VehicleConstraintSettings MakeConstraintSettings(const VehicleDef& def)
{
    JPH::VehicleConstraintSettings settings;
    const Vec3& e = def.halfExtents;
    const float z = e.z - def.wheelRadius;
    const float steer = def.maxSteerDegrees * std::numbers::pi_v<float> / 180.0f;
    for (std::uint32_t wheel = 0; wheel < kWheels; ++wheel)
    {
        const bool front = wheel == kFrontLeft || wheel == kFrontRight;
        const bool left = wheel == kFrontLeft || wheel == kRearLeft;
        auto* w = new JPH::WheelSettingsWV; // owned by the settings' Ref array
        w->mPosition = JPH::Vec3(left ? e.x : -e.x, -e.y, front ? z : -z);
        w->mRadius = def.wheelRadius;
        w->mWidth = def.wheelWidth;
        w->mSuspensionMinLength = def.suspensionMinLength;
        w->mSuspensionMaxLength = def.suspensionMaxLength;
        w->mSuspensionSpring.mFrequency = def.suspensionFrequency;
        w->mSuspensionSpring.mDamping = def.suspensionDamping;
        w->mMaxSteerAngle = front ? steer : 0.0f;
        w->mMaxBrakeTorque = def.brakeTorque;
        w->mMaxHandBrakeTorque = front ? 0.0f : def.handBrakeTorque;
        settings.mWheels.push_back(w);
    }

    auto* controller = new JPH::WheeledVehicleControllerSettings;
    controller->mEngine.mMaxTorque = def.engineTorque;
    controller->mEngine.mMinRPM = def.engineMinRpm;
    controller->mEngine.mMaxRPM = def.engineMaxRpm;
    // Jolt's default curve still gives 80% torque at max RPM and only clamps the RPM after the solve, so nothing
    // would cap the speed. Fall to zero at max RPM instead (a rev limiter): top speed is max RPM through the gearing.
    controller->mEngine.mNormalizedTorque.Clear();
    controller->mEngine.mNormalizedTorque.AddPoint(0.0f, 0.8f);
    controller->mEngine.mNormalizedTorque.AddPoint(0.66f, 1.0f);
    controller->mEngine.mNormalizedTorque.AddPoint(0.9f, 0.9f);
    controller->mEngine.mNormalizedTorque.AddPoint(1.0f, 0.0f);
    // Jolt requires shiftUp < maxRPM; keep its default 1000/2000/4000/6000 proportions.
    const float span = def.engineMaxRpm - def.engineMinRpm;
    controller->mTransmission.mShiftUpRPM = def.engineMinRpm + 0.6f * span;
    controller->mTransmission.mShiftDownRPM = def.engineMinRpm + 0.2f * span;
    controller->mTransmission.mGearRatios.assign(def.gearRatios.begin(), def.gearRatios.end());
    controller->mTransmission.mReverseGearRatios = {def.reverseGearRatio};
    const auto addDifferential = [&](std::uint32_t leftWheel, std::uint32_t rightWheel, float torqueRatio)
    {
        JPH::VehicleDifferentialSettings& differential = controller->mDifferentials.emplace_back();
        differential.mLeftWheel = static_cast<int>(leftWheel);
        differential.mRightWheel = static_cast<int>(rightWheel);
        differential.mDifferentialRatio = def.differentialRatio;
        differential.mEngineTorqueRatio = torqueRatio;
    };
    switch (def.drive)
    {
    case DriveLayout::Front:
        addDifferential(kFrontLeft, kFrontRight, 1.0f);
        break;
    case DriveLayout::Rear:
        addDifferential(kRearLeft, kRearRight, 1.0f);
        break;
    case DriveLayout::All:
        addDifferential(kFrontLeft, kFrontRight, 0.5f);
        addDifferential(kRearLeft, kRearRight, 0.5f);
        break;
    }
    settings.mController = controller;
    return settings;
}

} // namespace

Vehicle::Vehicle(physics::PhysicsWorld& physics, World& world, const VehicleDef& def, const Transform& pose)
    : m_physics(&physics)
    , m_world(&world)
    , m_def(def)
    , m_lastPose(pose)
    , m_health(def.health)
{
    INTERI_ASSERT(Validate(def).has_value());
    Transform entityPose = pose;
    entityPose.scale = Vec3(1.0f);
    m_entity = world.CreateEntity(entityPose, VehicleComponent{.health = m_health});

    const JPH::RefConst<JPH::Shape> box = physics::ColliderComponent::Box(def.halfExtents).shape;
    const JPH::RefConst<JPH::Shape> chassis =
        JPH::OffsetCenterOfMassShapeSettings(physics::ToJolt(def.centerOfMassOffset), box).Create().Get();
    JPH::BodyCreationSettings body(chassis, physics::ToJolt(pose.position), physics::ToJolt(pose.rotation),
                                   JPH::EMotionType::Dynamic, physics::ToJolt(physics::ObjectLayer::Vehicle));
    body.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
    body.mMassPropertiesOverride.mMass = def.mass;
    body.mLinearDamping = 0.0f; // the drivetrain and tyres slow the vehicle, not a velocity damping
    body.mUserData = physics::BodyUserData(m_entity);
    m_body = physics.Bodies().CreateAndAddBody(body, JPH::EActivation::Activate);
    INTERI_VERIFY(!m_body.IsInvalid()); // out of bodies

    const JPH::VehicleConstraintSettings settings = MakeConstraintSettings(def);
    {
        const JPH::BodyLockWrite lock(physics.System().GetBodyLockInterface(), m_body);
        m_constraint = new JPH::VehicleConstraint(lock.GetBody(), settings);
    }
    m_tester = new JPH::VehicleCollisionTesterCastCylinder(physics::ToJolt(physics::ObjectLayer::Vehicle));
    m_constraint->SetVehicleCollisionTester(m_tester);
    physics.System().AddConstraint(m_constraint);
    physics.System().AddStepListener(m_constraint);
}

Vehicle::~Vehicle()
{
    if (!m_destroyed)
    {
        RemoveFromPhysics();
        if (m_world->IsAlive(m_entity))
        {
            m_world->DestroyEntity(m_entity);
        }
    }
}

void Vehicle::RemoveFromPhysics()
{
    JPH::PhysicsSystem& system = m_physics->System();
    system.RemoveStepListener(m_constraint);
    system.RemoveConstraint(m_constraint);
    m_constraint = nullptr;
    m_physics->Bodies().RemoveBody(m_body);
    m_physics->Bodies().DestroyBody(m_body);
    m_body = JPH::BodyID();
}

void Vehicle::SetInput(const VehicleInput& input)
{
    if (m_destroyed)
    {
        return;
    }
    float forward = std::clamp(input.forward, -1.0f, 1.0f);
    const float right = std::clamp(input.right, -1.0f, 1.0f);
    float brake = std::clamp(input.brake, 0.0f, 1.0f);
    const float handBrake = std::clamp(input.handBrake, 0.0f, 1.0f);
    // Throttle against the direction of travel brakes until the vehicle has (nearly) stopped, then drives the other
    // way: otherwise the transmission would shift into reverse while rolling forward.
    const float speed = ForwardSpeed();
    if (forward * speed < 0.0f && std::abs(speed) > 0.5f)
    {
        brake = std::max(brake, std::abs(forward));
        forward = 0.0f;
    }
    Controller(*m_constraint).SetDriverInput(forward, right, brake, handBrake);
    if (forward != 0.0f || right != 0.0f || brake != 0.0f || handBrake != 0.0f)
    {
        m_physics->Bodies().ActivateBody(m_body);
    }
}

void Vehicle::Update()
{
    if (m_destroyed)
    {
        return;
    }
    for (const physics::ContactEvent& contact : m_physics->Contacts())
    {
        if ((contact.bodyA == m_body || contact.bodyB == m_body) && contact.impulse > m_def.impactThreshold)
        {
            ApplyDamage((contact.impulse - m_def.impactThreshold) * m_def.impactDamagePerImpulse);
        }
    }
    if (m_health <= 0.0f)
    {
        Destroy();
        return;
    }
    m_lastPose = Pose();
    *m_world->GetMut<Transform>(m_entity) = m_lastPose;
    m_world->GetMut<VehicleComponent>(m_entity)->health = m_health;
}

void Vehicle::ApplyDamage(float amount)
{
    if (!m_destroyed && amount > 0.0f)
    {
        m_health = std::max(0.0f, m_health - amount);
    }
}

void Vehicle::ApplyRadialDamage(const destruction::RadialDamage& damage)
{
    if (m_destroyed)
    {
        return;
    }
    const float distance = Distance(Pose().position, damage.center);
    ApplyDamage(damage.energy * destruction::RadialFalloff(distance, damage.innerRadius, damage.radius) *
                m_def.explosionDamagePerJoule);
}

void Vehicle::Teleport(const Transform& pose)
{
    if (m_destroyed)
    {
        return;
    }
    JPH::BodyInterface& bodies = m_physics->Bodies();
    bodies.SetPositionAndRotation(m_body, physics::ToJolt(pose.position), physics::ToJolt(pose.rotation),
                                  JPH::EActivation::Activate);
    bodies.SetLinearAndAngularVelocity(m_body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
    m_lastPose = Pose();
    *m_world->GetMut<Transform>(m_entity) = m_lastPose;
}

void Vehicle::Destroy()
{
    JPH::BodyInterface& bodies = m_physics->Bodies();
    const Vec3 velocity = physics::FromJolt(bodies.GetLinearVelocity(m_body));
    const Vec3 angular = physics::FromJolt(bodies.GetAngularVelocity(m_body));
    const Vec3 centerOfMass = physics::FromJolt(bodies.GetCenterOfMassPosition(m_body));
    const auto pointVelocity = [&](const Vec3& point) { return velocity + Cross(angular, point - centerOfMass); };

    struct Piece
    {
        Transform pose;
        physics::ColliderComponent collider;
        float mass = 0.0f;
        bool wheel = false;
    };
    std::vector<Piece> pieces;
    Transform chassis = Pose();
    pieces.push_back({.pose = chassis,
                      .collider = physics::ColliderComponent::Box(m_def.halfExtents),
                      .mass = m_def.mass - static_cast<float>(kWheels) * m_def.wheelMass});
    // Jolt cylinders run along Y; WheelPose has the axle along X: turn Y onto the axle.
    const Quat yToX = Quat::FromAxisAngle(Vec3(0.0f, 0.0f, 1.0f), 0.5f * std::numbers::pi_v<float>);
    for (std::uint32_t wheel = 0; wheel < kWheels; ++wheel)
    {
        Transform pose = WheelPose(wheel);
        pose.rotation = Normalize(pose.rotation * yToX);
        pieces.push_back({.pose = pose,
                          .collider = physics::ColliderComponent::Cylinder(0.5f * m_def.wheelWidth, m_def.wheelRadius),
                          .mass = m_def.wheelMass,
                          .wheel = true});
    }

    m_lastPose = chassis;
    RemoveFromPhysics();
    m_world->DestroyEntity(m_entity);
    m_entity = Entity();
    m_destroyed = true;

    for (const Piece& piece : pieces)
    {
        physics::RigidBodyComponent rigid;
        rigid.motion = physics::MotionType::Dynamic;
        rigid.layer = physics::ObjectLayer::Debris;
        rigid.mass = piece.mass;
        rigid.linearVelocity = pointVelocity(piece.pose.position);
        rigid.angularVelocity = angular;
        const float volume = piece.collider.shape->GetVolume();
        const Entity entity =
            m_world->CreateEntity(piece.pose, rigid, piece.collider, WreckComponent{.wheel = piece.wheel});
        if (m_debris != nullptr && !m_debris->Adopt(*m_world, entity, volume))
        {
            continue; // ranked below every live debris body: destroyed at once
        }
        m_wreck.push_back(entity);
    }
}

Transform Vehicle::Pose() const
{
    if (m_destroyed)
    {
        return m_lastPose;
    }
    JPH::RVec3 position;
    JPH::Quat rotation;
    m_physics->Bodies().GetPositionAndRotation(m_body, position, rotation);
    Transform pose;
    pose.position = physics::FromJolt(position);
    pose.rotation = physics::FromJolt(rotation);
    return pose;
}

Vec3 Vehicle::LinearVelocity() const
{
    return m_destroyed ? Vec3() : physics::FromJolt(m_physics->Bodies().GetLinearVelocity(m_body));
}

float Vehicle::ForwardSpeed() const
{
    return Dot(LinearVelocity(), Pose().TransformVector(Vec3(0.0f, 0.0f, 1.0f)));
}

float Vehicle::EngineRpm() const
{
    return m_destroyed ? 0.0f : Controller(*m_constraint).GetEngine().GetCurrentRPM();
}

int Vehicle::CurrentGear() const
{
    return m_destroyed ? 0 : Controller(*m_constraint).GetTransmission().GetCurrentGear();
}

Transform Vehicle::WheelPose(std::uint32_t wheel) const
{
    INTERI_ASSERT(wheel < kWheels);
    if (m_destroyed)
    {
        return m_lastPose;
    }
    const JPH::RMat44 matrix = m_constraint->GetWheelWorldTransform(wheel, JPH::Vec3::sAxisX(), JPH::Vec3::sAxisY());
    Transform pose;
    pose.position = physics::FromJolt(matrix.GetTranslation());
    pose.rotation = Normalize(physics::FromJolt(matrix.GetQuaternion()));
    return pose;
}

bool Vehicle::WheelInContact(std::uint32_t wheel) const
{
    INTERI_ASSERT(wheel < kWheels);
    return !m_destroyed && m_constraint->GetWheel(wheel)->HasContact();
}

} // namespace interi::gameplay
