// SPDX-License-Identifier: MIT
#pragma once

// Model importers (glTF 2.0 on cgltf, Wavefront OBJ on tinyobjloader, FBX on ufbx) and the import settings. An import
// reads a source file and the files it refers to through an IFileSystem (never outside it: '..' is refused) and
// produces an ImportedModel in the engine's conventions: right-handed, +Y up, metres, counter-clockwise front faces.
// Every importer treats its input as hostile: bad data is an error, never an assert, and sizes are capped.

#include <interi/assets/Material.h>
#include <interi/assets/Skeleton.h>
#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi::asset_import
{

/// The most triangles, joints, materials and animation keys one import accepts (hostile files).
inline constexpr std::size_t kMaxImportTriangles = 8'000'000;
inline constexpr std::size_t kMaxImportItems = 1'000'000;

enum class UpAxis : std::uint8_t
{
    Y,
    Z,
};

enum class Handedness : std::uint8_t
{
    Right,
    Left,
};

enum class NormalMode : std::uint8_t
{
    FromFile, ///< The file's normals; smooth ones are computed when it has none.
    Smooth,   ///< Computed: faces within `smoothingAngleDegrees` of each other share a vertex normal.
    Flat,     ///< Computed: one normal per face.
};

/// How a source file is turned into engine data. The defaults suit a glTF file; OBJ files from tools that work in Z-up
/// or left-handed space set `up` and `handedness`. FBX files declare their own axes and units, so those two settings
/// (and `scale`'s unit part) don't apply to them: `scale` multiplies on top of metres.
struct ModelImportSettings
{
    float scale = 1.0f; ///< Uniform, on top of the source's unit; positive and finite.
    UpAxis up = UpAxis::Y;
    Handedness handedness = Handedness::Right;
    NormalMode normals = NormalMode::FromFile;
    float smoothingAngleDegrees = 60.0f; ///< In [0, 180].
    bool generateTangents = true;        ///< From the UVs, when there are any and the file has none.
    bool optimize = true;                ///< meshoptimizer: vertex cache and fetch order.
    bool importMaterials = true;
    bool importSkeletons = true;
    bool importAnimations = true;

    friend bool operator==(const ModelImportSettings&, const ModelImportSettings&) = default;
};

/// Scale positive and finite, smoothing angle in [0, 180].
[[nodiscard]] Result<void> ValidateSettings(const ModelImportSettings& settings);

/// One mesh with its transform baked in (a glTF primitive, an OBJ object, an FBX node's mesh).
struct ImportedMesh
{
    std::string name;
    scene::TriangleMesh mesh;
    std::vector<std::int32_t> slotMaterials; ///< Material index (into ImportedModel::materials) per slot, -1 for none.
    std::int32_t skeleton = -1;              ///< Into ImportedModel::skeletons, for a skinned mesh.
    std::vector<assets::SkinInfluence> skin; ///< One per mesh corner (index) when skinned.
};

/// An image stored inside the model file (GLB, data URI); materials refer to it as "embedded:<index>".
struct EmbeddedImage
{
    std::string name;
    std::vector<std::byte> bytes; ///< PNG or JPEG data.
};

struct ImportedClip
{
    assets::AnimationClip clip;
    std::int32_t skeleton = -1;
};

struct ImportedModel
{
    std::string name;
    std::vector<ImportedMesh> meshes;
    std::vector<assets::PbrMaterial> materials; ///< Texture paths are virtual paths, or "embedded:<index>".
    std::vector<EmbeddedImage> images;
    std::vector<assets::Skeleton> skeletons;
    std::vector<ImportedClip> clips;
    std::vector<std::string> warnings;
};

/// Whether `path` has a model extension (.gltf, .glb, .obj, .fbx).
[[nodiscard]] bool IsModelPath(std::string_view path);

/// Reads and imports by extension. NotFound / IoError for the file, Unsupported for the extension, Corrupt for bad
/// data.
[[nodiscard]] Result<ImportedModel> ImportModel(const IFileSystem& files, std::string_view path,
                                                const ModelImportSettings& settings);

/// From bytes already read; `directory` is the virtual folder external files (.bin, textures, .mtl) are looked up in.
[[nodiscard]] Result<ImportedModel> ImportGltfData(std::span<const std::byte> data, const IFileSystem& files,
                                                   std::string_view directory, const ModelImportSettings& settings);
[[nodiscard]] Result<ImportedModel> ImportObjData(std::span<const std::byte> data, const IFileSystem& files,
                                                  std::string_view directory, const ModelImportSettings& settings);
[[nodiscard]] Result<ImportedModel> ImportFbxData(std::span<const std::byte> data, std::string_view directory,
                                                  const ModelImportSettings& settings);

} // namespace interi::asset_import
