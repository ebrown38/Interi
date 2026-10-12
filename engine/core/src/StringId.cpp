// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/StringId.h>

#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace interi
{

namespace
{
/// hash -> name. Node-based, so the stored strings (and views into them) never move.
struct NameTable
{
    std::shared_mutex mutex;
    std::unordered_map<std::uint64_t, std::string> names;
};

NameTable& Names()
{
    static NameTable table;
    return table;
}
} // namespace

StringId::StringId(std::string_view name)
{
    if (name.empty())
    {
        return;
    }
    if constexpr (kStringIdNamesEnabled)
    {
        *this = detail::RegisterStringId(name, Hash64(name));
    }
    else
    {
        m_value = Hash64(name);
    }
}

std::string_view StringId::DebugName() const
{
    if constexpr (!kStringIdNamesEnabled)
    {
        return {};
    }
    NameTable& table = Names();
    const std::shared_lock lock(table.mutex);
    const auto it = table.names.find(m_value);
    return it != table.names.end() ? std::string_view(it->second) : std::string_view();
}

namespace detail
{
StringId RegisterStringId(std::string_view name, std::uint64_t hash)
{
    NameTable& table = Names();
    {
        const std::shared_lock lock(table.mutex);
        const auto it = table.names.find(hash);
        if (it != table.names.end())
        {
            INTERI_ASSERT(it->second == name, "StringId hash collision: two different names share a hash");
            return StringId::FromValue(hash);
        }
    }
    const std::unique_lock lock(table.mutex);
    const auto [it, inserted] = table.names.try_emplace(hash, name);
    INTERI_ASSERT(inserted || it->second == name, "StringId hash collision: two different names share a hash");
    return StringId::FromValue(hash);
}
} // namespace detail

} // namespace interi
