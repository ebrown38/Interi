// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

enum class AssetKind : std::uint8_t
{
    Destructible, ///< A cooked `.idest`: placed into levels.
    Source,       ///< A `.gltf` / `.glb` scene: cooked into a destructible (with its `.fracture.json`).
    Level,        ///< A legacy level `.json` under `levels/` (opened through the level importer).
    Scene,        ///< A `.scene.json`.
    Mesh,         ///< A `.imesh` mesh asset: placed into scenes.
};

[[nodiscard]] std::string_view ToString(AssetKind kind) noexcept;

struct AssetEntry
{
    std::string path; ///< Virtual path under the root ('/' separators).
    AssetKind kind = AssetKind::Destructible;
};

/// Name of the drag-and-drop payload that carries an AssetEntry::path (a null-terminated string).
inline constexpr const char* kAssetPayload = "INTERI_ASSET";

/// The assets under `root`, sorted by path. Other files (including `.fracture.json` sidecars) are left out; a root
/// that doesn't exist gives an empty list.
[[nodiscard]] std::vector<AssetEntry> ListAssets(const std::filesystem::path& root);

} // namespace interi::editor
