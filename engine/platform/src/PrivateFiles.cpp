// SPDX-License-Identifier: MIT
#include <interi/platform/PrivateFiles.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <format>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace interi
{

namespace
{

std::filesystem::path FromEnvironment(const char* name)
{
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    std::filesystem::path result;
    if (_dupenv_s(&value, &length, name) == 0 && value != nullptr)
    {
        result = value;
        std::free(value); // NOLINT(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
    }
    return result;
#else
    const char* value = std::getenv(name); // NOLINT(concurrency-mt-unsafe): read at start-up, before threads race
    return value != nullptr ? std::filesystem::path(value) : std::filesystem::path();
#endif
}

Result<std::vector<std::string>> SplitRelative(std::string_view relative)
{
    std::vector<std::string> parts;
    for (std::size_t start = 0; start <= relative.size();)
    {
        const std::size_t end = std::min(relative.find('/', start), relative.size());
        const std::string_view part = relative.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("'{}' is not a normalised relative path", relative));
        }
        parts.emplace_back(part);
        start = end + 1;
    }
    return parts;
}

#if defined(_WIN32)
class ScopedHandle
{
public:
    explicit ScopedHandle(HANDLE handle) noexcept
        : m_handle(handle)
    {
    }
    ScopedHandle(ScopedHandle&& other) noexcept
        : m_handle(std::exchange(other.m_handle, INVALID_HANDLE_VALUE))
    {
    }
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle& operator=(const ScopedHandle&) = delete;
    ScopedHandle& operator=(ScopedHandle&&) = delete;
    ~ScopedHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(m_handle);
        }
    }
    [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }

private:
    HANDLE m_handle;
};
#else
class ScopedFd
{
public:
    explicit ScopedFd(int fd) noexcept
        : m_fd(fd)
    {
    }
    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;
    ScopedFd(ScopedFd&&) = delete;
    ScopedFd& operator=(ScopedFd&&) = delete;
    ~ScopedFd()
    {
        if (m_fd >= 0)
        {
            ::close(m_fd);
        }
    }
    [[nodiscard]] int Get() const noexcept { return m_fd; }
    void Reset(int fd) noexcept
    {
        if (m_fd >= 0)
        {
            ::close(m_fd);
        }
        m_fd = fd;
    }

private:
    int m_fd;
};
#endif

} // namespace

Result<std::filesystem::path> PerUserDataDirectory()
{
#if defined(_WIN32)
    if (auto local = FromEnvironment("LOCALAPPDATA"); !local.empty())
    {
        return local / "Interi";
    }
#else
    if (auto runtime = FromEnvironment("XDG_RUNTIME_DIR"); !runtime.empty())
    {
        return runtime / "interi";
    }
    if (auto cache = FromEnvironment("XDG_CACHE_HOME"); !cache.empty())
    {
        return cache / "interi";
    }
    if (auto home = FromEnvironment("HOME"); !home.empty())
    {
        return home / ".cache" / "interi";
    }
#endif
    return MakeError(ErrorCode::NotFound, "no per-user directory: the environment names no home");
}

Result<void> EnsurePrivateDirectory(const std::filesystem::path& directory)
{
    std::error_code error;
    std::filesystem::create_directories(directory.parent_path(), error);
#if defined(_WIN32)
    std::filesystem::create_directory(directory, error);
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(directory, error)))
    {
        return MakeError(ErrorCode::IoError, std::format("'{}' is a symbolic link", directory.string()));
    }
    if (!std::filesystem::is_directory(directory, error))
    {
        return MakeError(ErrorCode::IoError, std::format("'{}' is not a directory", directory.string()));
    }
#else
    if (::mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST)
    {
        return MakeError(ErrorCode::IoError, std::format("creating '{}': {}", directory.string(),
                                                         std::error_code(errno, std::generic_category()).message()));
    }
    struct stat info{};
    if (::lstat(directory.c_str(), &info) != 0)
    {
        return MakeError(ErrorCode::IoError, std::format("'{}' cannot be inspected", directory.string()));
    }
    if (S_ISLNK(info.st_mode) || !S_ISDIR(info.st_mode))
    {
        return MakeError(ErrorCode::IoError, std::format("'{}' is not a plain directory", directory.string()));
    }
    if (info.st_uid != ::geteuid())
    {
        return MakeError(ErrorCode::IoError, std::format("'{}' is owned by another user", directory.string()));
    }
    if ((info.st_mode & (S_IWGRP | S_IWOTH)) != 0)
    {
        return MakeError(ErrorCode::IoError,
                         std::format("'{}' is writable by other users; use mode 0700", directory.string()));
    }
#endif
    return {};
}

