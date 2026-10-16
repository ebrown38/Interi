// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/core/Profiling.h>
#include <interi/core/math/Quat.h>
#include <interi/core/math/Transform.h>
#include <interi/destruction_runtime/DebrisSystem.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/Shape.h>
// clang-format on

#include <utility>
#include <vector>

namespace interi::destruction
{

namespace
{

Entity Unpack(std::uint64_t packed) noexcept
{
    return Entity::FromParts(static_cast<std::uint32_t>(packed), static_cast<std::uint32_t>(packed >> 32));
}

Vec3 WorldCenter(const Transform& pose, const DebrisComponent& debris) noexcept
{
    return pose.TransformPoint(debris.localCenter);
}

} // namespace

DebrisSystem::DebrisSystem(physics::PhysicsWorld& physics, const DebrisSettings& settings, IAllocator& allocator)
    : m_physics(&physics)
    , m_settings(settings)
    , m_bodies(settings.maxBodies, allocator)
    , m_particles(settings.maxParticles, allocator)
{
    m_bodyQuery.With<DebrisComponent, Transform, physics::RigidBodyComponent>();
    m_particleQuery.With<DebrisComponent, DebrisParticleComponent, Transform>();
}

DebrisSystem::~DebrisSystem() = default;

DebrisPool& DebrisSystem::PoolOf(DebrisState state) noexcept
{
    return state == DebrisState::Body ? m_bodies : m_particles;
}

void DebrisSystem::Destroy(World& world, Entity entity)
{
    if (const auto* debris = world.Get<DebrisComponent>(entity))
    {
        (void)PoolOf(debris->state).Release(debris->slot);
    }
    world.DestroyEntity(entity);
}

bool DebrisSystem::Adopt(World& world, Entity entity, float volume)
{
    const auto* pose = world.Get<Transform>(entity);
    const auto* collider = world.Get<physics::ColliderComponent>(entity);
    INTERI_ASSERT(pose != nullptr && collider != nullptr && collider->shape != nullptr &&
                  world.Has<physics::RigidBodyComponent>(entity));
    INTERI_ASSERT(!world.Has<DebrisComponent>(entity));

    // Jolt bounds are relative to the centre of mass.
    const JPH::AABox bounds = collider->shape->GetLocalBounds();
    DebrisComponent debris;
    debris.localCenter = physics::FromJolt(collider->shape->GetCenterOfMass() + bounds.GetCenter());
    debris.radius = Length(physics::FromJolt(bounds.GetExtent()));
    debris.volume = volume;
    debris.distance = Distance(WorldCenter(*pose, debris), m_viewPosition);

    const float priority = DebrisPriority(debris.radius, 0.0f, debris.distance, m_settings.policy);
    const DebrisPool::Acquired acquired = m_bodies.Acquire(entity.Packed(), priority);
    if (acquired.evicted)
    {
        world.DestroyEntity(Unpack(*acquired.evicted)); // its slot now belongs to `entity`
        ++m_stats.evicted;
    }
    if (!acquired.slot.IsValid())
    {
        world.DestroyEntity(entity);
        ++m_stats.rejected;
        return false;
    }
    debris.slot = acquired.slot;
    world.Add(entity, debris);
    ++m_stats.adopted;
    return true;
}

void DebrisSystem::ReleaseDead(World& world, DebrisPool& pool, DebrisState state)
{
    // Debris destroyed (or stripped) by someone else: free its slot.
    pool.ForEachLive(
        [&](DebrisSlot slot)
        {
            const auto* debris = world.Get<DebrisComponent>(Unpack(pool.User(slot)));
            if (debris == nullptr || debris->state != state || debris->slot != slot)
            {
                (void)pool.Release(slot);
            }
        });
}

void DebrisSystem::Update(World& world, JobSystem& jobs, const DebrisView& view, float deltaSeconds)
{
    INTERI_ZONE_NAMED("DebrisSystem::Update");
    m_viewPosition = view.position;
    ReleaseDead(world, m_bodies, DebrisState::Body);
    ReleaseDead(world, m_particles, DebrisState::Particle);

    const DebrisSettings& settings = m_settings;
    const JPH::BodyInterface& bodies = m_physics->BodiesNoLock();

    // Per-debris work in parallel; each row writes only itself and its own pool slot.
    m_bodyQuery.ParallelForEachChunk(
        world, jobs, RunTicks{.lastRun = 0, .thisRun = world.BeginRun()},
        [&](ChunkView& chunk, std::size_t)
        {
            const auto poses = chunk.Read<Transform>();
            const auto debris = chunk.Write<DebrisComponent>();
            const bool hasBodies = chunk.Has<physics::PhysicsBodyComponent>();
            const auto handles = hasBodies ? chunk.Read<physics::PhysicsBodyComponent>()
                                           : std::span<const physics::PhysicsBodyComponent>();
            for (std::uint32_t row = 0; row < chunk.Count(); ++row)
            {
                DebrisComponent& d = debris[row];
                const Vec3 center = WorldCenter(poses[row], d);
                d.age += deltaSeconds;
                d.distance = Distance(center, view.position);
                d.visible = SphereVisible(center, d.radius, view.cullPlanes);
                d.opacity = BodyOpacity(d.volume, d.age, settings.policy);
                m_bodies.SetPriority(d.slot, DebrisPriority(d.radius, d.age, d.distance, settings.policy));
                const bool asleep = hasBodies && !bodies.IsActive(handles[row].id);
                d.action = ChooseDebrisAction(
                    {.volume = d.volume, .age = d.age, .distance = d.distance, .asleep = asleep}, settings.policy);
                if (d.action == DebrisAction::Settle && !settings.mergeRubble)
                {
                    d.action = DebrisAction::Keep;
                }
            }
        });
    m_particleQuery.ParallelForEachChunk(
        world, jobs, RunTicks{.lastRun = 0, .thisRun = world.BeginRun()},
        [&](ChunkView& chunk, std::size_t)
        {
            const auto poses = chunk.Write<Transform>();
            const auto motions = chunk.Write<DebrisParticleComponent>();
            const auto debris = chunk.Write<DebrisComponent>();
            for (std::uint32_t row = 0; row < chunk.Count(); ++row)
            {
                DebrisComponent& d = debris[row];
                IntegrateParticle(poses[row], motions[row].linearVelocity, motions[row].angularVelocity,
                                  settings.gravity, deltaSeconds);
                const Vec3 center = WorldCenter(poses[row], d);
                d.age += deltaSeconds;
                d.distance = Distance(center, view.position);
                d.visible = SphereVisible(center, d.radius, view.cullPlanes);
                d.opacity = ParticleOpacity(d.age, settings.policy);
                m_particles.SetPriority(d.slot, DebrisPriority(d.radius, d.age, d.distance, settings.policy));
                d.action = d.opacity > 0.0f ? DebrisAction::Keep : DebrisAction::Remove;
            }
        });

    // Structural changes on this thread, in query order.
    std::vector<std::pair<Entity, DebrisAction>> actions;
    const auto collect = [&](ChunkView& chunk)
    {
        const auto entities = chunk.Entities();
        const auto debris = chunk.Read<DebrisComponent>();
        for (std::uint32_t row = 0; row < chunk.Count(); ++row)
        {
            if (debris[row].action != DebrisAction::Keep)
            {
                actions.emplace_back(entities[row], debris[row].action);
            }
        }
    };
    const RunTicks readTicks{.lastRun = 0, .thisRun = world.CurrentTick()};
    m_bodyQuery.ForEachChunk(world, readTicks, collect);
    m_particleQuery.ForEachChunk(world, readTicks, collect);

    std::vector<Entity> settled;
    for (const auto& [entity, action] : actions)
    {
        switch (action)
        {
        case DebrisAction::Remove:
            Destroy(world, entity);
            ++m_stats.removed;
            break;
        case DebrisAction::ToParticle:
            ToParticle(world, entity);
            break;
        case DebrisAction::Settle:
            settled.push_back(entity);
            break;
        case DebrisAction::Keep:
            break;
        }
    }
    MergeRubble(world, settled);

    m_lastStats = m_stats;
    m_stats = {};
}

void DebrisSystem::ToParticle(World& world, Entity entity)
{
    const auto* rigidBody = world.Get<physics::RigidBodyComponent>(entity);
    DebrisParticleComponent motion{.linearVelocity = rigidBody->linearVelocity,
                                   .angularVelocity = rigidBody->angularVelocity};
    if (const auto* handle = world.Get<physics::PhysicsBodyComponent>(entity))
    {
        const JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
        motion.linearVelocity = physics::FromJolt(bodies.GetLinearVelocity(handle->id));
        motion.angularVelocity = physics::FromJolt(bodies.GetAngularVelocity(handle->id));
    }
    DebrisComponent debris = *world.Get<DebrisComponent>(entity);
    (void)m_bodies.Release(debris.slot);

    const DebrisPool::Acquired acquired =
        m_particles.Acquire(entity.Packed(), DebrisPriority(debris.radius, 0.0f, debris.distance, m_settings.policy));
    if (acquired.evicted)
    {
        world.DestroyEntity(Unpack(*acquired.evicted));
        ++m_stats.evicted;
    }
    if (!acquired.slot.IsValid())
    {
        world.DestroyEntity(entity);
        ++m_stats.evicted;
        return;
    }
    // PhysicsSync removes the body once the RigidBodyComponent is gone.
    (void)world.Remove<physics::RigidBodyComponent>(entity);
    world.Add(entity, motion);
    debris.slot = acquired.slot;
    debris.state = DebrisState::Particle;
    debris.age = 0.0f;
    debris.opacity = 1.0f;
    debris.action = DebrisAction::Keep;
    *world.GetMut<DebrisComponent>(entity) = debris;
    ++m_stats.toParticles;
}

void DebrisSystem::MergeRubble(World& world, std::span<const Entity> settled)
{
    if (settled.empty())
    {
        return;
    }
    std::vector<Vec3> centers;
    centers.reserve(settled.size());
    for (const Entity entity : settled)
    {
        centers.push_back(WorldCenter(*world.Get<Transform>(entity), *world.Get<DebrisComponent>(entity)));
    }
    std::vector<physics::ColliderPart> parts;
    for (const std::vector<std::uint32_t>& cluster :
         ClusterByGrid(centers, m_settings.rubbleCellSize, std::max(m_settings.rubbleMinPieces, 1u)))
    {
        Transform origin;
        origin.position = world.Get<Transform>(settled[cluster.front()])->position;
        parts.clear();
        RubbleSourceComponent sources;
        for (const std::uint32_t member : cluster)
        {
            const Transform& pose = *world.Get<Transform>(settled[member]);
            parts.push_back({.position = pose.position - origin.position,
                             .rotation = pose.rotation,
                             .collider = *world.Get<physics::ColliderComponent>(settled[member])});
            if (const auto* source = world.Get<DebrisSourceComponent>(settled[member]); source != nullptr)
            {
                sources.parts.push_back({.asset = source->asset,
                                         .chunks = source->chunks,
                                         .position = pose.position - origin.position,
                                         .rotation = pose.rotation});
            }
        }
        Result<physics::ColliderComponent> compound = physics::ColliderComponent::Compound(parts);
        if (!compound)
        {
            INTERI_LOG_WARN("destruction", "rubble collider: {}", compound.error().message);
            continue;
        }
        physics::RigidBodyComponent body;
        body.motion = physics::MotionType::Static;
        body.layer = m_settings.rubbleLayer;
        const RubbleComponent rubble{.pieces = static_cast<std::uint32_t>(cluster.size())};
        if (sources.parts.empty())
        {
            world.CreateEntity(origin, body, std::move(*compound), rubble);
        }
        else
        {
            world.CreateEntity(origin, body, std::move(*compound), rubble, std::move(sources));
        }
        for (const std::uint32_t member : cluster)
        {
            Destroy(world, settled[member]);
        }
        ++m_stats.rubblePiles;
        m_stats.rubblePieces += static_cast<std::uint32_t>(cluster.size());
    }
}

} // namespace interi::destruction
