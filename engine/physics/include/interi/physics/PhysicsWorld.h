// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/jobs_jolt/JoltJobSystem.h>
#include <interi/physics/ContactEvents.h>
#include <interi/physics/PhysicsLayers.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/PhysicsSystem.h>
// clang-format on

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace interi::physics
{

struct PhysicsWorldDesc
{
    std::uint32_t maxBodies = 65536;
    std::uint32_t maxBodyPairs = 65536;
    std::uint32_t maxContactConstraints = 32768;
    std::uint32_t bodyMutexes = 0; ///< 0 = Jolt's default.
    std::size_t tempAllocatorBytes = std::size_t{32} * 1024 * 1024;
    Vec3 gravity{0.0f, -9.81f, 0.0f};
    int collisionSteps = 1; ///< Jolt collision substeps per Step.
    /// New contacts with a lower estimated impulse (N·s) produce no ContactEvent.
    float minContactImpulse = 0.0f;
};

/// Owns a JPH::PhysicsSystem with the engine's layers, stepping it on the engine's JobSystem through JoltJobSystem.
/// Requires a live JoltRuntime. Body creation through Bodies() is thread-safe; Step must not overlap other access.
///
/// Determinism: the same bodies, added in the same order, stepped with the same time steps give bit-identical state
/// on any thread count (and, with the overlay Jolt build, on any supported compiler).
class PhysicsWorld
{
public:
    explicit PhysicsWorld(interi::JobSystem& jobs, const PhysicsWorldDesc& desc = {});
    ~PhysicsWorld();

    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsWorld(PhysicsWorld&&) = delete;
    PhysicsWorld& operator=(PhysicsWorld&&) = delete;

    /// Advances the simulation by `deltaSeconds`. Must be called from a thread that may wait on the JobSystem (and,
    /// for the contact buffers, from no other non-worker thread at the same time).
    void Step(float deltaSeconds);

    /// The new contacts of the last Step, sorted (identical at any thread count). Valid until the next Step.
    [[nodiscard]] std::span<const ContactEvent> Contacts() const noexcept { return m_contacts; }

    /// Rebuilds the broadphase trees; call after adding many bodies at once, before the next Step.
    void OptimizeBroadPhase();

    [[nodiscard]] JPH::PhysicsSystem& System() noexcept { return m_system; }
    [[nodiscard]] const JPH::PhysicsSystem& System() const noexcept { return m_system; }
    /// Locking body interface: safe from any thread.
    [[nodiscard]] JPH::BodyInterface& Bodies() noexcept { return m_system.GetBodyInterface(); }
    /// Non-locking body interface: only when nothing else touches bodies (e.g. between steps on the sim thread).
    [[nodiscard]] JPH::BodyInterface& BodiesNoLock() noexcept { return m_system.GetBodyInterfaceNoLock(); }

    /// The entity a body belongs to (PhysicsSync stores it in the body's user data). Invalid for bodies created
    /// outside PhysicsSync.
    [[nodiscard]] Entity EntityOf(const JPH::BodyID& body) const;

    [[nodiscard]] std::uint64_t StepCount() const noexcept { return m_stepCount; }

private:
    LayerFilters m_layers;
    JPH::PhysicsSystem m_system;
    JPH::TempAllocatorImpl m_tempAllocator;
    JoltJobSystem m_jolt;
    ContactCollector m_contactCollector;
    std::vector<ContactEvent> m_contacts;
    int m_collisionSteps;
    std::uint64_t m_stepCount = 0;
};

} // namespace interi::physics
