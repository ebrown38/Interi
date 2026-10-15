// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/core/Profiling.h>
#include <interi/destruction/Islands.h>
#include <interi/destruction/StressBudget.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>
#include <interi/platform/FileSystem.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/SubShapeID.h>
// clang-format on

#include <algorithm>
#include <limits>
#include <utility>

namespace interi::destruction
{

namespace
{

constexpr std::uint32_t kNoPiece = std::numeric_limits<std::uint32_t>::max();

} // namespace

Result<std::shared_ptr<const DestructibleAsset>> LoadDestructibleAsset(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    INTERI_TRY_ASSIGN(asset, ReadDestructibleAsset(bytes));
    return std::make_shared<const DestructibleAsset>(std::move(asset));
}

/// Jolt shapes of an asset's collision hulls, shared by all its instances. Each hull's user data is its chunk.
struct DestructionWorld::AssetShapes
{
    std::vector<std::vector<JPH::RefConst<JPH::Shape>>> hulls; ///< Per chunk (empty for non-leaves).
};

struct DestructionWorld::Piece
{
    Entity entity;
    bool anchored = false;
    std::vector<std::uint32_t> chunks; ///< Ascending; empty once removed.
    std::uint64_t spawnStep = 0;
    Vec3 linearVelocity; ///< At creation (used if it splits before its body exists).
    Vec3 angularVelocity;
};

struct DestructionWorld::Instance
{
    Instance(std::shared_ptr<const DestructibleAsset> sourceAsset, StressSettings stress)
        : asset(std::move(sourceAsset))
        , solver(MakeStressGraph(*asset), stress)
    {
    }

    DestructibleId id;
    std::shared_ptr<const DestructibleAsset> asset;
    std::shared_ptr<const AssetShapes> shapes;
    Transform transform;
    StressSolver solver;
    std::vector<float> health;
    std::vector<float> maxHealth;
    std::vector<std::uint8_t> broken; ///< Per bond.
    std::vector<GraphEdge> edges;
    std::vector<std::uint8_t> anchored; ///< Per chunk.
    std::vector<std::uint8_t> leaf;     ///< Per chunk.
    std::vector<std::uint32_t> chunkPiece;
    std::vector<Piece> pieces;
    std::uint32_t staticPiece = kNoPiece;
    std::vector<std::uint32_t> newlyBroken; ///< Bonds broken since the last RebuildPieces.
    bool piecesDirty = true;
    bool stressChecked = false; ///< Overloaded bonds were looked for since the solver last converged.
    std::uint64_t lastDamageStep = 0;
};

DestructionWorld::DestructionWorld(physics::PhysicsWorld& physics, const DestructionSettings& settings)
    : m_physics(&physics)
    , m_settings(settings)
{
}

DestructionWorld::~DestructionWorld() = default;

std::shared_ptr<const DestructionWorld::AssetShapes>
DestructionWorld::ShapesFor(const std::shared_ptr<const DestructibleAsset>& asset)
{
    // A live entry means a live instance holds the asset, so its address is not reused.
    std::erase_if(m_shapeCache, [](const auto& entry) { return entry.second.expired(); });
    for (const auto& [key, weak] : m_shapeCache)
    {
        if (key == asset.get())
        {
            return weak.lock();
        }
    }
    auto shapes = std::make_shared<AssetShapes>();
    shapes->hulls.resize(asset->chunks.size());
    for (std::uint32_t chunk = 0; chunk < asset->chunks.size(); ++chunk)
    {
        if (asset->chunks[chunk].childCount != 0)
        {
            continue;
        }
        for (const CollisionHull& hull : asset->chunks[chunk].hulls)
        {
            JPH::Array<JPH::Vec3> points;
            points.reserve(hull.points.size());
            for (const Vec3& point : hull.points)
            {
                points.push_back(physics::ToJolt(point));
            }
            JPH::ConvexHullShapeSettings settings(points);
            settings.mUserData = chunk;
            const JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                INTERI_LOG_WARN("destruction", "chunk {}: skipping a collision hull Jolt rejects", chunk);
                continue;
            }
            shapes->hulls[chunk].emplace_back(result.Get());
        }
    }
    m_shapeCache.emplace_back(asset.get(), shapes);
    return shapes;
}

