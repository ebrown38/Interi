// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Profiling.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>
#include <interi/physics/JoltRuntime.h>
#include <interi/physics/PhysicsWorld.h>

namespace interi::physics
{

PhysicsWorld::PhysicsWorld(interi::JobSystem& jobs, const PhysicsWorldDesc& desc)
    : m_tempAllocator(static_cast<JPH::uint>(desc.tempAllocatorBytes))
    , m_jolt(jobs)
    , m_contactCollector(jobs, desc.minContactImpulse)
    , m_collisionSteps(desc.collisionSteps)
{
    INTERI_ASSERT(JoltRuntime::IsInitialized(), "construct a JoltRuntime before any PhysicsWorld");
    m_system.Init(desc.maxBodies, desc.bodyMutexes, desc.maxBodyPairs, desc.maxContactConstraints,
                  m_layers.BroadPhaseLayers(), m_layers.ObjectVsBroadPhase(), m_layers.ObjectPairs());
    m_system.SetGravity(ToJolt(desc.gravity));
    m_system.SetContactListener(&m_contactCollector);
}

PhysicsWorld::~PhysicsWorld() = default;

void PhysicsWorld::Step(float deltaSeconds)
{
    INTERI_ZONE_NAMED("PhysicsWorld::Step");
    const JPH::EPhysicsUpdateError error = m_system.Update(deltaSeconds, m_collisionSteps, &m_tempAllocator, &m_jolt);
    INTERI_ASSERT(error == JPH::EPhysicsUpdateError::None, "Jolt step overflowed a PhysicsWorldDesc limit");
    (void)error;
    m_contactCollector.Merge(m_contacts);
    ++m_stepCount;
}

void PhysicsWorld::OptimizeBroadPhase()
{
    m_system.OptimizeBroadPhase();
}

Entity PhysicsWorld::EntityOf(const JPH::BodyID& body) const
{
    return EntityFromBodyUserData(m_system.GetBodyInterface().GetUserData(body));
}

} // namespace interi::physics
