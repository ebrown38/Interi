// SPDX-License-Identifier: MIT
#include <interi/editor/AssetBrowser.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

namespace interi::editor
{

namespace
{

constexpr std::array<std::string_view, kAssetKindCount> kNames = {"scene",   "mesh",     "prefab",   "model",
                                                                  "texture", "material", "skeleton", "clip"};

std::string Lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return out;
}

} // namespace

std::string_view ToString(AssetKind kind) noexcept
{
    const auto index = static_cast<std::size_t>(kind);
    return index < kNames.size() ? kNames[index] : "?";
}

std::optional<AssetKind> ParseAssetKind(std::string_view name) noexcept
{
    const auto found = std::ranges::find(kNames, name);
    if (found == kNames.end())
    {
        return std::nullopt;
    }
    return static_cast<AssetKind>(found - kNames.begin());
}

std::optional<AssetKind> KindOfPath(std::string_view path) noexcept
{
    const auto ends = [&](std::string_view suffix) { return path.ends_with(suffix); };
    if (ends(".imesh"))
    {
        return AssetKind::Mesh;
    }
    if (ends(".prefab.json"))
    {
        return AssetKind::Prefab;
    }
    if (ends(".scene.json"))
    {
        return AssetKind::Scene;
    }
    if (ends(".imodel.json"))
    {
        return AssetKind::Model;
    }
    if (ends(".itex"))
    {
        return AssetKind::Texture;
    }
    if (ends(".imat") || ends(".material.json"))
    {
        return AssetKind::Material;
    }
    if (ends(".iskel"))
    {
        return AssetKind::Skeleton;
    }
    if (ends(".ianim"))
    {
        return AssetKind::Clip;
    }
    return std::nullopt;
}

bool IsPlaceable(AssetKind kind) noexcept
{
    return kind == AssetKind::Mesh || kind == AssetKind::Prefab || kind == AssetKind::Model;
}

std::vector<AssetEntry> ListAssets(const std::filesystem::path& root)
{
    std::vector<AssetEntry> assets;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(root, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (!it->is_regular_file(error))
        {
            continue;
        }
        const std::filesystem::path relative = std::filesystem::relative(it->path(), root, error);
        std::string path = relative.generic_string();
        if (const auto kind = KindOfPath(path))
        {
            assets.push_back({.path = std::move(path), .kind = *kind});
        }
    }
    std::ranges::sort(assets, {}, &AssetEntry::path);
    return assets;
}

AssetQuery ParseAssetQuery(std::string_view text)
{
    AssetQuery query;
    std::size_t position = 0;
    while (position < text.size())
    {
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) != 0)
        {
            ++position;
        }
        const std::size_t begin = position;
        while (position < text.size() && std::isspace(static_cast<unsigned char>(text[position])) == 0)
        {
            ++position;
        }
        if (begin == position)
        {
            break;
        }
        const std::string word = Lower(text.substr(begin, position - begin));
        if (word.starts_with("kind:"))
        {
            query.kind = ParseAssetKind(std::string_view(word).substr(5));
            query.matchNothing = !query.kind.has_value();
        }
        else
        {
            query.words.push_back(word);
        }
    }
    return query;
}

bool Matches(const AssetQuery& query, const AssetEntry& entry)
{
    if (query.matchNothing || (query.kind && *query.kind != entry.kind))
    {
        return false;
    }
    const std::string path = Lower(entry.path);
    return std::ranges::all_of(query.words,
                               [&](const std::string& word) { return path.find(word) != std::string::npos; });
}

std::vector<AssetEntry> FilterAssets(std::span<const AssetEntry> assets, std::string_view queryText)
{
    const AssetQuery query = ParseAssetQuery(queryText);
    std::vector<AssetEntry> result;
    std::ranges::copy_if(assets, std::back_inserter(result),
                         [&](const AssetEntry& entry) { return Matches(query, entry); });
    return result;
}

} // namespace interi::editor
