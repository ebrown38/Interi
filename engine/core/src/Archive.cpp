// SPDX-License-Identifier: MIT
#include <interi/core/Archive.h>

#include <format>

namespace interi
{

Result<ArchiveReader> ArchiveReader::Open(std::span<const std::byte> data, FourCC magic, std::uint32_t currentVersion)
{
    ArchiveReader reader(data);
    FourCC storedMagic = 0;
    std::uint32_t storedVersion = 0;
    reader(storedMagic, storedVersion);
    if (!reader.Ok())
    {
        return MakeError(ErrorCode::Corrupt, "archive is too short for its header");
    }
    if (storedMagic != magic)
    {
        return MakeError(ErrorCode::Corrupt, "archive has the wrong file tag");
    }
    if (storedVersion > currentVersion)
    {
        return MakeError(
            ErrorCode::VersionMismatch,
            std::format("archive version {} is newer than supported version {}", storedVersion, currentVersion));
    }
    reader.m_version = storedVersion;
    return reader;
}

Result<> ArchiveReader::Finish() const
{
    if (!m_ok)
    {
        return MakeError(ErrorCode::Corrupt, "archive data is truncated or malformed");
    }
    if (m_position != m_data.size())
    {
        return MakeError(ErrorCode::Corrupt, std::format("{} unread bytes at end of archive", Remaining()));
    }
    return {};
}

bool ArchiveReader::ReadRaw(void* destination, std::size_t size) noexcept
{
    if (!m_ok || size > Remaining())
    {
        m_ok = false;
        if (size > 0)
        {
            std::memset(destination, 0, size);
        }
        return false;
    }
    if (size > 0)
    {
        std::memcpy(destination, m_data.data() + m_position, size);
    }
    m_position += size;
    return true;
}

std::size_t ArchiveReader::ReadCount() noexcept
{
    std::uint32_t count = 0;
    Process(count);
    if (!m_ok || count > Remaining())
    {
        m_ok = false;
        return 0;
    }
    return count;
}

} // namespace interi
