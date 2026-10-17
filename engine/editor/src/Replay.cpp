// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/editor/Replay.h>

namespace interi::editor
{

namespace
{

constexpr FourCC kPayloadMagic = MakeFourCC("IRPP");

} // namespace

template <typename Archive>
void Serialize(Archive& archive, ReplayEvent& event)
{
    archive(event.step, event.damage.center, event.damage.energy, event.damage.innerRadius, event.damage.radius);
}

template <typename Archive>
void Serialize(Archive& archive, Replay& replay)
{
    archive(replay.levelPath, replay.levelJson, replay.steps, replay.events, replay.finalHash);
}

std::vector<std::byte> WriteReplay(const Replay& replay)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, kPayloadMagic, kReplayVersion);
        Replay copy = replay; // Serialize takes non-const references (one function for both directions)
        writer(copy);
    }
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, kReplayMagic, kReplayVersion);
    writer(payloadHash, payload);
    return file;
}

Result<Replay> ReadReplay(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, kReplayMagic, kReplayVersion));
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, "replay payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, kPayloadMagic, kReplayVersion));
    Replay replay;
    reader(replay);
    INTERI_TRY(reader.Finish());
    return replay;
}

} // namespace interi::editor
