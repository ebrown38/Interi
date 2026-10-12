// SPDX-License-Identifier: MIT
#pragma once

#include <filesystem>

namespace interi::testing
{

/// Creates a unique, empty directory under the system temp directory and recursively deletes it
/// on destruction. Movable, not copyable; a moved-from instance owns nothing.
class ScopedTempDir
{
public:
    ScopedTempDir();
    ~ScopedTempDir();

    ScopedTempDir(const ScopedTempDir&) = delete;
    ScopedTempDir& operator=(const ScopedTempDir&) = delete;
    ScopedTempDir(ScopedTempDir&& other) noexcept;
    ScopedTempDir& operator=(ScopedTempDir&& other) noexcept;

    [[nodiscard]] const std::filesystem::path& Path() const noexcept { return m_path; }

private:
    void Remove() noexcept;

    std::filesystem::path m_path;
};

} // namespace interi::testing
