// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/core/math/Transform.h>
#include <interi/destruction/Damage.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/StressSolver.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/PhysicsLayers.h>
#include <interi/physics/PhysicsWorld.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace interi
{
class IFileSystem;
}

namespace interi::destruction
{

class DebrisSystem;

/// Reads and parses an `.idest` file through the engine's file system.
[[nodiscard]] Result<std::shared_ptr<const DestructibleAsset>> LoadDestructibleAsset(const IFileSystem& files,
                                                                                     std::string_view path);

/// Identifies a spawned destructible. Stale ids (despawned) are detected.
struct DestructibleId
{
    std::uint32_t index = 0;
    std::uint32_t generation = 0; ///< 0 = invalid.

    [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
    friend constexpr bool operator==(const DestructibleId&, const DestructibleId&) = default;
};

/// On every body entity of a destructible: which instance, and which of its pieces the body is. A piece is a set
/// of leaf chunks held together by unbroken bonds (the anchored part, or a falling section).
struct DestructibleComponent
{
    DestructibleId instance;
    std::uint32_t piece = 0;
};

static_assert(Component<DestructibleComponent>);

enum class DamageKind : std::uint8_t
{
    Explosion,
    Melee,
};

struct DestructionSettings
{
    DamageSettings damage;
    StressSettings stress;
    /// Bond-iterations of stress solving per Update, shared by all instances (most recently damaged first). Measured
    /// at ~50-60 M bond-iterations/s with 7 workers (benchmarks/baselines), so the default is about 3 ms.
    std::uint64_t stressWorkBudget = 150'000;
    /// Break rounds per Update (solve, break overloaded bonds, split islands): limits how far a collapse cascades
    /// in one frame; the rest continues next frame.
    std::uint32_t maxBreakRounds = 4;
    /// Speed (m/s) a new falling piece gets away from the bonds that broke to release it.
    float separationSpeed = 0.5f;
    /// Speed (m/s) explosions and melee hits give falling pieces within their radius, away from the centre, scaled by
    /// the damage falloff (so loose chunks are thrown clear rather than left in place).
    float explosionPushSpeed = 6.0f;
    float meleePushSpeed = 2.0f;
    /// Contacts between two pieces of the same instance cause no damage while either piece is younger than this
    /// many physics steps (they touch where they just broke apart).
    std::uint32_t spawnGraceSteps = 3;
    physics::ObjectLayer anchoredLayer = physics::ObjectLayer::Structure;
    physics::ObjectLayer pieceLayer = physics::ObjectLayer::Debris;
    physics::ObjectLayer smallPieceLayer = physics::ObjectLayer::DebrisSmall;
    float smallPieceVolume = 0.05f; ///< m^3; falling pieces up to this volume use smallPieceLayer.
};

/// A bond that broke during the last Update (by damage or by stress), for effects such as dust and sparks.
struct BreakEvent
{
    DestructibleId instance;
    std::uint32_t bond = 0;
    Vec3 position;              ///< World-space bond centroid when it broke.
    Vec3 normal;                ///< World space, from the bond's chunkA towards chunkB.
    std::uint32_t material = 0; ///< Index into the asset's materials: the side with the lower fractureEnergy.
    float area = 0.0f;          ///< m^2.
    float energy = 0.0f;        ///< J: the bond's full health (fracture energy x area).
};

/// Per-Update counters.
struct DestructionStats
{
    std::uint32_t bondsBrokenByDamage = 0;
    std::uint32_t bondsBrokenByStress = 0;
    std::uint32_t piecesSpawned = 0;
    std::uint32_t piecesToDebris = 0; ///< Of piecesSpawned: small pieces handed to the DebrisSystem.
    std::uint32_t breakRounds = 0;
    std::uint64_t stressWork = 0; ///< Bond-iterations.
    double injectedEnergy = 0.0;  ///< Kinetic energy (J) added to pieces by separation speeds and pushes.
};

/// Runtime destructibles: stress, damage and the splitting of structures into falling pieces.
///
/// Each instance's leaf chunks are grouped into pieces. Chunks connected to an anchor form one static compound body;
/// every other connected group is a dynamic compound body. Bodies are ECS entities (Transform, RigidBodyComponent,
/// ColliderComponent, DestructibleComponent) that PhysicsSync turns into Jolt bodies; every collider part is one
/// collision hull whose Jolt user data is its chunk index.
///
/// Call Update once per fixed step, after PhysicsSync::Step (it reads that step's contacts). Like PhysicsSync it is a
/// sync point: entities are created and destroyed on the calling thread in a deterministic order; stress solving and
/// island labelling run in parallel and are thread-count independent.
class DestructionWorld
{
public:
    DestructionWorld(physics::PhysicsWorld& physics, const DestructionSettings& settings = {});
    ~DestructionWorld();

    DestructionWorld(const DestructionWorld&) = delete;
    DestructionWorld& operator=(const DestructionWorld&) = delete;
    DestructionWorld(DestructionWorld&&) = delete;
    DestructionWorld& operator=(DestructionWorld&&) = delete;

