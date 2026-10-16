// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/destruction/Damage.h>
#include <interi/gameplay/Weapons.h>
#include <interi/physics/JoltMath.h>

#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <utility>

namespace interi::gameplay
{

namespace
{

/// Distance kept from the surface when a blast is set off on it (so its centre is in front of the face it hit).
constexpr float kSurfaceOffset = 0.05f;

destruction::RadialDamage DamageOf(const WeaponDef& weapon, const Vec3& center) noexcept
{
    return {.center = center, .energy = weapon.energy, .innerRadius = weapon.innerRadius, .radius = weapon.radius};
}

bool IsDynamic(JPH::BodyInterface& bodies, const JPH::BodyID& body)
{
    return !body.IsInvalid() && bodies.GetMotionType(body) == JPH::EMotionType::Dynamic;
}

} // namespace

WeaponSystem::WeaponSystem(physics::PhysicsWorld& physics, destruction::DestructionWorld& destruction)
    : m_physics(&physics)
    , m_destruction(&destruction)
{
}

std::optional<MeleeHit> WeaponSystem::Swing(const WeaponDef& weapon, const Vec3& origin, const Vec3& direction)
{
    const Vec3 displacement = direction * weapon.range;
    const std::optional<physics::QueryHit> hit =
        weapon.hitRadius > 0.0f ? physics::SphereCast(*m_physics, origin, weapon.hitRadius, displacement, kWeaponLayers)
                                : physics::Raycast(*m_physics, origin, displacement, kWeaponLayers);
    if (!hit)
    {
        return std::nullopt;
    }
    JPH::BodyInterface& bodies = m_physics->Bodies();
    if (weapon.impulse > 0.0f && IsDynamic(bodies, hit->body))
    {
        bodies.AddImpulse(hit->body, physics::ToJolt(direction * weapon.impulse), physics::ToJolt(hit->point));
    }
    if (weapon.energy > 0.0f)
    {
        m_destruction->QueueDamage(DamageOf(weapon, hit->point), destruction::DamageKind::Melee);
    }
    return MeleeHit{.entity = hit->entity, .point = hit->point, .normal = hit->normal};
}

std::optional<std::uint32_t> WeaponSystem::PlaceCharge(const World& world, const WeaponDef& weapon, const Vec3& origin,
                                                       const Vec3& direction)
{
    const std::optional<physics::QueryHit> hit =
        physics::Raycast(*m_physics, origin, direction * weapon.range, kWeaponLayers);
    if (!hit)
    {
        return std::nullopt;
    }
    RemoteCharge charge;
    charge.id = m_nextChargeId++;
    charge.position = hit->point;
    charge.normal = hit->normal;
    charge.weapon = &weapon;
    if (const std::optional<std::uint32_t> chunk = m_destruction->ChunkAt(world, hit->entity, hit->subShape))
    {
        const auto* component = world.Get<destruction::DestructibleComponent>(hit->entity);
        const std::optional<Transform> pose = m_destruction->ChunkPose(world, component->instance, *chunk);
        if (pose)
        {
            charge.instance = component->instance;
            charge.chunk = *chunk;
            charge.localPoint = pose->Inverse().TransformPoint(hit->point);
        }
    }
    else if (const auto* transform = world.IsAlive(hit->entity) ? world.Get<Transform>(hit->entity) : nullptr)
    {
        charge.entity = hit->entity;
        charge.localPoint = transform->Inverse().TransformPoint(hit->point);
    }
    const auto sameWeapon = [&weapon](const RemoteCharge& placed) { return placed.weapon == &weapon; };
    if (std::cmp_greater_equal(std::ranges::count_if(m_charges, sameWeapon), weapon.maxCharges))
    {
        m_charges.erase(std::ranges::find_if(m_charges, sameWeapon));
    }
    m_charges.push_back(charge);
    return charge.id;
}

void WeaponSystem::FireRocket(const WeaponDef& weapon, const Vec3& origin, const Vec3& direction)
{
    m_rockets.push_back(
        Rocket{.position = origin, .velocity = direction * weapon.speed, .travelled = 0.0f, .weapon = &weapon});
}

void WeaponSystem::Update(const World& world, float deltaSeconds)
{
    m_blasts.clear();

    // Charges follow what they stick to; if that is gone they stay where they last were.
    for (RemoteCharge& charge : m_charges)
    {
        if (charge.instance.IsValid())
        {
            if (const std::optional<Transform> pose = m_destruction->ChunkPose(world, charge.instance, charge.chunk))
            {
                charge.position = pose->TransformPoint(charge.localPoint);
            }
        }
        else if (charge.entity.IsValid() && world.IsAlive(charge.entity))
        {
            if (const auto* transform = world.Get<Transform>(charge.entity))
            {
                charge.position = transform->TransformPoint(charge.localPoint);
            }
        }
    }

    // Rockets fly straight; a segment ray per step finds what they hit.
    std::vector<Rocket> flying;
    for (Rocket& rocket : m_rockets)
    {
        const Vec3 step = rocket.velocity * deltaSeconds;
        const std::optional<physics::QueryHit> hit =
            rocket.weapon->hitRadius > 0.0f
                ? physics::SphereCast(*m_physics, rocket.position, rocket.weapon->hitRadius, step, kWeaponLayers)
                : physics::Raycast(*m_physics, rocket.position, step, kWeaponLayers);
        if (hit)
        {
            Explode(*rocket.weapon, hit->point + hit->normal * kSurfaceOffset, hit->normal);
            continue;
        }
        rocket.position += step;
        rocket.travelled += Length(step);
        if (rocket.travelled >= rocket.weapon->range)
        {
            Explode(*rocket.weapon, rocket.position, Vec3());
            continue;
        }
        flying.push_back(rocket);
    }
    m_rockets = std::move(flying);

    if (m_detonate)
    {
        m_detonate = false;
        for (const RemoteCharge& charge : m_charges)
        {
            Explode(*charge.weapon, charge.position + charge.normal * kSurfaceOffset, charge.normal);
        }
        m_charges.clear();
    }
}

void WeaponSystem::Clear() noexcept
{
    m_charges.clear();
    m_rockets.clear();
    m_blasts.clear();
    m_detonate = false;
}

void WeaponSystem::Explode(const WeaponDef& weapon, const Vec3& center, const Vec3& normal)
{
    m_blasts.push_back(Blast{.center = center, .normal = normal, .weapon = &weapon});
    if (weapon.energy > 0.0f)
    {
        m_destruction->QueueDamage(DamageOf(weapon, center), destruction::DamageKind::Explosion);
    }
    if (weapon.impulse <= 0.0f)
    {
        return;
    }
    // Bodies sorted by ID, so the order of impulses is deterministic.
    physics::OverlapSphere(*m_physics, center, weapon.radius, m_overlap, kWeaponLayers);
    JPH::BodyInterface& bodies = m_physics->Bodies();
    for (const physics::QueryHit& hit : m_overlap)
    {
        if (!IsDynamic(bodies, hit.body))
        {
            continue;
        }
        const Vec3 centerOfMass = physics::FromJolt(bodies.GetCenterOfMassPosition(hit.body));
        const Vec3 away = centerOfMass - center;
        const float distance = Length(away);
        const Vec3 direction = distance > 1e-4f ? away * (1.0f / distance) : Vec3(0.0f, 1.0f, 0.0f);
        const float falloff = destruction::RadialFalloff(distance, weapon.innerRadius, weapon.radius);
        bodies.AddImpulse(hit.body, physics::ToJolt(direction * (weapon.impulse * falloff)));
    }
}

} // namespace interi::gameplay
