// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction_cook/CookConfig.h>
#include <interi/destruction_cook/Cooker.h>
#include <interi/destruction_cook/GltfImport.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::editor
{

/// Destructible assets being authored: each glTF source (imported once), its sidecar config and materials, and an
/// in-memory cook of the current settings (destruction::CookScene, exactly what Cook would write). Update recooks
/// only the assets whose settings changed since their last cook.
class FracturePreview
{
public:
    struct Entry
    {
        std::filesystem::path source; ///< The glTF ("" for entries added in memory).
        destruction::ImportedScene scene;
        destruction::CookConfig config; ///< Edit freely; Update notices the change.
        std::vector<destruction::DestructionMaterial> materials;
        std::shared_ptr<const destruction::DestructibleAsset> asset; ///< The last successful cook.
        std::string error;                                           ///< Why the last cook failed ("" if it succeeded).
        std::uint32_t revision = 0;                                  ///< Cooks so far (successful or not).
        std::uint64_t cookedKey = 0;
    };

    /// Opens a glTF with its `<stem>.fracture.json` (or the default config if there is none) and the materials
    /// file the config names (or `defaultMaterials`, which then becomes the config's materialsFile).
    [[nodiscard]] Result<void> Open(const std::string& key, const std::filesystem::path& gltf,
                                    const std::filesystem::path& defaultMaterials);
    /// Adds a source without files (tests, generated assets).
    void Add(const std::string& key, destruction::ImportedScene scene, destruction::CookConfig config,
             std::vector<destruction::DestructionMaterial> materials);

    [[nodiscard]] Entry* Find(std::string_view key) noexcept;
    [[nodiscard]] std::vector<std::string> Keys() const;

    /// Cooks every entry whose config changed (or that was never cooked). Returns how many were cooked.
    std::size_t Update(JobSystem& jobs);

    /// Writes the entry's sidecar next to its glTF and cooks it to `<stem>.idest` with destruction::Cook.
    [[nodiscard]] Result<destruction::CookReport> SaveAndCook(std::string_view key, JobSystem& jobs);

    /// The settings key Update compares: a hash of the config as written.
    [[nodiscard]] static std::uint64_t SettingsKey(const destruction::CookConfig& config);

private:
    std::map<std::string, Entry, std::less<>> m_entries;
};

/// The path of the sidecar config next to a glTF: `<stem>.fracture.json`.
[[nodiscard]] std::filesystem::path SidecarPath(const std::filesystem::path& gltf);

} // namespace interi::editor
