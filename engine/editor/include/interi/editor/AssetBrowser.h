// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

enum class AssetKind : std::uint8_t
{
    Scene,    ///< A `.scene.json`.
    Mesh,     ///< A `.imesh` mesh asset: placed into scenes.
    Prefab,   ///< A `.prefab.json`: instanced into scenes.
    Model,    ///< A cooked model's `manifest.imodel.json`: placed into scenes as its meshes.
    Texture,  ///< A cooked `.itex`.
    Material, ///< A cooked `.imat`, or an authored `.material.json` (opens in the material editor).
    Skeleton, ///< A `.iskel`.
    Clip,     ///< An `.ianim` animation clip.
};
inline constexpr std::size_t kAssetKindCount = 8;

[[nodiscard]] std::string_view ToString(AssetKind kind) noexcept;
/// The kind named by `ToString` ("texture"), or nothing.
[[nodiscard]] std::optional<AssetKind> ParseAssetKind(std::string_view name) noexcept;
/// The kind of a file by its name, or nothing for files the browser doesn't list.
[[nodiscard]] std::optional<AssetKind> KindOfPath(std::string_view path) noexcept;
/// Whether the asset can be dragged into the viewport (kAssetPayload): meshes, prefabs and models.
[[nodiscard]] bool IsPlaceable(AssetKind kind) noexcept;

struct AssetEntry
{
    std::string path; ///< Virtual path under the root ('/' separators).
    AssetKind kind = AssetKind::Scene;
};

/// Name of the drag-and-drop payload that carries an AssetEntry::path (a null-terminated string).
inline constexpr const char* kAssetPayload = "INTERI_ASSET";

/// The assets under `root`, sorted by path. Other files are left out (modules list their own kinds); a root that
/// doesn't exist gives an empty list.
[[nodiscard]] std::vector<AssetEntry> ListAssets(const std::filesystem::path& root);

/// A search: every word must occur in the path (case-insensitive), and `kind:<name>` restricts the kind
/// ("crate kind:texture"). Unknown kind names match nothing.
struct AssetQuery
{
    std::vector<std::string> words; ///< Lower case.
    std::optional<AssetKind> kind;
    bool matchNothing = false; ///< An unknown `kind:`.
};

[[nodiscard]] AssetQuery ParseAssetQuery(std::string_view text);
[[nodiscard]] bool Matches(const AssetQuery& query, const AssetEntry& entry);
/// The entries that match, in their order.
[[nodiscard]] std::vector<AssetEntry> FilterAssets(std::span<const AssetEntry> assets, std::string_view query);

} // namespace interi::editor