    /// Places an asset in the world at `transform` (uniform scale 1). Its pieces' entities are created now; their
    /// bodies appear at the next PhysicsSync step.
    DestructibleId Spawn(World& world, JobSystem& jobs, std::shared_ptr<const DestructibleAsset> asset,
                         const Transform& transform);

    /// Hands small falling pieces (volume <= smallPieceVolume) to `debris` from now on (nullptr: keep them). A handed
    /// over piece leaves its instance: its chunks no longer take damage or form pieces, and its entity (without a
    /// DestructibleComponent, with a DebrisSourceComponent) belongs to the DebrisSystem, so Despawn does not destroy
    /// it.
    void SetDebrisSystem(DebrisSystem* debris) noexcept { m_debris = debris; }

    /// Destroys every entity of the instance. Returns false for a stale id.
    bool Despawn(World& world, DestructibleId id);

    /// Damage in world space, applied to every instance at the next Update.
    void QueueDamage(const RadialDamage& damage, DamageKind kind);

    /// One fixed step of destruction: queued and contact damage, stress solving within the budget, breaking
    /// overloaded bonds, and splitting off pieces that lost their support.
    void Update(World& world, JobSystem& jobs);

    [[nodiscard]] const DestructionStats& LastStats() const noexcept { return m_stats; }
    /// Bonds broken by the last Update, in instance order, then in the order they broke. Recording them does not
    /// affect the simulation.
    [[nodiscard]] std::span<const BreakEvent> BreakEvents() const noexcept { return m_breakEvents; }
    [[nodiscard]] std::size_t InstanceCount() const noexcept;
    [[nodiscard]] bool IsAlive(DestructibleId id) const noexcept;

    /// Every live instance, in index order.
    [[nodiscard]] std::vector<DestructibleId> Instances() const;
    /// Where the instance was spawned (the pose of its anchored piece).
    [[nodiscard]] const Transform& InstanceTransform(DestructibleId id) const;
    [[nodiscard]] const DestructibleAsset& Asset(DestructibleId id) const;
    [[nodiscard]] std::span<const float> BondHealth(DestructibleId id) const;
    /// Each bond's health when undamaged (InitialBondHealth).
    [[nodiscard]] std::span<const float> BondFullHealth(DestructibleId id) const;
    [[nodiscard]] bool IsBondBroken(DestructibleId id, std::uint32_t bond) const;
    [[nodiscard]] const StressSolver& Stress(DestructibleId id) const;

    /// Pieces of an instance, including removed ones (no entity, no chunks), in creation order.
    [[nodiscard]] std::uint32_t PieceCount(DestructibleId id) const;
    [[nodiscard]] Entity PieceEntity(DestructibleId id, std::uint32_t piece) const;
    [[nodiscard]] bool PieceAnchored(DestructibleId id, std::uint32_t piece) const;
    [[nodiscard]] std::span<const std::uint32_t> PieceChunks(DestructibleId id, std::uint32_t piece) const;

    /// The chunk a contact or query hit on a destructible's body (`subShape` as in ContactEvent / QueryHit), or
    /// nothing for other entities.
    [[nodiscard]] std::optional<std::uint32_t> ChunkAt(const World& world, Entity entity, std::uint32_t subShape) const;
    /// Where asset space currently is for `chunk`: the pose of the piece that holds it (the instance transform for
    /// the anchored piece). Nothing for a stale id or a chunk that left the instance (handed to the DebrisSystem).
    /// Things attached to a chunk (remote charges) follow it through piece splits with this.
    [[nodiscard]] std::optional<Transform> ChunkPose(const World& world, DestructibleId id, std::uint32_t chunk) const;

private:
    struct Instance;
    struct Piece;
    struct AssetShapes;

    [[nodiscard]] Instance& Get(DestructibleId id);
    [[nodiscard]] const Instance& Get(DestructibleId id) const;
    [[nodiscard]] std::shared_ptr<const AssetShapes> ShapesFor(const std::shared_ptr<const DestructibleAsset>& asset);

    void ApplyDamage(World& world, Instance& instance, const RadialDamage& damage, float scale);
    void ApplyContactDamage(World& world);
    void ApplyPush(World& world, Instance& instance, const RadialDamage& damage, float speed);
    void RebuildPieces(World& world, JobSystem& jobs, Instance& instance);
    void ReleaseToDebris(Instance& instance, std::uint32_t piece);
    [[nodiscard]] Transform PiecePose(const World& world, const Instance& instance, std::uint32_t piece) const;

    physics::PhysicsWorld* m_physics;
    DebrisSystem* m_debris = nullptr;
    DestructionSettings m_settings;
    std::vector<std::unique_ptr<Instance>> m_instances; ///< By DestructibleId::index; null = free slot.
    std::vector<std::uint32_t> m_generations;
    std::vector<std::pair<const DestructibleAsset*, std::weak_ptr<const AssetShapes>>> m_shapeCache;
    std::vector<std::pair<RadialDamage, DamageKind>> m_queuedDamage;
    DestructionStats m_stats;
    std::vector<BreakEvent> m_breakEvents;
};

} // namespace interi::destruction