DestructibleId DestructionWorld::Spawn(World& world, JobSystem& jobs, std::shared_ptr<const DestructibleAsset> asset,
                                       const Transform& transform)
{
    INTERI_ASSERT(asset != nullptr);
    auto instance = std::make_unique<Instance>(std::move(asset), m_settings.stress);
    const DestructibleAsset& source = *instance->asset;
    instance->shapes = ShapesFor(instance->asset);
    instance->transform = transform;
    instance->maxHealth = InitialBondHealth(source);
    instance->health = instance->maxHealth;
    instance->broken.assign(source.bonds.size(), 0);
    for (const Bond& bond : source.bonds)
    {
        instance->edges.emplace_back(bond.chunkA, bond.chunkB);
    }
    const std::size_t chunkCount = source.chunks.size();
    instance->anchored.assign(chunkCount, 0);
    instance->leaf.assign(chunkCount, 0);
    instance->chunkPiece.assign(chunkCount, kNoPiece);
    for (const std::uint32_t anchor : source.anchors)
    {
        instance->anchored[anchor] = 1;
    }
    for (std::size_t chunk = 0; chunk < chunkCount; ++chunk)
    {
        instance->leaf[chunk] = source.chunks[chunk].childCount == 0 ? 1 : 0;
    }

    DestructibleId id;
    const auto freeSlot = std::ranges::find(m_instances, nullptr);
    id.index = static_cast<std::uint32_t>(freeSlot - m_instances.begin());
    if (freeSlot == m_instances.end())
    {
        m_instances.push_back(nullptr);
        m_generations.push_back(0);
    }
    id.generation = ++m_generations[id.index];
    instance->id = id;
    Instance& placed = *(m_instances[id.index] = std::move(instance));
    RebuildPieces(world, jobs, placed);
    return id;
}

bool DestructionWorld::Despawn(World& world, DestructibleId id)
{
    if (!IsAlive(id))
    {
        return false;
    }
    for (const Piece& piece : m_instances[id.index]->pieces)
    {
        if (piece.entity.IsValid())
        {
            world.DestroyEntity(piece.entity);
        }
    }
    m_instances[id.index].reset();
    return true;
}

void DestructionWorld::QueueDamage(const RadialDamage& damage, DamageKind kind)
{
    m_queuedDamage.emplace_back(damage, kind);
}

std::size_t DestructionWorld::InstanceCount() const noexcept
{
    return static_cast<std::size_t>(std::ranges::count_if(m_instances, [](const auto& i) { return i != nullptr; }));
}

bool DestructionWorld::IsAlive(DestructibleId id) const noexcept
{
    return id.IsValid() && id.index < m_instances.size() && m_instances[id.index] != nullptr &&
           m_generations[id.index] == id.generation;
}

DestructionWorld::Instance& DestructionWorld::Get(DestructibleId id)
{
    INTERI_ASSERT(IsAlive(id));
    return *m_instances[id.index];
}

const DestructionWorld::Instance& DestructionWorld::Get(DestructibleId id) const
{
    INTERI_ASSERT(IsAlive(id));
    return *m_instances[id.index];
}

const DestructibleAsset& DestructionWorld::Asset(DestructibleId id) const
{
    return *Get(id).asset;
}

std::span<const float> DestructionWorld::BondHealth(DestructibleId id) const
{
    return Get(id).health;
}

bool DestructionWorld::IsBondBroken(DestructibleId id, std::uint32_t bond) const
{
    return Get(id).broken[bond] != 0;
}

const StressSolver& DestructionWorld::Stress(DestructibleId id) const
{
    return Get(id).solver;
}

std::uint32_t DestructionWorld::PieceCount(DestructibleId id) const
{
    return static_cast<std::uint32_t>(Get(id).pieces.size());
}

Entity DestructionWorld::PieceEntity(DestructibleId id, std::uint32_t piece) const
{
    return Get(id).pieces[piece].entity;
}

bool DestructionWorld::PieceAnchored(DestructibleId id, std::uint32_t piece) const
{
    return Get(id).pieces[piece].anchored;
}

std::span<const std::uint32_t> DestructionWorld::PieceChunks(DestructibleId id, std::uint32_t piece) const
{
    return Get(id).pieces[piece].chunks;
}

