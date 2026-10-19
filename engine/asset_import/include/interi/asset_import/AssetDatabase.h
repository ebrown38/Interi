// SPDX-License-Identifier: MIT
#pragma once

// The asset database of a project folder: every importable source file (models, images, `.material.json`) gets a
// `.meta` sidecar with its guid and import settings, and cooking turns sources into the formats of interi::assets under
// `imported/`.
//
// - A guid belongs to the .meta, so it survives a rename as long as the pair moves together. Sub-assets of a model
//   (meshes, materials, ...) get guids derived from the model's guid and their name, so they are stable too.
// - Cooking is incremental. The cook key hashes the source bytes, every file the import read (an external .bin or
// .mtl),
//   the settings, the guids of the textures it refers to and the importer and format versions. A source is skipped
//   when its key matches and every cooked file is still on disk with the hash the .meta recorded.
// - The dependency graph is what the last cook read and referenced; `Dependents` answers "what is affected if this file
//   changes".

#include <interi/asset_import/ModelImport.h>
#include <interi/asset_import/TextureImport.h>
#include <interi/assets/Guid.h>
#include <interi/core/Result.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace interi::asset_import
{

/// Bump to re-cook everything after an importer change.
inline constexpr std::uint32_t kImporterVersion = 1;

enum class SourceKind : std::uint8_t
{
    Model,
    Texture,
    Material,
};

[[nodiscard]] std::string_view ToString(SourceKind kind) noexcept;

/// A file the cook wrote.
struct CookedProduct
{
    std::string name; ///< Within the source: "Cube", "Brick", "rig"; the single product of a texture is "texture".
    std::string kind; ///< mesh, skin, material, texture, skeleton, clip or manifest.
    std::string path; ///< Virtual path under the imported folder.
    assets::Guid guid;
    std::uint64_t hash = 0; ///< Of the file's bytes.

    friend bool operator==(const CookedProduct&, const CookedProduct&) = default;
};

struct AssetRecord
{
    assets::Guid guid;
    std::string path; ///< Virtual path of the source ('/' separators).
    SourceKind kind = SourceKind::Model;
    ModelImportSettings model;     ///< Used when kind is Model.
    TextureImportSettings texture; ///< Used when kind is Texture.
    std::uint64_t cookKey = 0;     ///< 0 = never cooked.
    std::vector<CookedProduct> products;
    std::vector<std::string> reads;      ///< Files the import read besides the source (virtual paths).
    std::vector<std::string> references; ///< Source files whose cooked texture this asset points to.
};

struct CookReport
{
    std::vector<std::string> cooked;  ///< Sources imported this time.
    std::vector<std::string> skipped; ///< Up to date.
    std::vector<std::string> failed;  ///< "path: reason".
    std::vector<std::string> warnings;
};

class AssetDatabase
{
public:
    struct Options
    {
        std::filesystem::path root;
        std::string importedFolder = "imported"; ///< Under the root; not scanned for sources.
        assets::GuidGenerator guids;             ///< Empty = random.
    };

    explicit AssetDatabase(Options options);
    ~AssetDatabase();
    AssetDatabase(const AssetDatabase&) = delete;
    AssetDatabase& operator=(const AssetDatabase&) = delete;

    /// Finds the sources under the root, reads their .meta files and creates missing ones. A .meta that can't be read
    /// is replaced by a default one (with a warning); a guid used twice is regenerated for the later path. Sources that
    /// disappeared drop out of the database (their .meta and cooked files stay until Prune).
    Result<void> Scan();

    [[nodiscard]] const std::vector<AssetRecord>& Records() const noexcept;
    [[nodiscard]] const AssetRecord* Find(const assets::Guid& guid) const noexcept;
    [[nodiscard]] const AssetRecord* FindByPath(std::string_view path) const noexcept;
    /// The source whose last cook wrote the file at `path` (a cooked mesh, texture, ...), or nullptr.
    [[nodiscard]] const AssetRecord* FindByProduct(std::string_view path) const noexcept;
    /// Warnings from the last Scan.
    [[nodiscard]] const std::vector<std::string>& ScanWarnings() const noexcept;

    /// Replaces the import settings of a source (written to its .meta). The next cook re-imports it.
    Result<void> SetModelSettings(std::string_view path, const ModelImportSettings& settings);
    Result<void> SetTextureSettings(std::string_view path, const TextureImportSettings& settings);

    /// Cooks every source that is out of date.
    CookReport CookAll();
    /// Cooks one source; true if it was imported, false if it was up to date.
    Result<bool> Cook(std::string_view path);

    /// Sources that the last cook of `path` read or referenced.
    [[nodiscard]] std::vector<std::string> Dependencies(std::string_view path) const;
    /// Sources whose last cook read or referenced `path`; with `transitive` also theirs, and so on. Sorted.
    [[nodiscard]] std::vector<std::string> Dependents(std::string_view path, bool transitive = false) const;

    /// Deletes .meta files and cooked output whose source is gone; returns what was removed.
    std::vector<std::string> Prune();

    [[nodiscard]] const std::filesystem::path& Root() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Re-cooks when the sources change. Poll it a few times per second; it watches the root (the imported folder too, and
/// ignores the changes its own cook makes).
class AssetWatcher
{
public:
    explicit AssetWatcher(AssetDatabase& database);
    ~AssetWatcher();
    AssetWatcher(const AssetWatcher&) = delete;
    AssetWatcher& operator=(const AssetWatcher&) = delete;

    /// If anything under the root changed since the last call: Scan, then CookAll. Otherwise an empty report and
    /// `changed` false. The first call only records the state.
    struct Outcome
    {
        bool changed = false;
        CookReport report;
    };
    Outcome Update();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// The guid of a sub-asset: stable for a source guid, kind and name.
[[nodiscard]] assets::Guid DeriveGuid(const assets::Guid& parent, std::string_view kind, std::string_view name);

} // namespace interi::asset_import
