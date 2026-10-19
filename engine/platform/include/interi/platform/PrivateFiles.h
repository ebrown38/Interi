// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <filesystem>

namespace interi
{

/// A per-user directory for files other users must not be able to replace (loaded module copies): Linux
/// `$XDG_RUNTIME_DIR/interi`, else `$XDG_CACHE_HOME/interi`, else `~/.cache/interi`; Windows `%LOCALAPPDATA%\Interi`.
/// NotFound when the environment names no home.
[[nodiscard]] Result<std::filesystem::path> PerUserDataDirectory();

/// Creates `directory` (and missing parents; the directory itself with mode 0700 on POSIX) when absent, then checks
/// that it is a real directory (not a symlink) that the current user owns and that group and others cannot write to.
/// IoError (naming the check) when one fails. On Windows the per-user location's ACLs are relied on and only the
/// symlink check applies.
[[nodiscard]] Result<void> EnsurePrivateDirectory(const std::filesystem::path& directory);

/// Copies `from` to the new file `to` and fails if `to` already exists or is a symlink (POSIX: O_CREAT | O_EXCL |
/// O_NOFOLLOW, mode 0700 as the copy is an executable library).
[[nodiscard]] Result<void> CopyFileExclusive(const std::filesystem::path& from, const std::filesystem::path& to);

} // namespace interi