std::optional<std::uint32_t> DestructionWorld::ChunkAt(const World& world, Entity entity, std::uint32_t subShape) const
{
    const DestructibleComponent* component = world.IsAlive(entity) ? world.Get<DestructibleComponent>(entity) : nullptr;
    const physics::ColliderComponent* collider =
        component != nullptr ? world.Get<physics::ColliderComponent>(entity) : nullptr;
    if (collider == nullptr || !IsAlive(component->instance))
    {
        return std::nullopt;
    }
    JPH::SubShapeID id;
    id.SetValue(subShape);
    return static_cast<std::uint32_t>(collider->shape->GetSubShapeUserData(id));
}

Transform DestructionWorld::PiecePose(const World& world, const Instance& instance, std::uint32_t piece) const
{
    if (piece == kNoPiece || instance.pieces[piece].anchored || !instance.pieces[piece].entity.IsValid())
    {
        return instance.transform;
    }
    const auto* transform = world.Get<Transform>(instance.pieces[piece].entity);
    return transform != nullptr ? *transform : instance.transform;
}

void DestructionWorld::ApplyDamage(World& world, Instance& instance, const RadialDamage& damage, float scale)
{
    const std::size_t bondCount = instance.asset->bonds.size();
    std::vector<Transform> poses(instance.pieces.size());
    for (std::uint32_t piece = 0; piece < poses.size(); ++piece)
    {
        poses[piece] = PiecePose(world, instance, piece);
    }
    std::vector<Vec3> centroids(bondCount);
    for (std::size_t bond = 0; bond < bondCount; ++bond)
    {
        const std::uint32_t piece = instance.chunkPiece[instance.asset->bonds[bond].chunkA];
        const Transform& pose = piece == kNoPiece ? instance.transform : poses[piece];
        centroids[bond] = pose.TransformPoint(instance.asset->bonds[bond].centroid);
    }
    const std::size_t before = instance.newlyBroken.size();
    if (ApplyRadialDamage(centroids, damage, scale, instance.health, instance.newlyBroken) == 0)
    {
        return;
    }
    for (std::size_t i = before; i < instance.newlyBroken.size(); ++i)
    {
        const std::uint32_t bond = instance.newlyBroken[i];
        instance.broken[bond] = 1;
        instance.solver.BreakBond(bond);
        instance.piecesDirty = true;
        ++m_stats.bondsBrokenByDamage;
    }
    // Weakened bonds have lower limits: look for overloads again.
    instance.stressChecked = false;
    instance.lastDamageStep = m_physics->StepCount();
}

void DestructionWorld::ApplyContactDamage(World& world)
{
    const std::uint64_t step = m_physics->StepCount();
    for (const physics::ContactEvent& contact : m_physics->Contacts())
    {
        const RadialDamage damage = ImpactDamage(contact.point, contact.impulse, m_settings.damage);
        if (!(damage.energy > 0.0f))
        {
            continue;
        }
        const auto component = [&](Entity entity) -> const DestructibleComponent*
        {
            const DestructibleComponent* c = world.IsAlive(entity) ? world.Get<DestructibleComponent>(entity) : nullptr;
            return c != nullptr && IsAlive(c->instance) ? c : nullptr;
        };
        const DestructibleComponent* a = component(contact.entityA);
        const DestructibleComponent* b = component(contact.entityB);
        if (a != nullptr && b != nullptr && a->instance == b->instance)
        {
            const Instance& instance = Get(a->instance);
            const auto young = [&](std::uint32_t piece)
            { return instance.pieces[piece].spawnStep + m_settings.spawnGraceSteps > step; };
            if (young(a->piece) || young(b->piece))
            {
                continue;
            }
            b = nullptr; // damage the instance once
        }
        for (const DestructibleComponent* side : {a, b})
        {
            if (side != nullptr)
            {
                ApplyDamage(world, Get(side->instance), damage, 1.0f);
            }
        }
    }
}

