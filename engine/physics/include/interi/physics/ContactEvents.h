// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/BodyID.h>
#include <Jolt/Physics/Collision/ContactListener.h>
// clang-format on

#include <cstdint>
#include <memory>
#include <vector>

namespace interi::physics
{

INTERI_MSVC_WARNING_PUSH_DISABLE(4324) // structure padded due to alignas

/// A new contact between two bodies (the first step they touch). Feeds destruction damage and impact audio.
/// Bodies are ordered: bodyA's ID is below bodyB's.
struct ContactEvent
{
    JPH::BodyID bodyA;
    JPH::BodyID bodyB;
    Entity entityA; ///< Invalid for bodies not created by PhysicsSync.
    Entity entityB;
    std::uint32_t subShapeA = 0; ///< JPH::SubShapeID values: which part of a compound was hit.
    std::uint32_t subShapeB = 0;
    Vec3 point;  ///< World space, the middle of the contact manifold.
    Vec3 normal; ///< Unit, pointing from A towards B.
    /// Estimated impulse of the impact (N·s): effective mass × approach speed × (1 + restitution). Jolt's contact
    /// callbacks run before the solver, so the solved impulse isn't available; this ignores rotation.
    float impulse = 0.0f;
};

/// Jolt contact listener that records ContactEvents into one buffer per thread (no locks on the hot path) and merges
/// them after the step into a list sorted by content, so the result doesn't depend on thread scheduling.
///
/// Callbacks may run on the JobSystem's workers and on the one thread that steps the physics world.
class ContactCollector final : public JPH::ContactListener
{
public:
    /// Contacts with a lower estimated impulse are not recorded (resting and grazing contacts).
    ContactCollector(const interi::JobSystem& jobs, float minImpulse);
    ~ContactCollector() override;

    ContactCollector(const ContactCollector&) = delete;
    ContactCollector& operator=(const ContactCollector&) = delete;
    ContactCollector(ContactCollector&&) = delete;
    ContactCollector& operator=(ContactCollector&&) = delete;

    void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold,
                        JPH::ContactSettings& settings) override;

    /// Appends to the calling thread's buffer. Thread-safe as long as at most one non-worker thread records.
    void Record(const ContactEvent& event);

    /// Moves every recorded event into `out` (replacing its contents), sorted. No Record may run concurrently.
    void Merge(std::vector<ContactEvent>& out);

private:
    struct alignas(kCacheLineSize) Buffer
    {
        std::vector<ContactEvent> events;
    };

    const interi::JobSystem* m_jobs;
    float m_minImpulse;
    std::unique_ptr<Buffer[]> m_buffers; ///< One per worker, plus the last one for the stepping thread.
};

INTERI_MSVC_WARNING_POP()

} // namespace interi::physics
