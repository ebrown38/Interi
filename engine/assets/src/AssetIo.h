// SPDX-License-Identifier: MIT
#pragma once

// The container every cooked asset shares: magic, version, the XXH3 of the payload, then the payload (its own
// archive with its own magic). Private to the library.

#include <interi/core/Archive.h>
#include <interi/core/Hash.h>
#include <interi/core/Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace interi::assets::detail
{

template <typename T>
[[nodiscard]] std::vector<std::byte> WriteHashed(FourCC magic, FourCC payloadMagic, std::uint32_t version, T value)
{
    std::vector<std::byte> payload;
    {
        ArchiveWriter writer(payload, payloadMagic, version);
        writer(value);
    }
    std::uint64_t payloadHash = HashBytes(payload.data(), payload.size());
    std::vector<std::byte> file;
    ArchiveWriter writer(file, magic, version);
    writer(payloadHash, payload);
    return file;
}

/// Reads the container and the value; the caller validates the value. Corrupt for a bad hash or truncated data,
/// VersionMismatch for a newer version.
template <typename T>
[[nodiscard]] Result<T> ReadHashed(std::span<const std::byte> data, FourCC magic, FourCC payloadMagic,
                                   std::uint32_t version, const char* what)
{
    INTERI_TRY_ASSIGN(header, ArchiveReader::Open(data, magic, version));
    std::uint64_t payloadHash = 0;
    std::vector<std::byte> payload;
    header(payloadHash, payload);
    INTERI_TRY(header.Finish());
    if (HashBytes(payload.data(), payload.size()) != payloadHash)
    {
        return MakeError(ErrorCode::Corrupt, std::string(what) + " payload hash mismatch");
    }
    INTERI_TRY_ASSIGN(reader, ArchiveReader::Open(payload, payloadMagic, version));
    T value;
    reader(value);
    INTERI_TRY(reader.Finish());
    return value;
}

} // namespace interi::assets::detail
