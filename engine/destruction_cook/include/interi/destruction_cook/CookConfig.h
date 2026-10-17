// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/CollisionHulls.h>
#include <interi/destruction/Voronoi.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::destruction
{

/// A material volume by material name (resolved against the materials file when cooking).
struct MaterialVolumeSettings
{
    MaterialVolume volume; ///< Its `material` index is ignored; `material` below names it.
    std::string material;
};

/// How one source object is cooked.
struct ObjectCookSettings
{
    std::string material = "concrete";    ///< DestructionMaterial name.
    std::vector<SeedSettings> levels;     ///< Fracture levels (see HierarchySettings).
    std::optional<double> minPieceVolume; ///< m^3; default: the material's debrisMinSize cubed.
    bool anchor = false;                  ///< Static world geometry: not fractured, anchors what touches it.
    bool skip = false;                    ///< Not part of the destructible.
    /// Regions of other materials, in the object's (imported) space; chunks whose centroid is inside one take it.
    std::vector<MaterialVolumeSettings> materialVolumes;
};

/// The sidecar `<asset>.fracture.json` that tells the cooker how to cook a glTF scene:
///
///     {
///       "materialsFile": "materials.json",           // relative to the config file
///       "groundHeight": 0.0,                         // optional; leaves touching it are anchors
///       "defaults": { "material": "concrete", "levels": [ { "pattern": "uniform", "count": 8, "seed": 1 } ] },
///       "surfaceMaterials": { "WindowGlass": { "material": "glass" } }, // by glTF material of the first primitive
///       "objects": { "Ground": { "anchor": true }, "Beam": { "material": "steel" } }, // by node name
///       // An object entry may also hold "materialVolumes": [ { "shape": "box" | "sphere", "centre": [x, y, z],
///       //   "halfExtents": [x, y, z], "radius": r, "material": "steel" } ] (later volumes win).
///       "hull": { "maxConcavity": 0.15, "maxDepth": 4 },
///       "contact": { "distanceTolerance": 0.001, "normalTolerance": 0.999, "minArea": 0.0001 }
///     }
///
/// An object's settings start from "defaults", then fields from its glTF material's entry, then its node entry.
/// A level is { "pattern": "uniform" | "clustered" | "grid" | "painted", "count", "seed", "impactPoint": [x, y, z],
/// "clusterRadius", "gridCellSize": [x, y, z], "jitter", "strokes": [ { "centre": [x, y, z], "radius", "density" } ] }.
struct CookConfig
{
    std::string materialsFile;
    std::optional<float> groundHeight;
    ObjectCookSettings defaults;
    std::map<std::string, std::string, std::less<>> surfaceOverrides; ///< Raw JSON of each "surfaceMaterials" entry.
    std::map<std::string, std::string, std::less<>> objectOverrides;  ///< Raw JSON of each "objects" entry.
    HullSettings hull;
    ContactSettings contact;
    std::vector<std::string> warnings; ///< Unknown keys, etc.

    /// Settings for a node: defaults, then its glTF material's entry, then its node entry.
    [[nodiscard]] Result<ObjectCookSettings> SettingsFor(std::string_view nodeName,
                                                         std::string_view materialName) const;

    /// Replaces a node's entry with the complete settings (as the editor stores them).
    void SetObjectSettings(std::string_view nodeName, const ObjectCookSettings& settings);
};

/// The config used when an asset has no sidecar file: concrete, one level of 8 uniform pieces, no ground.
[[nodiscard]] CookConfig DefaultCookConfig();

/// Parses a sidecar config. Fails with InvalidArgument on malformed JSON or wrongly typed / out-of-range values.
[[nodiscard]] Result<CookConfig> ParseCookConfig(std::string_view json);

/// The config as sidecar JSON that ParseCookConfig reads back to the same settings (warnings aside).
[[nodiscard]] std::string WriteCookConfig(const CookConfig& config);
/// One object entry ("defaults" or an "objects" value) as a JSON object, every field written.
[[nodiscard]] std::string WriteObjectSettings(const ObjectCookSettings& settings);

} // namespace interi::destruction