Result<void> CopyFileExclusive(const std::filesystem::path& from, const std::filesystem::path& to)
{
#if defined(_WIN32)
    std::error_code error;
    if (!std::filesystem::copy_file(from, to, std::filesystem::copy_options::none, error))
    {
        return MakeError(ErrorCode::IoError,
                         std::format("copying '{}' to '{}': {}", from.string(), to.string(), error.message()));
    }
    return {};
#else
    const auto fail = [&](const char* what)
    {
        return MakeError(ErrorCode::IoError,
                         std::format("copying '{}' to '{}': {} ({})", from.string(), to.string(), what,
                                     std::error_code(errno, std::generic_category()).message()));
    };
    const int in = ::open(from.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0)
    {
        return fail("opening the source");
    }
    const int out = ::open(to.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0700);
    if (out < 0)
    {
        const auto result = fail("creating the copy");
        ::close(in);
        return result;
    }
    char buffer[1 << 16];
    for (;;)
    {
        const ssize_t got = ::read(in, buffer, sizeof buffer);
        if (got < 0 && errno == EINTR)
        {
            continue;
        }
        if (got <= 0)
        {
            const bool failed = got < 0;
            const auto result = failed ? fail("reading") : Result<void>();
            ::close(in);
            if (::close(out) != 0 && !failed)
            {
                const auto closeError = fail("closing the copy");
                ::unlink(to.c_str());
                return closeError;
            }
            if (failed)
            {
                ::unlink(to.c_str());
            }
            return result;
        }
        for (ssize_t done = 0; done < got;)
        {
            const ssize_t put = ::write(out, buffer + done, static_cast<std::size_t>(got - done));
            if (put < 0 && errno == EINTR)
            {
                continue;
            }
            if (put <= 0)
            {
                const auto result = fail("writing");
                ::close(in);
                ::close(out);
                ::unlink(to.c_str());
                return result;
            }
            done += put;
        }
    }
#endif
}

Result<bool> RemoveFileBeneath(const std::filesystem::path& base, std::string_view relative)
{
    INTERI_TRY_ASSIGN(parts, SplitRelative(relative));
#if defined(_WIN32)
    const auto fail = [&](const char* what)
    {
        return MakeError(ErrorCode::IoError,
                         std::format("removing '{}' beneath '{}': {} ({})", relative, base.string(), what,
                                     std::system_category().message(static_cast<int>(::GetLastError()))));
    };
    const auto missing = []
    {
        const DWORD code = ::GetLastError();
        return code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND;
    };
    // Every directory on the way stays open without FILE_SHARE_DELETE, so none of them can be renamed or replaced
    // until the delete is done; each is opened as itself (FILE_FLAG_OPEN_REPARSE_POINT) and refused if it is a link.
    std::vector<ScopedHandle> held;
    std::filesystem::path path = base;
    held.emplace_back(::CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (held.back().Get() == INVALID_HANDLE_VALUE)
    {
        return missing() ? Result<bool>(false) : Result<bool>(fail("opening the base directory"));
    }
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        path /= parts[i];
        const bool leaf = i + 1 == parts.size();
        held.emplace_back(::CreateFileW(
            path.c_str(), leaf ? DELETE | FILE_READ_ATTRIBUTES : FILE_READ_ATTRIBUTES,
            leaf ? FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE : FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | (leaf ? 0 : FILE_FLAG_BACKUP_SEMANTICS), nullptr));
        if (held.back().Get() == INVALID_HANDLE_VALUE)
        {
            return missing() ? Result<bool>(false) : Result<bool>(fail("opening a component"));
        }
        BY_HANDLE_FILE_INFORMATION info{};
        if (!::GetFileInformationByHandle(held.back().Get(), &info))
        {
            return fail("inspecting a component");
        }
        const bool directory = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if ((info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 || directory == leaf)
        {
            return MakeError(ErrorCode::IoError,
                             std::format("removing '{}' beneath '{}': '{}' is a link or of the wrong "
                                         "kind",
                                         relative, base.string(), parts[i]));
        }
    }
    FILE_DISPOSITION_INFO disposition{};
    disposition.DeleteFile = TRUE;
    if (!::SetFileInformationByHandle(held.back().Get(), FileDispositionInfo, &disposition, sizeof disposition))
    {
        return fail("deleting");
    }
    return true;
#else
    const auto fail = [&](const char* what)
    {
        return MakeError(ErrorCode::IoError,
                         std::format("removing '{}' beneath '{}': {} ({})", relative, base.string(), what,
                                     std::error_code(errno, std::generic_category()).message()));
    };
    ScopedFd dir(::open(base.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (dir.Get() < 0)
    {
        return errno == ENOENT ? Result<bool>(false) : Result<bool>(fail("opening the base directory"));
    }
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        // O_NOFOLLOW fails with ELOOP (or ENOTDIR) on a link: the walk never leaves the directory it started in.
        dir.Reset(::openat(dir.Get(), parts[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (dir.Get() < 0)
        {
            return errno == ENOENT ? Result<bool>(false)
                                   : Result<bool>(fail("a directory is a link or not a directory"));
        }
    }
    const char* leaf = parts.back().c_str();
    struct stat info{};
    if (::fstatat(dir.Get(), leaf, &info, AT_SYMLINK_NOFOLLOW) != 0)
    {
        return errno == ENOENT ? Result<bool>(false) : Result<bool>(fail("inspecting the file"));
    }
    if (!S_ISREG(info.st_mode))
    {
        errno = EPERM;
        return fail("not a regular file");
    }
    // Should the leaf turn into a link after the check, unlinkat removes the link itself, never its target.
    if (::unlinkat(dir.Get(), leaf, 0) != 0)
    {
        return errno == ENOENT ? Result<bool>(false) : Result<bool>(fail("deleting"));
    }
    return true;
#endif
}

} // namespace interi
