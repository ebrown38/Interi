// SPDX-License-Identifier: MIT
#include <interi/editor/AssetBrowser.h>

#include <algorithm>
#include <system_error>

namespace interi::editor
{

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
        if (extension == ".idest")
        {
            assets.push_back({.path = std::move(path), .kind = AssetKind::Destructible});
        }
        else if (extension == ".gltf" || extension == ".glb")
        {
            assets.push_back({.path = std::move(path), .kind = AssetKind::Source});
        }
        else if (extension == ".json" && path.starts_with("levels/"))
        {
            assets.push_back({.path = std::move(path), .kind = AssetKind::Level});
        }
    }
    std::ranges::sort(assets, {}, &AssetEntry::path);
    return assets;
}

} // namespace interi::editor