void DestructionWorld::RebuildPieces(World& world, JobSystem& jobs, Instance& instance)
{
    INTERI_ZONE_NAMED("DestructionWorld::RebuildPieces");
    const DestructibleAsset& asset = *instance.asset;
    const auto chunkCount = static_cast<std::uint32_t>(asset.chunks.size());
    std::vector<std::uint32_t> labels(chunkCount);
    LabelComponents(jobs, chunkCount, instance.edges, instance.broken, labels);
    const std::vector<Island> islands = GroupIslands(labels, instance.anchored, instance.leaf);

    std::vector<std::uint32_t> anchoredChunks;
    for (const Island& island : islands)
    {
        if (island.anchored)
        {
            anchoredChunks.insert(anchoredChunks.end(), island.nodes.begin(), island.nodes.end());
        }
    }
    std::ranges::sort(anchoredChunks);

    struct NewPiece
    {
        const std::vector<std::uint32_t>* chunks;
        bool anchored;
        std::uint32_t source;
        Transform pose;
        Vec3 linearVelocity;
        Vec3 angularVelocity;
    };
    std::vector<NewPiece> created;
    std::vector<std::uint8_t> keep(instance.pieces.size(), 0);

    const bool staticChanged = instance.staticPiece == kNoPiece
                                   ? !anchoredChunks.empty()
                                   : instance.pieces[instance.staticPiece].chunks != anchoredChunks;
    if (instance.staticPiece != kNoPiece && !staticChanged)
    {
        keep[instance.staticPiece] = 1;
    }
    if (staticChanged && !anchoredChunks.empty())
    {
        created.push_back({&anchoredChunks, true, kNoPiece, instance.transform, {}, {}});
    }

    JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
    for (const Island& island : islands)
    {
        if (island.anchored)
        {
            continue;
        }
        const std::uint32_t source = instance.chunkPiece[island.nodes.front()];
        if (source != kNoPiece && !instance.pieces[source].anchored && instance.pieces[source].chunks == island.nodes)
        {
            keep[source] = 1;
            continue;
        }
        NewPiece piece{&island.nodes, false, source, PiecePose(world, instance, source), {}, {}};

        // Centre of mass (asset space) and where the bonds that released it broke.
        Vec3 weighted;
        float mass = 0.0f;
        for (const std::uint32_t chunk : island.nodes)
        {
            weighted += asset.chunks[chunk].centroid * asset.chunks[chunk].mass;
            mass += asset.chunks[chunk].mass;
        }
        const Vec3 centerOfMass = mass > 0.0f ? weighted / mass : asset.chunks[island.nodes.front()].centroid;
        const Vec3 worldCenter = piece.pose.TransformPoint(centerOfMass);

        if (source != kNoPiece && !instance.pieces[source].anchored)
        {
            const Piece& parent = instance.pieces[source];
            const physics::PhysicsBodyComponent* body =
                parent.entity.IsValid() ? world.Get<physics::PhysicsBodyComponent>(parent.entity) : nullptr;
            if (body != nullptr)
            {
                piece.linearVelocity =
                    physics::FromJolt(bodies.GetPointVelocity(body->id, physics::ToJolt(worldCenter)));
                piece.angularVelocity = physics::FromJolt(bodies.GetAngularVelocity(body->id));
            }
            else
            {
                piece.linearVelocity = parent.linearVelocity;
                piece.angularVelocity = parent.angularVelocity;
            }
        }

        Vec3 breakPoint;
        std::uint32_t breakCount = 0;
        for (const std::uint32_t bond : instance.newlyBroken)
        {
            const Bond& b = asset.bonds[bond];
            const bool inA = std::ranges::binary_search(island.nodes, b.chunkA);
            const bool inB = std::ranges::binary_search(island.nodes, b.chunkB);
            if (inA != inB)
            {
                breakPoint += b.centroid;
                ++breakCount;
            }
        }
        if (breakCount > 0)
        {
            const Vec3 away = NormalizeOrZero(centerOfMass - breakPoint / static_cast<float>(breakCount));
            const Vec3 before = piece.linearVelocity;
            piece.linearVelocity += piece.pose.TransformVector(away) * m_settings.separationSpeed;
            m_stats.injectedEnergy += 0.5 * static_cast<double>(mass) *
                                      static_cast<double>(LengthSquared(piece.linearVelocity) - LengthSquared(before));
        }
        created.push_back(piece);
    }

    // Remove replaced pieces, then create the new ones (static first, then islands by smallest chunk).
    for (std::uint32_t index = 0; index < instance.pieces.size(); ++index)
    {
        Piece& piece = instance.pieces[index];
        if (keep[index] == 0 && !piece.chunks.empty())
        {
            if (piece.entity.IsValid())
            {
                world.DestroyEntity(piece.entity);
            }
            piece.entity = {};
            piece.chunks.clear();
        }
    }
    if (staticChanged)
    {
        instance.staticPiece = kNoPiece;
    }
    const DestructibleId id = instance.id;
    for (const NewPiece& source : created)
    {
        const auto index = static_cast<std::uint32_t>(instance.pieces.size());
        Piece& piece = instance.pieces.emplace_back();
        piece.anchored = source.anchored;
        piece.chunks = *source.chunks;
        piece.spawnStep = m_physics->StepCount();
        piece.linearVelocity = source.linearVelocity;
        piece.angularVelocity = source.angularVelocity;
        if (source.anchored)
        {
            instance.staticPiece = index;
        }

        std::vector<physics::ColliderPart> parts;
        float mass = 0.0f;
        float volume = 0.0f;
        for (const std::uint32_t chunk : piece.chunks)
        {
            mass += asset.chunks[chunk].mass;
            volume += asset.chunks[chunk].volume;
            for (const JPH::RefConst<JPH::Shape>& hull : instance.shapes->hulls[chunk])
            {
                parts.push_back({.position = {}, .rotation = {}, .collider = {.shape = hull}});
            }
        }
        physics::ColliderComponent collider;
        if (parts.size() == 1)
        {
            collider = parts.front().collider;
        }
        else if (!parts.empty())
        {
            Result<physics::ColliderComponent> compound = physics::ColliderComponent::Compound(parts);
            if (compound)
            {
                collider = std::move(*compound);
            }
            else
            {
                INTERI_LOG_WARN("destruction", "piece collider: {}", compound.error().message);
            }
        }
        if (collider.shape == nullptr)
        {
            continue; // nothing to collide with: the piece exists only in the graph
        }

        physics::RigidBodyComponent body;
        if (piece.anchored)
        {
            body.motion = physics::MotionType::Static;
            body.layer = m_settings.anchoredLayer;
        }
        else
        {
            body.motion = physics::MotionType::Dynamic;
            body.layer = volume <= m_settings.smallPieceVolume ? m_settings.smallPieceLayer : m_settings.pieceLayer;
            body.mass = mass;
            body.linearVelocity = piece.linearVelocity;
            body.angularVelocity = piece.angularVelocity;
        }
        piece.entity = world.CreateEntity(source.pose, body, std::move(collider),
                                          DestructibleComponent{.instance = id, .piece = index});
        if (!piece.anchored)
        {
            ++m_stats.piecesSpawned;
        }
    }

    for (const Piece& piece : instance.pieces)
    {
        const auto index = static_cast<std::uint32_t>(&piece - instance.pieces.data());
        for (const std::uint32_t chunk : piece.chunks)
        {
            instance.chunkPiece[chunk] = index;
        }
    }
    instance.newlyBroken.clear();
    instance.piecesDirty = false;
}

