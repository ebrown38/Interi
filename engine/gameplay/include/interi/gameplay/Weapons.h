// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>
#include <interi/gameplay/WeaponDef.h>
#include <interi/physics/PhysicsWorld.h>
#include <interi/physics/Queries.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace interi::gameplay
{

/// What weapons hit: everything but characters, projectiles and small debris (which nothing interacts with).
inline constexpr physics::LayerMask kWeaponLayers =
    physics::kAllLayers & ~physics::LayerBit(physics::ObjectLayer::Character) &
    ~physics::LayerBit(physics::ObjectLayer::Projectile) & ~physics::LayerBit(physics::ObjectLayer::DebrisSmall);

/// A melee hit.
struct MeleeHit
{
    Entity entity; ///< Invalid for bodies without an entity.
    Vec3 point;
    Vec3 normal;
};

/// A placed remote charge. It sticks to what it was placed on: a destructible's chunk (following whichever piece
/// holds the chunk, through splits), another entity's Transform, or the world.
struct RemoteCharge
{
    std::uint32_t id = 0;
    Vec3 position; ///< World space, updated every Update.
    Vec3 normal;   ///< World space at placement.
    const WeaponDef* weapon = nullptr;

    destruction::DestructibleId instance; ///< Attached to `chunk` of this instance (if valid)...
    std::uint32_t chunk = 0;
    Entity entity;   ///< ...or to this entity's Transform (if valid)...
    Vec3 localPoint; ///< ...at this point in asset / entity space.
};

struct Rocket
{
    Vec3 position;
    Vec3 velocity;
    float travelled = 0.0f;
    const WeaponDef* weapon = nullptr;
};

/// A blast of the last Update (charges and rockets), for effects.
struct Blast
{
    Vec3 center;
    Vec3 normal; ///< Surface normal where it hit (zero in the air).
    const WeaponDef* weapon = nullptr;
};

/// The sledgehammer, remote charges and rockets, on the physics and destruction worlds.
///
/// Like PhysicsSync and DestructionWorld this is a sync point on the simulation thread. Per fixed step: PhysicsSync
/// Step, then Update (moves charges with what they stick to, flies rockets, sets off detonations), then
/// DestructionWorld::Update (which applies the queued damage). Swing, PlaceCharge and FireRocket are called between
/// steps; Swing applies its impulse and queues its damage at once. Blasts damage every destructible through
/// QueueDamage and push dynamic bodies within `radius` away from the centre with `impulse` scaled by the falloff.
///
/// WeaponDefs are referenced, not copied: keep the WeaponDefSet alive while the system uses it.
class WeaponSystem
{
public:
    WeaponSystem(physics::PhysicsWorld& physics, destruction::DestructionWorld& destruction);

    /// Sledgehammer: sweeps a hitRadius sphere from `origin` along unit `direction` for `range`. On a hit, applies
    /// `impulse` along `direction` at the contact point (dynamic bodies) and queues melee damage there.
    std::optional<MeleeHit> Swing(const WeaponDef& weapon, const Vec3& origin, const Vec3& direction);

    /// Places a charge where the ray from `origin` along unit `direction` hits within `range`. Placing more than
    /// maxCharges of this weapon removes the oldest. Returns the charge id, or nothing if the ray hit nothing.
    std::optional<std::uint32_t> PlaceCharge(const World& world, const WeaponDef& weapon, const Vec3& origin,
                                             const Vec3& direction);

    /// Sets off every placed charge at the next Update.
    void DetonateCharges() noexcept { m_detonate = true; }

    /// Launches a rocket from `origin` along unit `direction`.
    void FireRocket(const WeaponDef& weapon, const Vec3& origin, const Vec3& direction);

    /// One fixed step (see the class comment).
    void Update(const World& world, float deltaSeconds);

    [[nodiscard]] std::span<const RemoteCharge> Charges() const noexcept { return m_charges; }
    [[nodiscard]] std::span<const Rocket> Rockets() const noexcept { return m_rockets; }
    /// Blasts set off by the last Update.
    [[nodiscard]] std::span<const Blast> Blasts() const noexcept { return m_blasts; }

    /// Removes every charge and rocket (level reload).
    void Clear() noexcept;

private:
    void Explode(const WeaponDef& weapon, const Vec3& center, const Vec3& normal);

    physics::PhysicsWorld* m_physics;
    destruction::DestructionWorld* m_destruction;
    std::vector<RemoteCharge> m_charges; ///< In placement order.
    std::vector<Rocket> m_rockets;
    std::vector<Blast> m_blasts;
    std::vector<physics::QueryHit> m_overlap;
    std::uint32_t m_nextChargeId = 1;
    bool m_detonate = false;
};

} // namespace interi::gameplay
