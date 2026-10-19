// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelManifest.h>

#include <nlohmann/json.hpp>

namespace interi::asset_import
{

namespace
{

using Json = nlohmann::json;

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, "model manifest: " + std::move(message));
}

Result<std::vector<std::string>> ReadStrings(const Json& root, const char* key)
{
    std::vector<std::string> out;
    const auto it = root.find(key);
    if (it == root.end())
    {
        return out;
    }
    if (!it->is_array())
    {
        return Invalid(std::string("\"") + key + "\" must be an array");
    }
    for (const Json& item : *it)
    {
        if (!item.is_string())
        {
            return Invalid(std::string("\"") + key + "\" must hold strings");
        }
        out.push_back(item.get<std::string>());
    }
    return out;
}

} // namespace

std::string WriteModelManifest(const ModelManifest& manifest)
{
    Json meshes = Json::array();
    for (const ModelManifestMesh& mesh : manifest.meshes)
    {
        meshes.push_back(
            {{"name", mesh.name}, {"mesh", mesh.mesh}, {"skin", mesh.skin}, {"materials", mesh.materials}});
    }
    const Json root = {{"version", 1},
                       {"source", manifest.source},
                       {"meshes", meshes},
                       {"materials", manifest.materials},
                       {"textures", manifest.textures},
                       {"skeletons", manifest.skeletons},
                       {"clips", manifest.clips}};
    return root.dump(2) + "\n";
}

Result<ModelManifest> ParseModelManifest(std::string_view text)
{
    const Json root = Json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("not a JSON object");
    }
    ModelManifest manifest;
    if (const auto it = root.find("source"); it != root.end() && it->is_string())
    {
        manifest.source = it->get<std::string>();
    }
    if (const auto it = root.find("meshes"); it != root.end())
    {
        if (!it->is_array())
        {
            return Invalid("\"meshes\" must be an array");
        }
        for (const Json& item : *it)
        {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string() || !item.contains("mesh") ||
                !item["mesh"].is_string())
            {
                return Invalid("a mesh needs \"name\" and \"mesh\" strings");
            }
            ModelManifestMesh mesh;
            mesh.name = item["name"].get<std::string>();
            mesh.mesh = item["mesh"].get<std::string>();
            if (const auto skin = item.find("skin"); skin != item.end() && skin->is_string())
            {
                mesh.skin = skin->get<std::string>();
            }
            INTERI_TRY_ASSIGN(materials, ReadStrings(item, "materials"));
            mesh.materials = std::move(materials);
            manifest.meshes.push_back(std::move(mesh));
        }
    }
    INTERI_TRY_ASSIGN(materials, ReadStrings(root, "materials"));
    INTERI_TRY_ASSIGN(textures, ReadStrings(root, "textures"));
    INTERI_TRY_ASSIGN(skeletons, ReadStrings(root, "skeletons"));
    INTERI_TRY_ASSIGN(clips, ReadStrings(root, "clips"));
    manifest.materials = std::move(materials);
    manifest.textures = std::move(textures);
    manifest.skeletons = std::move(skeletons);
    manifest.clips = std::move(clips);
    return manifest;
}

} // namespace interi::asset_import