void DestructionWorld::Update(World& world, JobSystem& jobs)
{
    INTERI_ZONE_NAMED("DestructionWorld::Update");
    m_stats = {};

    ApplyContactDamage(world);
    for (const auto& [damage, kind] : m_queuedDamage)
    {
        const float scale =
            kind == DamageKind::Explosion ? m_settings.damage.explosionScale : m_settings.damage.meleeScale;
        for (const std::unique_ptr<Instance>& instance : m_instances)
        {
            if (instance != nullptr)
            {
                ApplyDamage(world, *instance, damage, scale);
            }
        }
    }
    std::vector<std::pair<RadialDamage, float>> pushes;
    for (const auto& [damage, kind] : m_queuedDamage)
    {
        const float speed = kind == DamageKind::Explosion ? m_settings.explosionPushSpeed : m_settings.meleePushSpeed;
        if (speed > 0.0f)
        {
            pushes.emplace_back(damage, speed);
        }
    }
    m_queuedDamage.clear();

    const std::uint64_t now = m_physics->StepCount();
    std::vector<StressTask> tasks;
    std::vector<float> limitScale;
    std::vector<std::uint32_t> overloaded;
    for (std::uint32_t round = 0; round < m_settings.maxBreakRounds; ++round)
    {
        ++m_stats.breakRounds;
        tasks.clear();
        for (const std::unique_ptr<Instance>& instance : m_instances)
        {
            if (instance == nullptr)
            {
                continue;
            }
            if (instance->piecesDirty)
            {
                RebuildPieces(world, jobs, *instance);
            }
            const auto age = static_cast<float>(now - std::min(now, instance->lastDamageStep));
            tasks.push_back({.solver = &instance->solver, .importance = 1.0f / (1.0f + age)});
        }
        const std::uint64_t remaining =
            m_settings.stressWorkBudget - std::min(m_stats.stressWork, m_settings.stressWorkBudget);
        m_stats.stressWork += SolveWithBudget(jobs, tasks, remaining).workUsed;

        bool brokeAny = false;
        for (const std::unique_ptr<Instance>& instance : m_instances)
        {
            if (instance == nullptr || instance->stressChecked || !instance->solver.IsConverged())
            {
                continue;
            }
            limitScale.resize(instance->health.size());
            for (std::size_t bond = 0; bond < limitScale.size(); ++bond)
            {
                limitScale[bond] =
                    instance->maxHealth[bond] > 0.0f ? instance->health[bond] / instance->maxHealth[bond] : 1.0f;
            }
            instance->solver.FindOverloadedBonds(jobs, limitScale, overloaded);
            instance->stressChecked = true;
            for (const std::uint32_t bond : overloaded)
            {
                instance->broken[bond] = 1;
                instance->health[bond] = 0.0f;
                instance->solver.BreakBond(bond);
                instance->newlyBroken.push_back(bond);
                instance->piecesDirty = true;
                instance->stressChecked = false;
                ++m_stats.bondsBrokenByStress;
                brokeAny = true;
            }
        }
        if (!brokeAny)
        {
            break;
        }
    }
    for (const std::unique_ptr<Instance>& instance : m_instances)
    {
        if (instance == nullptr)
        {
            continue;
        }
        if (instance->piecesDirty)
        {
            RebuildPieces(world, jobs, *instance);
        }
        for (const auto& [damage, speed] : pushes)
        {
            ApplyPush(world, *instance, damage, speed);
        }
    }
}

