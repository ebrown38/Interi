// SPDX-License-Identifier: MIT
#include "FuzzTargets.h"

#include <interi/asset_import/ModelImport.h>
#include <interi/asset_import/ModelManifest.h>
#include <interi/asset_import/TextureImport.h>
#include <interi/assets/Material.h>
#include <interi/assets/Skeleton.h>
#include <interi/assets/Texture.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Primitives.h>

#include "../ImportFixtures.h"

#include <format>

namespace interi::fuzz
{

namespace
{

using namespace interi::asset_import;

std::string_view AsText(std::span<const std::byte> data)
{
    return {reinterpret_cast<const char*>(data.data()), data.size()};
}

std::string CheckModel(const Result<ImportedModel>& model)
{
    if (!model)
    {
        return {};
    }
    for (const assets::PbrMaterial& material : model->materials)
    {
        if (!assets::ValidateMaterial(material))
        {
            return "imported material fails validation";
        }
    }
    for (const assets::Skeleton& skeleton : model->skeletons)
    {
        if (!assets::ValidateSkeleton(skeleton))
        {
            return "imported skeleton fails validation";
        }
    }
    for (const ImportedClip& clip : model->clips)
    {
        if (clip.skeleton < 0 || static_cast<std::size_t>(clip.skeleton) >= model->skeletons.size() ||
            !assets::ValidateClip(
                clip.clip,
                static_cast<std::uint32_t>(model->skeletons[static_cast<std::size_t>(clip.skeleton)].joints.size())))
        {
            return "imported clip fails validation";
        }
    }
    for (const ImportedMesh& mesh : model->meshes)
    {
        if (!scene::ValidateMesh(mesh.mesh))
        {
            return "imported mesh fails validation";
        }
        for (const std::int32_t slot : mesh.slotMaterials)
        {
            if (slot < -1 || slot >= static_cast<std::int32_t>(model->materials.size()))
            {
                return "slot material out of range";
            }
        }
        if (mesh.slotMaterials.size() != mesh.mesh.SlotCount())
        {
            return "slot count disagrees with the mesh";
        }
        if (mesh.skeleton >= 0)
        {
            if (static_cast<std::size_t>(mesh.skeleton) >= model->skeletons.size() ||
                mesh.skin.size() != mesh.mesh.indices.size() ||
                !assets::ValidateSkin({.skeleton = {}, .influences = mesh.skin},
                                      static_cast<std::uint32_t>(
                                          model->skeletons[static_cast<std::size_t>(mesh.skeleton)].joints.size())))
            {
                return "imported skin fails validation";
            }
        }
        else if (!mesh.skin.empty())
        {
            return "skin without a skeleton";
        }
        // The cooked form must round-trip.
        auto read = scene::ReadMeshAsset(scene::WriteMeshAsset(mesh.mesh));
        if (!read || !(*read == mesh.mesh))
        {
            return "imported mesh does not round-trip";
        }
    }
    return {};
}

} // namespace

std::string_view Name(Target target) noexcept
{
    static constexpr std::string_view kNames[] = {"gltf",
                                                  "obj",
                                                  "fbx",
                                                  "image",
                                                  "texture-import",
                                                  "mesh-asset",
                                                  "texture-asset",
                                                  "material-asset",
                                                  "material-json",
                                                  "skeleton-asset",
                                                  "clip-asset",
                                                  "skin-asset",
                                                  "model-manifest"};
    const auto index = static_cast<std::size_t>(target);
    return index < std::size(kNames) ? kNames[index] : "?";
}

std::string Run(Target target, std::span<const std::byte> data)
{
    const ModelImportSettings settings;
    MemoryFileSystem files;
    switch (target)
    {
    case Target::Gltf:
        return CheckModel(ImportGltfData(data, files, "models", settings));
    case Target::Obj:
        return CheckModel(ImportObjData(data, files, "models", settings));
    case Target::Fbx:
        return CheckModel(ImportFbxData(data, "models", settings));
    case Target::Image:
    {
        auto image = DecodeImage(data);
        if (image && !image->isHdr && image->ldr.texels.size() != std::size_t{image->ldr.width} * image->ldr.height * 4)
        {
            return "decoded image has the wrong size";
        }
        if (image && image->isHdr && image->hdr.texels.size() != std::size_t{image->hdr.width} * image->hdr.height * 4)
        {
            return "decoded HDR image has the wrong size";
        }
        return {};
    }
    case Target::TextureImport:
    {
        auto texture = ImportTexture(data, {.colorSpace = assets::ColorSpace::Srgb,
                                            .generateMips = true,
                                            .compression = assets::TextureCompression::Bc1});
        return texture && !assets::ValidateTexture(*texture) ? "imported texture fails validation" : std::string();
    }
    case Target::MeshAsset:
    {
        auto mesh = scene::ReadMeshAsset(data);
        if (!mesh)
        {
            return {};
        }
        auto again = scene::ReadMeshAsset(scene::WriteMeshAsset(*mesh));
        return again && *again == *mesh ? std::string() : "mesh asset does not round-trip";
    }
    case Target::TextureAsset:
    {
        auto texture = assets::ReadTextureAsset(data);
        if (!texture)
        {
            return {};
        }
        auto again = assets::ReadTextureAsset(assets::WriteTextureAsset(*texture));
        if (!again || !(*again == *texture))
        {
            return "texture asset does not round-trip";
        }
        for (std::uint32_t mip = 0; mip < texture->mips.size(); ++mip)
        {
            if (!assets::DecodeMip(*texture, mip))
            {
                return "a valid texture level does not decode";
            }
        }
        return {};
    }
    case Target::MaterialAsset:
    {
        auto material = assets::ReadMaterialAsset(data);
        if (!material)
        {
            return {};
        }
        auto again = assets::ReadMaterialAsset(assets::WriteMaterialAsset(*material));
        return again && *again == *material ? std::string() : "material asset does not round-trip";
    }
    case Target::MaterialJson:
    {
        auto parsed = assets::ParseMaterial(AsText(data));
        if (!parsed)
        {
            return {};
        }
        auto again = assets::ParseMaterial(assets::WriteMaterial(parsed->material));
        return again && again->material == parsed->material ? std::string() : "material JSON does not round-trip";
    }
    case Target::SkeletonAsset:
    {
        auto skeleton = assets::ReadSkeletonAsset(data);
        if (!skeleton)
        {
            return {};
        }
        auto again = assets::ReadSkeletonAsset(assets::WriteSkeletonAsset(*skeleton));
        return again && *again == *skeleton ? std::string() : "skeleton asset does not round-trip";
    }
    case Target::ClipAsset:
    {
        auto clip = assets::ReadClipAsset(data, 64);
        if (!clip)
        {
            return {};
        }
        auto again = assets::ReadClipAsset(assets::WriteClipAsset(*clip), 64);
        return again && *again == *clip ? std::string() : "clip asset does not round-trip";
    }
    case Target::SkinAsset:
    {
        auto skin = assets::ReadSkinAsset(data, 64);
        if (!skin)
        {
            return {};
        }
        auto again = assets::ReadSkinAsset(assets::WriteSkinAsset(*skin), 64);
        return again && *again == *skin ? std::string() : "skin asset does not round-trip";
    }
    case Target::ModelManifest:
    {
        auto manifest = ParseModelManifest(AsText(data));
        if (!manifest)
        {
            return {};
        }
        auto again = ParseModelManifest(WriteModelManifest(*manifest));
        return again && *again == *manifest ? std::string() : "model manifest does not round-trip";
    }
    case Target::Count:
        break;
    }
    return {};
}

std::vector<Seed> SeedCorpus()
{
    using namespace interi::testing_fixtures;
    std::vector<Seed> seeds;
    const auto add = [&](Target target, std::string name, std::vector<std::byte> data)
    { seeds.push_back({.target = target, .name = std::move(name), .data = std::move(data)}); };

    GltfOptions withMaterial;
    withMaterial.materials = nlohmann::json::array(
        {{{"name", "M"},
          {"pbrMetallicRoughness", {{"baseColorFactor", {1, 0.5, 0.25, 1}}, {"baseColorTexture", {{"index", 0}}}}}}});
    withMaterial.extraRoot = {{"images", {{{"uri", "a.png"}}}}, {"textures", {{{"source", 0}}}}};
    add(Target::Gltf, "cube.gltf", MakeCubeGltf(withMaterial).main);
    GltfOptions glb;
    glb.glb = true;
    add(Target::Gltf, "cube.glb", MakeCubeGltf(glb).main);
    add(Target::Obj, "cube.obj", Bytes(MakeCubeObj(true, true, "mtllib cube.mtl\nusemtl Red\n")));
    add(Target::Obj, "plain.obj", Bytes("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n"));
    add(Target::Fbx, "cube.fbx", Bytes(MakeCubeFbx()));
    std::vector<std::uint8_t> pixels;
    for (int i = 0; i < 16; ++i)
    {
        pixels.insert(pixels.end(), {static_cast<std::uint8_t>(i * 16), 80, 160, 255});
    }
    const std::vector<std::byte> png = MakePng(4, 4, pixels);
    add(Target::Image, "a.png", png);
    add(Target::Image, "a.tga", MakeTga(2, 2, {0, 0, 255, 0, 255, 0, 255, 0, 0, 1, 2, 3}));
    add(Target::Image, "a.hdr", MakeHdr(4, 2));
    add(Target::TextureImport, "a.png", png);
    add(Target::TextureImport, "a.hdr", MakeHdr(4, 2));

    scene::PrimitiveParams box;
    add(Target::MeshAsset, "box.imesh", scene::WriteMeshAsset(*scene::GeneratePrimitive(box)));
    assets::Image8 image{.width = 8, .height = 8, .texels = {}};
    for (int i = 0; i < 64; ++i)
    {
        image.texels.insert(image.texels.end(), {static_cast<std::uint8_t>(i * 4), 20, 200, 255});
    }
    add(Target::TextureAsset, "rgba.itex", assets::WriteTextureAsset(*assets::BuildTexture(image, {})));
    add(Target::TextureAsset, "bc1.itex",
        assets::WriteTextureAsset(*assets::BuildTexture(image, {.colorSpace = assets::ColorSpace::Srgb,
                                                                .generateMips = true,
                                                                .compression = assets::TextureCompression::Bc1})));
    add(Target::TextureAsset, "bc5.itex",
        assets::WriteTextureAsset(*assets::BuildTexture(image, {.colorSpace = assets::ColorSpace::Linear,
                                                                .generateMips = false,
                                                                .compression = assets::TextureCompression::Bc5})));
    assets::PbrMaterial material;
    material.name = "Painted";
    material.baseColorTexture.path = "imported/a.png.itex";
    add(Target::MaterialAsset, "m.imat", assets::WriteMaterialAsset(material));
    add(Target::MaterialJson, "m.material.json", Bytes(assets::WriteMaterial(material)));

    assets::Skeleton skeleton;
    skeleton.name = "rig";
    assets::Joint root;
    root.name = "root";
    assets::Joint child;
    child.name = "child";
    child.parent = 0;
    child.bind.translation = Vec3(0, 1, 0);
    skeleton.joints = {root, child};
    add(Target::SkeletonAsset, "rig.iskel", assets::WriteSkeletonAsset(skeleton));
    assets::AnimationClip clip;
    clip.name = "wave";
    clip.duration = 1.0f;
    assets::AnimationChannel channel;
    channel.joint = 1;
    channel.times = {0.0f, 1.0f};
    channel.values = {Vec4(0, 0, 0, 0), Vec4(0, 2, 0, 0)};
    clip.channels = {channel};
    add(Target::ClipAsset, "wave.ianim", assets::WriteClipAsset(clip));
    assets::SkinBinding skin;
    skin.skeleton = "rig.iskel";
    assets::SkinInfluence influence;
    influence.joints = {0, 1, 0, 0};
    influence.weights = {0.5f, 0.5f, 0.0f, 0.0f};
    skin.influences = {influence, influence, influence};
    add(Target::SkinAsset, "body.iskin", assets::WriteSkinAsset(skin));
    ModelManifest manifest;
    manifest.source = "m.fbx";
    manifest.meshes.push_back({.name = "Body",
                               .mesh = "imported/Body.imesh",
                               .skin = "imported/Body.iskin",
                               .materials = {"imported/Red.imat"}});
    manifest.materials = {"imported/Red.imat"};
    add(Target::ModelManifest, "manifest.imodel.json", Bytes(WriteModelManifest(manifest)));
    return seeds;
}

} // namespace interi::fuzz
