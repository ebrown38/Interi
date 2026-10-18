// SPDX-License-Identifier: MIT
#include <interi/platform/FileSystem.h>

#include <cstdint>
#include <fstream>
#include <mutex>
#include <ranges>
#include <system_error>
#include <utility>

namespace interi
{

Result<std::string> NormalizeVirtualPath(std::string_view path)
{
    std::string normalized;
    normalized.reserve(path.size());
    std::size_t begin = 0;
    while (begin <= path.size())
    {
        std::size_t end = path.find_first_of("/\\", begin);
        if (end == std::string_view::npos)
        {
            end = path.size();
        }
        const std::string_view component = path.substr(begin, end - begin);
        if (component == "..")
        {
            return MakeError(ErrorCode::InvalidArgument, "virtual path must not contain '..': " + std::string(path));
        }
        if (component.find(':') != std::string_view::npos)
        {
            return MakeError(ErrorCode::InvalidArgument, "virtual path must not contain ':': " + std::string(path));
        }
        if (!component.empty() && component != ".")
        {
            if (!normalized.empty())
            {
                normalized.push_back('/');
            }
            normalized.append(component);
        }
        begin = end + 1;
    }
    return normalized;
}

// --- NativeFileSystem -------------------------------------------------------------------------------------------

NativeFileSystem::NativeFileSystem(std::filesystem::path root)
    : m_root(std::move(root))
{
}

Result<std::filesystem::path> NativeFileSystem::Resolve(std::string_view path) const
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(path));
    return m_root / std::filesystem::path(std::u8string(normalized.begin(), normalized.end()));
}

bool NativeFileSystem::Exists(std::string_view path) const
{
    const auto resolved = Resolve(path);
    std::error_code error;
    return resolved.has_value() && std::filesystem::is_regular_file(*resolved, error);
}

Result<std::vector<std::byte>> NativeFileSystem::ReadFile(std::string_view path) const
{
    INTERI_TRY_ASSIGN(resolved, Resolve(path));
    std::error_code error;
    if (!std::filesystem::is_regular_file(resolved, error))
    {
        return MakeError(ErrorCode::NotFound, "no such file: " + std::string(path));
    }
    const std::uintmax_t size = std::filesystem::file_size(resolved, error);
    std::ifstream stream(resolved, std::ios::binary);
    if (error || !stream)
    {
        return MakeError(ErrorCode::IoError, "cannot open file: " + std::string(path));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()))
    {
        return MakeError(ErrorCode::IoError, "short read: " + std::string(path));
    }
    return bytes;
}

Result<void> NativeFileSystem::WriteFile(std::string_view path, std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(resolved, Resolve(path));
    std::error_code error;
    std::filesystem::create_directories(resolved.parent_path(), error);
    if (error)
    {
        return MakeError(ErrorCode::IoError, "cannot create directories for: " + std::string(path));
    }
    std::ofstream stream(resolved, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    stream.close();
    if (!stream)
    {
        return MakeError(ErrorCode::IoError, "cannot write file: " + std::string(path));
    }
    return {};
}

// --- MemoryFileSystem -------------------------------------------------------------------------------------------

bool MemoryFileSystem::Exists(std::string_view path) const
{
    const auto normalized = NormalizeVirtualPath(path);
    if (!normalized.has_value())
    {
        return false;
    }
    const std::shared_lock lock(m_mutex);
    return m_files.contains(*normalized);
}

Result<std::vector<std::byte>> MemoryFileSystem::ReadFile(std::string_view path) const
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(path));
    const std::shared_lock lock(m_mutex);
    const auto found = m_files.find(normalized);
    if (found == m_files.end())
    {
        return MakeError(ErrorCode::NotFound, "no such file: " + normalized);
    }
    return found->second;
}

Result<void> MemoryFileSystem::WriteFile(std::string_view path, std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(path));
    std::vector<std::byte> bytes(data.begin(), data.end());
    const std::unique_lock lock(m_mutex);
    m_files.insert_or_assign(std::move(normalized), std::move(bytes));
    return {};
}

// --- VirtualFileSystem ------------------------------------------------------------------------------------------

Result<void> VirtualFileSystem::Mount(std::string_view prefix, IFileSystem& fileSystem)
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(prefix));
    m_mounts.push_back(MountPoint{std::move(normalized), &fileSystem});
    return {};
}

bool VirtualFileSystem::Unmount(const IFileSystem& fileSystem)
{
    return std::erase_if(m_mounts, [&](const MountPoint& mount) { return mount.fileSystem == &fileSystem; }) > 0;
}

bool VirtualFileSystem::StripPrefix(const MountPoint& mount, std::string_view path, std::string_view& relative)
{
    if (mount.prefix.empty())
    {
        relative = path;
        return true;
    }
    if (!path.starts_with(mount.prefix))
    {
        return false;
    }
    if (path.size() == mount.prefix.size())
    {
        relative = {};
        return true;
    }
    if (path[mount.prefix.size()] != '/')
    {
        return false;
    }
    relative = path.substr(mount.prefix.size() + 1);
    return true;
}

bool VirtualFileSystem::Exists(std::string_view path) const
{
    const auto normalized = NormalizeVirtualPath(path);
    if (!normalized.has_value())
    {
        return false;
    }
    for (const MountPoint& mount : std::views::reverse(m_mounts))
    {
        std::string_view relative;
        if (StripPrefix(mount, *normalized, relative) && mount.fileSystem->Exists(relative))
        {
            return true;
        }
    }
    return false;
}

Result<std::vector<std::byte>> VirtualFileSystem::ReadFile(std::string_view path) const
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(path));
    for (const MountPoint& mount : std::views::reverse(m_mounts))
    {
        std::string_view relative;
        if (!StripPrefix(mount, normalized, relative))
        {
            continue;
        }
        auto result = mount.fileSystem->ReadFile(relative);
        if (result.has_value() || result.error().code != ErrorCode::NotFound)
        {
            return result;
        }
    }
    return MakeError(ErrorCode::NotFound, "no mount has file: " + normalized);
}

Result<void> VirtualFileSystem::WriteFile(std::string_view path, std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(normalized, NormalizeVirtualPath(path));
    for (const MountPoint& mount : std::views::reverse(m_mounts))
    {
        std::string_view relative;
        if (StripPrefix(mount, normalized, relative))
        {
            return mount.fileSystem->WriteFile(relative, data);
        }
    }
    return MakeError(ErrorCode::NotFound, "no mount covers: " + normalized);
}

} // namespace interi
