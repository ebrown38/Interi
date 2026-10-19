// SPDX-License-Identifier: MIT
#include <interi/editor/AssetBrowser.h>

#include <algorithm>
#include <system_error>

namespace interi::editor
{

std::string_view ToString(AssetKind kind) noexcept
{
    switch (kind)
    {
    case AssetKind::Scene:
        return "scene";
    case AssetKind::Mesh:
        return "mesh";
    case AssetKind::Prefab:
        return "prefab";
    }
    return "?";
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
        const std::string extension = relative.extension().string();
        if (extension == ".imesh")
        {
            assets.push_back({.path = std::move(path), .kind = AssetKind::Mesh});
        }
        else if (path.ends_with(".prefab.json"))
        {
            assets.push_back({.path = std::move(path), .kind = AssetKind::Prefab});
        }
        else if (path.ends_with(".scene.json"))
        {
            assets.push_back({.path = std::move(path), .kind = AssetKind::Scene});
        }
    }
    std::ranges::sort(assets, {}, &AssetEntry::path);
    return assets;
}

} // namespace interi::editor
