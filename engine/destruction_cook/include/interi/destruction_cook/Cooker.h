// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/destruction/DestructibleAsset.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

/// Bump when cooking changes in a way that alters output for the same inputs, so stale assets are recooked.
inline constexpr std::uint32_t kCookerVersion = 1;

struct CookRequest
{
    std::filesystem::path input;     ///< .gltf or .glb.
    std::filesystem::path output;    ///< Default: the input with extension .idest.
    std::filesystem::path config;    ///< Default: <input stem>.fracture.json next to the input, if it exists.
    std::filesystem::path materials; ///< Default: the config's materialsFile (relative to the config).
    bool force = false;              ///< Cook even if the output is up to date.
};

struct CookReport
{
    bool skipped = false; ///< The output was already cooked from identical inputs.
    std::uint64_t cookKey = 0;
    std::filesystem::path output;
    std::size_t chunkCount = 0;
    std::size_t bondCount = 0;
    std::size_t anchorCount = 0;
    std::vector<std::string> warnings;
};

/// Cooks a glTF scene into a `.idest` destructible:
/// 1. Imports the scene and reads the config and materials.
/// 2. Computes the cook key: a hash of the glTF and its buffers, the config and materials files, and the cooker and
///    format versions. If the existing output is a valid asset with the same key, stops (skipped).
/// 3. Fractures every object (hierarchy per object), builds bonds and anchors across objects, collision hulls and
///    meshoptimizer-optimized render meshes, and writes the output (via a temporary file and rename).
///
/// Fails with IoError, Corrupt (bad glTF), InvalidArgument (bad config, unknown material) or Degenerate.
[[nodiscard]] Result<CookReport> Cook(const CookRequest& request, JobSystem& jobs);

/// Reads and validates a cooked `.idest` file.
[[nodiscard]] Result<DestructibleAsset> LoadDestructibleAsset(const std::filesystem::path& path);

/// Reorders a render mesh for the GPU vertex cache (per submesh) and vertex fetch (meshoptimizer). The triangles
/// and submesh ranges are unchanged as sets.
void OptimizeRenderMesh(RenderMesh& mesh);

} // namespace interi::destruction
