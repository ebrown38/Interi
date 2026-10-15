// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/destruction/FractureMesh.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace interi::destruction
{

/// One glTF node with a mesh, in world space (metres, +Y up, as glTF).
struct SourceObject
{
    std::string name; ///< Node name ("node<index>" if unnamed).
    /// All triangle primitives of the node's mesh, positions welded. `surfaceMaterial` is the glTF material index
    /// (or ImportedScene::surfaceMaterials.size() - 1 for primitives without one); `material` is 0.
    FractureMesh mesh;
    std::string firstMaterialName; ///< glTF material name of the first primitive ("" if none).
};

struct ImportedScene
{
    std::vector<SourceObject> objects;         ///< In glTF node order.
    std::vector<std::string> surfaceMaterials; ///< glTF material names, then "default".
    std::uint64_t contentHash = 0;             ///< XXH3 over the glTF file and every buffer it loads.
};

/// Loads a .gltf or .glb file with its buffers (files or data URIs). Only triangle primitives are imported; other
/// primitive modes are skipped. Fails with IoError (unreadable), Corrupt (malformed glTF) or Unsupported (no
/// triangle meshes).
[[nodiscard]] Result<ImportedScene> ImportGltf(const std::filesystem::path& path);

} // namespace interi::destruction
