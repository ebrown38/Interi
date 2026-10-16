// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace interi::hotreload
{

/// Polls files and directories (recursively) for changes: a file added, removed, or with a new size or
/// modification time. Polling keeps it portable and deterministic to test; call Poll a few times per second.
class FileWatcher
{
public:
    /// Watches `path` (a file, or a directory and everything under it). It need not exist yet.
    void Watch(std::filesystem::path path);

    /// True if anything watched changed since the previous Poll (the first Poll after Watch records the state and
    /// reports no change).
    [[nodiscard]] bool Poll();

private:
    struct Entry
    {
        std::filesystem::path path;
        std::uintmax_t size;
        std::filesystem::file_time_type time;
        friend bool operator==(const Entry&, const Entry&) = default;
    };

    [[nodiscard]] std::vector<Entry> Scan() const;

    std::vector<std::filesystem::path> m_roots;
    std::vector<Entry> m_state;
    bool m_initialised = false;
};

} // namespace interi::hotreload