void DestructionWorld::ApplyPush(World& world, Instance& instance, const RadialDamage& damage, float speed)
{
    const DestructibleAsset& asset = *instance.asset;
    JPH::BodyInterface& bodies = m_physics->BodiesNoLock();
    for (std::uint32_t index = 0; index < instance.pieces.size(); ++index)
    {
        const Piece& piece = instance.pieces[index];
        if (piece.anchored || piece.chunks.empty() || !piece.entity.IsValid())
        {
            continue;
        }
        Vec3 weighted;
        float mass = 0.0f;
        for (const std::uint32_t chunk : piece.chunks)
        {
            weighted += asset.chunks[chunk].centroid * asset.chunks[chunk].mass;
            mass += asset.chunks[chunk].mass;
        }
        if (!(mass > 0.0f))
        {
            continue;
        }
        const Vec3 center = PiecePose(world, instance, index).TransformPoint(weighted / mass);
        const float distance = Distance(center, damage.center);
        const float falloff = RadialFalloff(distance, damage.innerRadius, damage.radius);
        if (!(falloff > 0.0f))
        {
            continue;
        }
        const Vec3 push = NormalizeOrZero(center - damage.center) * (speed * falloff);
        Vec3 velocity;
        const auto* body = world.Get<physics::PhysicsBodyComponent>(piece.entity);
        if (body != nullptr)
        {
            velocity = physics::FromJolt(bodies.GetLinearVelocity(body->id));
            bodies.AddLinearVelocity(body->id, physics::ToJolt(push));
        }
        else
        {
            auto* pending = world.GetMut<physics::RigidBodyComponent>(piece.entity);
            velocity = pending->linearVelocity;
            pending->linearVelocity += push;
        }
        m_stats.injectedEnergy += 0.5 * static_cast<double>(mass) *
                                  static_cast<double>(LengthSquared(velocity + push) - LengthSquared(velocity));
    }
}

} // namespace interi::destruction
