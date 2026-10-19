// SPDX-License-Identifier: MIT
#include <interi/platform/PrivateFiles.h>

#include <cerrno>
#include <cstdlib>
#include <format>
#include <string>
#include <system_error>

#if !defined(_WIN32)
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

} // namespace interi
