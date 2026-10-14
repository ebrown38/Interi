// SPDX-License-Identifier: MIT
#include <interi/core/Profiling.h>
#include <interi/physics/Components.h>
#include <interi/physics/ContactEvents.h>
#include <interi/physics/JoltMath.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
// clang-format on

#include <algorithm>
#include <cstddef>
#include <tuple>
#include <utility>

namespace interi::physics
{

namespace
{

float InverseMass(const JPH::Body& body)
{
    return body.IsDynamic() ? body.GetMotionProperties()->GetInverseMass() : 0.0f;
}

/// Total order over every field, so sorting gives the same sequence whatever order the events were recorded in.
auto SortKey(const ContactEvent& e)
{
    return std::tuple(e.bodyA.GetIndexAndSequenceNumber(), e.bodyB.GetIndexAndSequenceNumber(), e.subShapeA,
                      e.subShapeB, e.point.x, e.point.y, e.point.z, e.normal.x, e.normal.y, e.normal.z, e.impulse);
}

} // namespace

ContactCollector::ContactCollector(const interi::JobSystem& jobs, float minImpulse)
    : m_jobs(&jobs)
    , m_minImpulse(minImpulse)
    , m_buffers(std::make_unique<Buffer[]>(jobs.ThreadCount()))
{
}

ContactCollector::~ContactCollector() = default;

void ContactCollector::OnContactAdded(const JPH::Body& body1, const JPH::Body& body2,
                                      const JPH::ContactManifold& manifold, JPH::ContactSettings& settings)
{
    const float inverseMassSum = InverseMass(body1) + InverseMass(body2);
    const JPH::uint pointCount = manifold.mRelativeContactPointsOn1.size();
    if (inverseMassSum <= 0.0f || pointCount == 0)
    {
        return;
    }
    // Middle of the manifold, halfway between the two surfaces.
    JPH::Vec3 sum = JPH::Vec3::sZero();
    for (JPH::uint i = 0; i < pointCount; ++i)
    {
        sum += manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i];
    }
    const JPH::RVec3 point = manifold.mBaseOffset + sum / (2.0f * static_cast<float>(pointCount));

    // mWorldSpaceNormal points from body 1 to body 2; the bodies approach while their relative velocity opposes it.
    const JPH::Vec3 relativeVelocity = body2.GetPointVelocity(point) - body1.GetPointVelocity(point);
    const float approachSpeed = -relativeVelocity.Dot(manifold.mWorldSpaceNormal);
    const float impulse = std::max(approachSpeed, 0.0f) * (1.0f + settings.mCombinedRestitution) / inverseMassSum;
    if (impulse < m_minImpulse)
    {
        return;
    }

    ContactEvent event{.bodyA = body1.GetID(),
                       .bodyB = body2.GetID(),
                       .entityA = EntityFromBodyUserData(body1.GetUserData()),
                       .entityB = EntityFromBodyUserData(body2.GetUserData()),
                       .subShapeA = manifold.mSubShapeID1.GetValue(),
                       .subShapeB = manifold.mSubShapeID2.GetValue(),
                       .point = FromJolt(point),
                       .normal = FromJolt(manifold.mWorldSpaceNormal),
                       .impulse = impulse};
    if (event.bodyB < event.bodyA)
    {
        std::swap(event.bodyA, event.bodyB);
        std::swap(event.entityA, event.entityB);
        std::swap(event.subShapeA, event.subShapeB);
        event.normal = -event.normal;
    }
    Record(event);
}

void ContactCollector::Record(const ContactEvent& event)
{
    const std::size_t slot = m_jobs->CurrentWorkerIndex().value_or(m_jobs->WorkerCount());
    m_buffers[slot].events.push_back(event);
}

void ContactCollector::Merge(std::vector<ContactEvent>& out)
{
    INTERI_ZONE_NAMED("ContactCollector::Merge");
    out.clear();
    for (std::uint32_t slot = 0; slot < m_jobs->ThreadCount(); ++slot)
    {
        std::vector<ContactEvent>& events = m_buffers[slot].events;
        out.insert(out.end(), events.begin(), events.end());
        events.clear(); // keeps the capacity for the next step
    }
    std::ranges::sort(out, [](const ContactEvent& a, const ContactEvent& b) { return SortKey(a) < SortKey(b); });
}

} // namespace interi::physics
