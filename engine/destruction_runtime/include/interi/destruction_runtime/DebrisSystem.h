// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Quat.h>
#include <interi/core/math/Vec.h>
#include <interi/core/memory/Allocator.h>
#include <interi/destruction/Debris.h>
#include <interi/destruction/DebrisPool.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/Query.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/PhysicsLayers.h>
#include <interi/physics/PhysicsWorld.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace interi::destruction
{

enum class DebrisState : std::uint8_t
{
    Body,     ///< A dynamic Jolt body (RigidBodyComponent), in the body pool.
    Particle, ///< No body: falls freely (DebrisParticleComponent), in the particle pool.
};

/// On every debris entity (with Transform and ColliderComponent). DebrisSystem fills it; read-only for others.
struct DebrisComponent
{
    DebrisSlot slot;       ///< In the pool of `state`.
    Vec3 localCenter;      ///< Centre of the collider's bounds, in the entity's space.
    float radius = 0.0f;   ///< Of the bounding sphere around localCenter (m).
    float volume = 0.0f;   ///< m^3.
    float age = 0.0f;      ///< Seconds since adopted (particles: since the conversion).
    float distance = 0.0f; ///< From the view at the last Update (m).
    float opacity = 1.0f;  ///< 1 = opaque; fading pieces reach 0 and are removed.
    DebrisState state = DebrisState::Body;
    DebrisAction action = DebrisAction::Keep; ///< Decided by the last Update.
    bool visible = true;                      ///< Bounding sphere inside the view's cull planes.
};

/// Motion of a debris particle (no Jolt body).
struct DebrisParticleComponent
{
    Vec3 linearVelocity;
    Vec3 angularVelocity;
};

/// On a static rubble pile: debris that came to rest, merged into one static compound body.
struct RubbleComponent
{
    std::uint32_t pieces = 0;
};

/// On a debris entity handed over by DestructionWorld: the leaf chunks it is made of, in asset space (the entity's
/// Transform maps them to the world). For rendering.
struct DebrisSourceComponent
{
    std::shared_ptr<const DestructibleAsset> asset;
    std::vector<std::uint32_t> chunks; ///< Ascending leaf chunk indices.
};

/// One member of a rubble pile: its chunks and its pose relative to the pile entity.
struct RubblePart
{
    std::shared_ptr<const DestructibleAsset> asset;
    std::vector<std::uint32_t> chunks;
    Vec3 position;
    Quat rotation;
};

/// On a rubble pile whose members had a DebrisSourceComponent: what to draw (the members are destroyed).
struct RubbleSourceComponent
{
    std::vector<RubblePart> parts;
};

static_assert(Component<DebrisComponent>);
static_assert(Component<DebrisSourceComponent>);
static_assert(Component<RubbleSourceComponent>);
static_assert(Component<DebrisParticleComponent>);
static_assert(Component<RubbleComponent>);

struct DebrisSettings
{
    /// Hard caps; past them the lowest-priority debris is evicted (destroyed). Proposed, see M8.4 baselines.
    std::uint32_t maxBodies = 2048;
    std::uint32_t maxParticles = 16384;
    DebrisPolicy policy;
    /// Sleeping debris bodies sharing a grid cell (edge rubbleCellSize, m) merge into one static rubble body once
    /// there are at least rubbleMinPieces of them.
    bool mergeRubble = true;
    float rubbleCellSize = 2.0f;
    std::uint32_t rubbleMinPieces = 4;
    physics::ObjectLayer rubbleLayer = physics::ObjectLayer::Static;
    Vec3 gravity{0.0f, -9.81f, 0.0f}; ///< For particles.
};

/// Where the debris is seen from.
struct DebrisView
{
    Vec3 position;
    /// Planes (n, w) that keep Dot(n, p) + w >= 0, n unit length; empty = no culling.
    std::span<const Vec4> cullPlanes;
};

/// Counters since the previous Update (adoptions between two Updates are counted in the second).
struct DebrisStats
{
    std::uint32_t adopted = 0;
    std::uint32_t rejected = 0; ///< Adoptions refused at the cap (lower priority than every live piece).
    std::uint32_t evicted = 0;  ///< Bodies and particles destroyed to make room.
    std::uint32_t toParticles = 0;
    std::uint32_t removed = 0; ///< Faded out (bodies and particles).
    std::uint32_t rubblePiles = 0;
    std::uint32_t rubblePieces = 0;
};

/// Keeps debris affordable: a hard cap with priority eviction, particles at a distance, rubble piles for settled
/// debris and fading for small pieces.
///
/// Debris entities are ECS entities with Transform, ColliderComponent and (while bodies) RigidBodyComponent, which
/// PhysicsSync turns into Jolt bodies. The system owns them once adopted: it destroys them when they are evicted,
/// fade out, or merge into rubble.
///
/// Adopt and Update are sync points like PhysicsSync: no systems may run on the world. The per-debris work in
/// Update (ages, distances, visibility, priorities, decisions, particle motion) runs in parallel over chunks;
/// structural changes are then applied on the calling thread in query order, so results are the same at any thread
/// count. Call Update once per fixed step after PhysicsSync::Step (and DestructionWorld::Update).
class DebrisSystem
{
public:
    DebrisSystem(physics::PhysicsWorld& physics, const DebrisSettings& settings = {},
                 IAllocator& allocator = GetDefaultAllocator());
    ~DebrisSystem();

    DebrisSystem(const DebrisSystem&) = delete;
    DebrisSystem& operator=(const DebrisSystem&) = delete;
    DebrisSystem(DebrisSystem&&) = delete;
    DebrisSystem& operator=(DebrisSystem&&) = delete;

    /// Takes over a dynamic body entity (Transform, ColliderComponent, RigidBodyComponent) of `volume` m^3. At the cap
    /// the lowest-priority body is evicted; if the newcomer ranks lowest it is destroyed instead and false returned.
    bool Adopt(World& world, Entity entity, float volume);

    void Update(World& world, JobSystem& jobs, const DebrisView& view, float deltaSeconds);

    [[nodiscard]] std::uint32_t BodyCount() const noexcept { return m_bodies.LiveCount(); }
    [[nodiscard]] std::uint32_t ParticleCount() const noexcept { return m_particles.LiveCount(); }
    [[nodiscard]] const DebrisSettings& Settings() const noexcept { return m_settings; }
    [[nodiscard]] const DebrisStats& LastStats() const noexcept { return m_lastStats; }

private:
    [[nodiscard]] DebrisPool& PoolOf(DebrisState state) noexcept;
    void Destroy(World& world, Entity entity);
    void ReleaseDead(World& world, DebrisPool& pool, DebrisState state);
    void ToParticle(World& world, Entity entity);
    void MergeRubble(World& world, std::span<const Entity> settled);

    physics::PhysicsWorld* m_physics;
    DebrisSettings m_settings;
    DebrisPool m_bodies;
    DebrisPool m_particles;
    Vec3 m_viewPosition;
    DebrisStats m_stats;
    DebrisStats m_lastStats;
    Query m_bodyQuery;
    Query m_particleQuery;
};

} // namespace interi::destruction
