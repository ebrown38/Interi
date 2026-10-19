// SPDX-License-Identifier: MIT
#pragma once

// What a cooked model leaves behind: `manifest.imodel.json` lists the cooked meshes, which cooked material each mesh
// slot uses, the skin, skeleton and clips, so the editor (or a game) can place a model without re-reading the source.

#include <interi/core/Result.h>

#include <string>
#include <string_view>
#include <vector>

namespace interi::asset_import
{

struct ModelManifestMesh
{
    std::string name;
    std::string mesh;                   ///< Virtual path of the `.imesh`.
    std::string skin;                   ///< Virtual path of the `.iskin`, or empty.
    std::vector<std::string> materials; ///< Per mesh slot: virtual path of an `.imat`, or empty.

    friend bool operator==(const ModelManifestMesh&, const ModelManifestMesh&) = default;
};

struct ModelManifest
{
    std::string source; ///< Virtual path of the source file.
    std::vector<ModelManifestMesh> meshes;
    std::vector<std::string> materials;
    std::vector<std::string> textures;
    std::vector<std::string> skeletons;
    std::vector<std::string> clips;

    friend bool operator==(const ModelManifest&, const ModelManifest&) = default;
};

/// Canonical text (sorted keys); ParseModelManifest(WriteModelManifest(m)) gives `m` back.
[[nodiscard]] std::string WriteModelManifest(const ModelManifest& manifest);
/// InvalidArgument for anything malformed.
[[nodiscard]] Result<ModelManifest> ParseModelManifest(std::string_view json);

} // namespace interi::asset_import
