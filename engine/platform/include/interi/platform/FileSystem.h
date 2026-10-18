// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <cstddef>
#include <filesystem>
#include <map>
#include <shared_mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi
{

/// Converts a virtual path to canonical form: '/'-separated components, no leading or trailing separator, '.' and
/// empty components removed, '\\' accepted as a separator. Fails with InvalidArgument for '..' components and for
/// ':' (drive letters, alternate data streams), so a virtual path can never leave its mount.
[[nodiscard]] Result<std::string> NormalizeVirtualPath(std::string_view path);

/// A tree of files addressed by virtual paths (see NormalizeVirtualPath). Implementations normalise their input.
/// Const operations are safe to call from several threads at once; whether writes may overlap reads is up to the
/// implementation (both backends below allow it).
class IFileSystem
{
public:
    IFileSystem() = default;
    IFileSystem(const IFileSystem&) = delete;
    IFileSystem& operator=(const IFileSystem&) = delete;
    IFileSystem(IFileSystem&&) = delete;
    IFileSystem& operator=(IFileSystem&&) = delete;
    virtual ~IFileSystem() = default;

    [[nodiscard]] virtual bool Exists(std::string_view path) const = 0;
    /// The whole file. NotFound if it doesn't exist, IoError if it can't be read.
    [[nodiscard]] virtual Result<std::vector<std::byte>> ReadFile(std::string_view path) const = 0;
    /// Creates or replaces the file, creating parent directories as needed.
    virtual Result<void> WriteFile(std::string_view path, std::span<const std::byte> data) = 0;
};

/// Files under a directory on disk.
class NativeFileSystem final : public IFileSystem
{
public:
    explicit NativeFileSystem(std::filesystem::path root);

    [[nodiscard]] bool Exists(std::string_view path) const override;
    [[nodiscard]] Result<std::vector<std::byte>> ReadFile(std::string_view path) const override;
    Result<void> WriteFile(std::string_view path, std::span<const std::byte> data) override;

    [[nodiscard]] const std::filesystem::path& Root() const noexcept { return m_root; }

private:
    [[nodiscard]] Result<std::filesystem::path> Resolve(std::string_view path) const;

    std::filesystem::path m_root;
};

/// Files held in memory. Thread-safe.
class MemoryFileSystem final : public IFileSystem
{
public:
    [[nodiscard]] bool Exists(std::string_view path) const override;
    [[nodiscard]] Result<std::vector<std::byte>> ReadFile(std::string_view path) const override;
    Result<void> WriteFile(std::string_view path, std::span<const std::byte> data) override;

private:
    mutable std::shared_mutex m_mutex;
    std::map<std::string, std::vector<std::byte>, std::less<>> m_files;
};

/// Overlays several file systems under virtual mount prefixes. A lookup tries the mounts newest first and uses the
/// first one whose prefix matches (whole path components) and that has the file, so a later mount overrides an
/// earlier one and falls through to it for files it lacks. Writes go to the newest mount whose prefix matches.
///
/// Mounted file systems are not owned and must outlive the VFS. Mount must not overlap any other call; the other
/// operations are as thread-safe as the mounted backends.
class VirtualFileSystem final : public IFileSystem
{
public:
    /// Mounts `fileSystem` at `prefix` ("" = root). The prefix is normalised.
    Result<void> Mount(std::string_view prefix, IFileSystem& fileSystem);
    /// Removes every mount of `fileSystem` (before it is destroyed). False if it wasn't mounted.
    bool Unmount(const IFileSystem& fileSystem);

    [[nodiscard]] bool Exists(std::string_view path) const override;
    [[nodiscard]] Result<std::vector<std::byte>> ReadFile(std::string_view path) const override;
    Result<void> WriteFile(std::string_view path, std::span<const std::byte> data) override;

private:
    struct MountPoint
    {
        std::string prefix;
        IFileSystem* fileSystem;
    };

    /// When `path` lies under `mount`'s prefix, sets `relative` to the rest of it and returns true.
    [[nodiscard]] static bool StripPrefix(const MountPoint& mount, std::string_view path, std::string_view& relative);

    std::vector<MountPoint> m_mounts; ///< Oldest first.
};

} // namespace interi
