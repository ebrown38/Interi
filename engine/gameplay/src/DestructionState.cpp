// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>
#include <interi/core/Hash.h>
#include <interi/gameplay/DestructionState.h>
#include <interi/physics/Components.h>
#include <interi/physics/JoltMath.h>

#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <format>

namespace interi::gameplay
{

namespace
{

constexpr FourCC kPayloadMagic = MakeFourCC("IDSP");

template <typename Archive>
void SerializeQuat(Archive& archive, Quat& q)
{
    archive(q.x, q.y, q.z, q.w);
}

} // namespace

template <typename Archive>
void Serialize(Archive& archive, PieceState& piece)
{
    archive(piece.firstChunk, piece.pose.position);
    SerializeQuat(archive, piece.pose.rotation);
    archive(piece.pose.scale, piece.linearVelocity, piece.angularVelocity);
}

template <typename Archive>
void Serialize(Archive& archive, InstanceDamageState& instance)
{
    archive(instance.bondHealth, instance.releasedChunks, instance.pieces);
}

template <typename Archive>
void Serialize(Archive& archive, DestructionState& state)
{
    archive(state.instances);
}

DestructionState CaptureDestructionState(const World& world, const destruction::DestructionWorld& destruction,
                                         physics::PhysicsWorld& physics)
{
    DestructionState state;
    JPH::BodyInterface& bodies = physics.Bodies();
    for (const destruction::DestructibleId id : destruction.Instances())
    {
        InstanceDamageState& instance = state.instances.emplace_back();
        const std::span<const float> health = destruction.BondHealth(id);
        instance.bondHealth.assign(health.begin(), health.end());
        const auto chunkCount = static_cast<std::uint32_t>(destruction.Asset(id).chunks.size());
        for (std::uint32_t chunk = 0; chunk < chunkCount; ++chunk)
        {
            if (destruction.IsChunkReleased(id, chunk))
            {
                instance.releasedChunks.push_back(chunk);
            }
        }
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            const std::span<const std::uint32_t> chunks = destruction.PieceChunks(id, piece);
            const Entity entity = destruction.PieceEntity(id, piece);
            if (chunks.empty() || destruction.PieceAnchored(id, piece) || !world.IsAlive(entity))
            {
                continue;
            }
            PieceState saved;
            saved.firstChunk = chunks.front();
            saved.pose = *world.Get<Transform>(entity);
            if (const auto* body = world.Get<physics::PhysicsBodyComponent>(entity))
            {
                saved.linearVelocity = physics::FromJolt(bodies.GetLinearVelocity(body->id));
                saved.angularVelocity = physics::FromJolt(bodies.GetAngularVelocity(body->id));
            }
            else if (const auto* rigid = world.Get<physics::RigidBodyComponent>(entity))
            {
                saved.linearVelocity = rigid->linearVelocity;
                saved.angularVelocity = rigid->angularVelocity;
            }
            instance.pieces.push_back(saved);
        }
        std::ranges::sort(instance.pieces, {}, &PieceState::firstChunk);
    }
    return state;
}

Result<> RestoreDestructionState(World& world, JobSystem& jobs, destruction::DestructionWorld& destruction,
                                 physics::PhysicsWorld& physics, const DestructionState& state)
{
    const std::vector<destruction::DestructibleId> ids = destruction.Instances();
    if (ids.size() != state.instances.size())
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("save has {} destructibles, the level {}", state.instances.size(), ids.size()));
    }
    JPH::BodyInterface& bodies = physics.Bodies();
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        const destruction::DestructibleId id = ids[i];
        const InstanceDamageState& saved = state.instances[i];
        INTERI_TRY(destruction.RestoreDamage(world, jobs, id, saved.bondHealth, saved.releasedChunks));
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            const std::span<const std::uint32_t> chunks = destruction.PieceChunks(id, piece);
            const Entity entity = destruction.PieceEntity(id, piece);
            if (chunks.empty() || destruction.PieceAnchored(id, piece) || !world.IsAlive(entity))
            {
                continue;
            }
            const auto found = std::ranges::lower_bound(saved.pieces, chunks.front(), {}, &PieceState::firstChunk);
            if (found == saved.pieces.end() || found->firstChunk != chunks.front())
            {
                continue; // not in the save (e.g. it broke off after saving); stays at rest where the asset is
            }
            if (const auto* body = world.Get<physics::PhysicsBodyComponent>(entity))
            {
                bodies.SetPositionRotationAndVelocity(
                    body->id, physics::ToJolt(found->pose.position), physics::ToJolt(found->pose.rotation),
                    physics::ToJolt(found->linearVelocity), physics::ToJolt(found->angularVelocity));
            }
            *world.GetMut<Transform>(entity) = found->pose;
            if (auto* rigid = world.GetMut<physics::RigidBodyComponent>(entity))
            {
                rigid->linearVelocity = found->linearVelocity;
                rigid->angularVelocity = found->angularVelocity;
            }
        }
    }
    return {};
}

std::vector<std::byte> WriteDestructionState(const DestructionState& state)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kPayloadMagic, kDestructionStateVersion);
        DestructionState copy = state; // Serialize takes non-const references (one function for both directions)
        writer(copy);
    }
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, kDestructionStateMagic, kDestructionStateVersion);
    writer(payloadHash, payload);
    return file;
}

Result<DestructionState> ReadDestructionState(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, kDestructionStateMagic, kDestructionStateVersion));
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, "destruction state payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kPayloadMagic, kDestructionStateVersion));
    DestructionState state;
    reader(state);
    INTERI_TRY(reader.Finish());
    return state;
}

} // namespace interi::gameplay
