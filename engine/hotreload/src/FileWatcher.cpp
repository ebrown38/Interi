// SPDX-License-Identifier: MIT
#include <interi/hotreload/FileWatcher.h>

#include <algorithm>
#include <system_error>
#include <utility>

namespace interi::hotreload
{

void FileWatcher::Watch(std::filesystem::path path)
{
    m_roots.push_back(std::move(path));
    m_initialised = false;
}

bool FileWatcher::Poll()
{
    std::vector<Entry> state = Scan();
    const bool changed = m_initialised && state != m_state;
    m_state = std::move(state);
    m_initialised = true;
    return changed;
}

std::vector<FileWatcher::Entry> FileWatcher::Scan() const
{
    std::vector<Entry> entries;
    const auto add = [&entries](const std::filesystem::path& path)
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(path, error);
        if (error)
        {
            return; // removed between listing and reading
        }
        const std::filesystem::file_time_type time = std::filesystem::last_write_time(path, error);
        if (!error)
        {
            entries.push_back(Entry{.path = path, .size = size, .time = time});
        }
    };
    for (const std::filesystem::path& root : m_roots)
    {
        std::error_code error;
        if (std::filesystem::is_regular_file(root, error))
        {
            add(root);
            continue;
        }
        if (!std::filesystem::is_directory(root, error))
        {
            continue;
        }
        for (auto it = std::filesystem::recursive_directory_iterator(
                 root, std::filesystem::directory_options::skip_permission_denied, error);
             !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
        {
            if (it->is_regular_file(error))
            {
                add(it->path());
            }
        }
    }
    std::ranges::sort(entries, {}, &Entry::path);
    return entries;
}

} // namespace interi::hotreload
