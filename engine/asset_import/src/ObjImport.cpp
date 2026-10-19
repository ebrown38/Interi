// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelImport.h>

#include "ImportUtil.h"
#include "ModelProcess.h"
#include <tiny_obj_loader.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>
#include <utility>

namespace interi::asset_import
{

namespace
{

/// Looks .mtl files up next to the .obj, through the file system (never outside it).
class MtlReader final : public tinyobj::MaterialReader
{
public:
    MtlReader(const IFileSystem& files, std::string directory)
        : m_files(files)
        , m_directory(std::move(directory))
    {
    }

    bool operator()(const std::string& matId, std::vector<tinyobj::material_t>* materials,
                    std::map<std::string, int>* matMap, std::string* warn, std::string* err) override
    {
        const auto path = ResolveReference(m_directory, matId);
        if (!path)
        {
            *warn += std::format("material library '{}': {}\n", matId, path.error().message);
            return false;
        }
        const auto bytes = m_files.ReadFile(*path);
        if (!bytes)
        {
            *warn += std::format("material library '{}' not found\n", *path);
            return false;
        }
        std::istringstream stream(std::string(reinterpret_cast<const char*>(bytes->data()), bytes->size()));
        tinyobj::LoadMtl(matMap, materials, &stream, warn, err);
        return true;
    }

private:
    const IFileSystem& m_files;
    std::string m_directory;
};

float Clamp01(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

float NonNegative(float v)
{
    return std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
}

void SetTexture(assets::TextureRef& out, const std::string& name, std::string_view directory,
                std::vector<std::string>& warnings)
{
    if (name.empty())
    {
        return;
    }
    auto path = ResolveReference(directory, name);
    if (path)
    {
        out.path = *path;
    }
    else
    {
        warnings.push_back(std::format("texture '{}': {}", name, path.error().message));
    }
}

assets::PbrMaterial ConvertMaterial(const tinyobj::material_t& source, std::string_view directory,
                                    std::vector<std::string>& warnings)
{
    assets::PbrMaterial material;
    material.name = source.name;
    const float alpha = Clamp01(source.dissolve);
    material.baseColor =
        Vec4(NonNegative(source.diffuse[0]), NonNegative(source.diffuse[1]), NonNegative(source.diffuse[2]), alpha);
    material.metallic = Clamp01(source.metallic);
    // Phong shininess to roughness when the file has no PBR extension.
    material.roughness = source.roughness > 0.0f ? Clamp01(source.roughness)
                                                 : Clamp01(std::sqrt(2.0f / (NonNegative(source.shininess) + 2.0f)));
    material.emissive =
        Vec3(NonNegative(source.emission[0]), NonNegative(source.emission[1]), NonNegative(source.emission[2]));
    material.alphaMode = alpha < 1.0f ? assets::AlphaMode::Blend : assets::AlphaMode::Opaque;
    SetTexture(material.baseColorTexture, source.diffuse_texname, directory, warnings);
    SetTexture(material.emissiveTexture, source.emissive_texname, directory, warnings);
    SetTexture(material.normalTexture, source.normal_texname.empty() ? source.bump_texname : source.normal_texname,
               directory, warnings);
    if (!source.roughness_texname.empty() || !source.metallic_texname.empty())
    {
        warnings.push_back(
            std::format("material '{}': separate roughness and metallic maps are not combined", source.name));
    }
    return material;
}

} // namespace

Result<ImportedModel> ImportObjData(std::span<const std::byte> data, const IFileSystem& files,
                                    std::string_view directory, const ModelImportSettings& settings)
{
    INTERI_TRY(ValidateSettings(settings));
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn;
    std::string err;
    std::istringstream stream(std::string(reinterpret_cast<const char*>(data.data()), data.size()));
    MtlReader reader(files, std::string(directory));
    const bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, &stream, &reader, true, false);
    if (!ok)
    {
        return MakeError(ErrorCode::Corrupt, std::format("OBJ: {}", err.empty() ? "parse failed" : err));
    }
    ImportedModel model;
    if (!warn.empty())
    {
        model.warnings.push_back(std::format("OBJ: {}", warn));
    }
    if (settings.importMaterials)
    {
        for (const tinyobj::material_t& material : materials)
        {
            model.materials.push_back(ConvertMaterial(material, directory, model.warnings));
        }
    }
    const std::size_t vertexCount = attrib.vertices.size() / 3;
    const std::size_t normalCount = attrib.normals.size() / 3;
    const std::size_t uvCount = attrib.texcoords.size() / 2;
    const Mat4 conversion = SourceToEngine(settings);
    std::size_t triangles = 0;
    for (const tinyobj::shape_t& shape : shapes)
    {
        const tinyobj::mesh_t& mesh = shape.mesh;
        if (mesh.indices.empty())
        {
            continue;
        }
        const std::size_t shapeTriangles = mesh.indices.size() / 3;
        if (triangles + shapeTriangles > kMaxImportTriangles)
        {
            return MakeError(ErrorCode::Corrupt, "OBJ: too many triangles");
        }
        triangles += shapeTriangles;
        CornerMesh corners;
        corners.name = shape.name.empty() ? "mesh" : shape.name;
        const bool allNormals =
            std::ranges::all_of(mesh.indices, [](const tinyobj::index_t& i) { return i.normal_index >= 0; });
        const bool allUvs =
            std::ranges::all_of(mesh.indices, [](const tinyobj::index_t& i) { return i.texcoord_index >= 0; });
        for (const tinyobj::index_t& index : mesh.indices)
        {
            if (index.vertex_index < 0 || std::cmp_greater_equal(index.vertex_index, vertexCount) ||
                (allNormals && std::cmp_greater_equal(index.normal_index, normalCount)) ||
                (allUvs && std::cmp_greater_equal(index.texcoord_index, uvCount)))
            {
                return MakeError(ErrorCode::Corrupt,
                                 std::format("OBJ: shape '{}' has an index out of range", corners.name));
            }
            const auto v = static_cast<std::size_t>(index.vertex_index);
            corners.positions.emplace_back(attrib.vertices[3 * v], attrib.vertices[3 * v + 1],
                                           attrib.vertices[3 * v + 2]);
            if (allNormals)
            {
                const auto n = static_cast<std::size_t>(index.normal_index);
                corners.normals.emplace_back(attrib.normals[3 * n], attrib.normals[3 * n + 1],
                                             attrib.normals[3 * n + 2]);
            }
            if (allUvs)
            {
                const auto t = static_cast<std::size_t>(index.texcoord_index);
                // OBJ's v grows upwards; ours grows downwards.
                corners.uvs.push_back({.u = attrib.texcoords[2 * t], .v = 1.0f - attrib.texcoords[2 * t + 1]});
            }
        }
        corners.materials.assign(shapeTriangles, -1);
        for (std::size_t t = 0; t < shapeTriangles && t < mesh.material_ids.size(); ++t)
        {
            const int id = mesh.material_ids[t];
            if (settings.importMaterials && id >= 0 && static_cast<std::size_t>(id) < model.materials.size())
            {
                corners.materials[t] = id;
            }
        }
        INTERI_TRY_ASSIGN(built, BuildMesh(std::move(corners), conversion, settings, model.warnings));
        model.meshes.push_back(std::move(built));
    }
    return model;
}

} // namespace interi::asset_import
