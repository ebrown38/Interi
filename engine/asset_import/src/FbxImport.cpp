// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelImport.h>

#include "ImportUtil.h"
#include "ModelProcess.h"
#include <ufbx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <set>

namespace interi::asset_import
{

namespace
{

constexpr std::size_t kMemoryLimit = std::size_t{1} << 30;
constexpr std::size_t kMaxNodeDepth = 4096;

struct SceneDeleter
{
    void operator()(ufbx_scene* scene) const { ufbx_free_scene(scene); }
};
using ScenePtr = std::unique_ptr<ufbx_scene, SceneDeleter>;

struct BakedDeleter
{
    void operator()(ufbx_baked_anim* baked) const { ufbx_free_baked_anim(baked); }
};
using BakedPtr = std::unique_ptr<ufbx_baked_anim, BakedDeleter>;

std::string ToString(ufbx_string text, std::string_view fallback)
{
    return text.length > 0 ? std::string(text.data, text.length) : std::string(fallback);
}

Mat4 ToMat4(const ufbx_matrix& m)
{
    return Mat4{
        {{static_cast<float>(m.cols[0].x), static_cast<float>(m.cols[0].y), static_cast<float>(m.cols[0].z), 0.0f},
         {static_cast<float>(m.cols[1].x), static_cast<float>(m.cols[1].y), static_cast<float>(m.cols[1].z), 0.0f},
         {static_cast<float>(m.cols[2].x), static_cast<float>(m.cols[2].y), static_cast<float>(m.cols[2].z), 0.0f},
         {static_cast<float>(m.cols[3].x), static_cast<float>(m.cols[3].y), static_cast<float>(m.cols[3].z), 1.0f}}};
}

Vec3 ToVec3(const ufbx_vec3& v)
{
    return Vec3(static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z));
}

float Clamp01(double v)
{
    return std::isfinite(v) ? static_cast<float>(std::clamp(v, 0.0, 1.0)) : 0.0f;
}

float NonNegative(double v)
{
    return std::isfinite(v) ? static_cast<float>(std::max(v, 0.0)) : 0.0f;
}

std::size_t Depth(const ufbx_node* node)
{
    std::size_t depth = 0;
    for (const ufbx_node* n = node->parent; n != nullptr && depth < kMaxNodeDepth; n = n->parent)
    {
        ++depth;
    }
    return depth;
}

class FbxImporter
{
public:
    FbxImporter(const ufbx_scene& scene, std::string directory, const ModelImportSettings& settings,
                ImportedModel& model)
        : m_scene(scene)
        , m_directory(std::move(directory))
        , m_settings(settings)
        , m_model(model)
    {
        // FBX files declare their axes and unit and ufbx converted them already: only the scale applies.
        ModelImportSettings scaleOnly = settings;
        scaleOnly.up = UpAxis::Y;
        scaleOnly.handedness = Handedness::Right;
        m_conversion = SourceToEngine(scaleOnly);
    }

    Result<void> Run()
    {
        if (m_settings.importMaterials)
        {
            ImportMaterials();
        }
        if (m_settings.importSkeletons)
        {
            INTERI_TRY(ImportSkeleton());
            if (m_settings.importAnimations && !m_model.skeletons.empty())
            {
                INTERI_TRY(ImportAnimations());
            }
        }
        return ImportMeshes();
    }

private:
    // --- Materials ---------------------------------------------------------------------------------------------

    void SetTexture(assets::TextureRef& out, const ufbx_material_map& map)
    {
        const ufbx_texture* texture = map.texture;
        if (texture == nullptr || texture->type != UFBX_TEXTURE_FILE)
        {
            return;
        }
        std::string& cached = m_textureRefs[texture->element_id];
        if (cached.empty())
        {
            if (texture->content.size > 0)
            {
                EmbeddedImage image;
                image.name = ToString(texture->name, "texture");
                image.bytes.resize(texture->content.size);
                std::memcpy(image.bytes.data(), texture->content.data, image.bytes.size());
                m_model.images.push_back(std::move(image));
                cached = std::format("embedded:{}", m_model.images.size() - 1);
            }
            else
            {
                std::string name = ToString(texture->relative_filename, ToString(texture->filename, ""));
                std::ranges::replace(name, '\\', '/');
                auto path = ResolveReference(m_directory, name);
                if (!path)
                {
                    // An absolute path from the artist's machine: look for the file name next to the model.
                    const std::size_t slash = name.find_last_of('/');
                    path = ResolveReference(m_directory, slash == std::string::npos ? name : name.substr(slash + 1));
                }
                if (path)
                {
                    cached = *path;
                }
                else
                {
                    m_model.warnings.push_back(std::format("texture '{}': {}", name, path.error().message));
                    cached = "?";
                }
            }
        }
        if (cached != "?")
        {
            out.path = cached;
        }
    }

    std::int32_t MaterialIndex(const ufbx_material* material)
    {
        if (material == nullptr || !m_settings.importMaterials)
        {
            return -1;
        }
        return m_materialIndex.at(material->element_id);
    }

    void ImportMaterials()
    {
        for (const ufbx_material* source : m_scene.materials)
        {
            assets::PbrMaterial material;
            material.name = ToString(source->name, "material");
            const ufbx_material_pbr_maps& pbr = source->pbr;
            const double baseFactor = pbr.base_factor.has_value ? pbr.base_factor.value_real : 1.0;
            const double opacity = pbr.opacity.has_value ? pbr.opacity.value_real : 1.0;
            material.baseColor = Vec4(NonNegative(pbr.base_color.value_vec4.x * baseFactor),
                                      NonNegative(pbr.base_color.value_vec4.y * baseFactor),
                                      NonNegative(pbr.base_color.value_vec4.z * baseFactor),
                                      Clamp01(pbr.base_color.value_vec4.w * opacity));
            material.metallic = Clamp01(pbr.metalness.has_value ? pbr.metalness.value_real : 0.0);
            material.roughness = Clamp01(pbr.roughness.has_value ? pbr.roughness.value_real : 0.5);
            const double emissionFactor = pbr.emission_factor.has_value ? pbr.emission_factor.value_real : 1.0;
            if (pbr.emission_color.has_value)
            {
                material.emissive = Vec3(NonNegative(pbr.emission_color.value_vec3.x * emissionFactor),
                                         NonNegative(pbr.emission_color.value_vec3.y * emissionFactor),
                                         NonNegative(pbr.emission_color.value_vec3.z * emissionFactor));
            }
            material.alphaMode = material.baseColor.w < 1.0f ? assets::AlphaMode::Blend : assets::AlphaMode::Opaque;
            SetTexture(material.baseColorTexture, pbr.base_color);
            SetTexture(material.normalTexture, pbr.normal_map);
            SetTexture(material.occlusionTexture, pbr.ambient_occlusion);
            SetTexture(material.emissiveTexture, pbr.emission_color);
            m_materialIndex[source->element_id] = static_cast<std::int32_t>(m_model.materials.size());
            m_model.materials.push_back(std::move(material));
        }
    }

    // --- Skeleton ----------------------------------------------------------------------------------------------

    /// One skeleton for the scene: every bone a skin refers to, plus their ancestors, parents first.
    Result<void> ImportSkeleton()
    {
        std::set<const ufbx_node*> members;
        for (const ufbx_mesh* mesh : m_scene.meshes)
        {
            if (mesh->skin_deformers.count == 0)
            {
                continue;
            }
            for (const ufbx_skin_cluster* cluster : mesh->skin_deformers.data[0]->clusters)
            {
                std::size_t depth = 0;
                for (const ufbx_node* n = cluster->bone_node;
                     n != nullptr && n != m_scene.root_node && depth < kMaxNodeDepth; n = n->parent, ++depth)
                {
                    members.insert(n);
                }
            }
        }
        if (members.empty())
        {
            return {};
        }
        if (members.size() > assets::kMaxJoints)
        {
            return MakeError(ErrorCode::Corrupt,
                             std::format("FBX: {} joints (at most {})", members.size(), assets::kMaxJoints));
        }
        std::vector<const ufbx_node*> ordered(members.begin(), members.end());
        std::ranges::sort(ordered, [](const ufbx_node* a, const ufbx_node* b)
                          { return Depth(a) != Depth(b) ? Depth(a) < Depth(b) : a->element_id < b->element_id; });
        assets::Skeleton skeleton;
        skeleton.name = "skeleton";
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            const ufbx_node* node = ordered[i];
            m_jointOf[node->element_id] = static_cast<std::int32_t>(i);
            assets::Joint joint;
            joint.name = ToString(node->name, "joint");
            joint.parent = node->parent != nullptr && m_jointOf.contains(node->parent->element_id)
                               ? m_jointOf.at(node->parent->element_id)
                               : -1;
            const ufbx_transform& t = node->local_transform;
            joint.bind.translation = ToVec3(t.translation);
            joint.bind.rotation = Normalize(Quat{static_cast<float>(t.rotation.x), static_cast<float>(t.rotation.y),
                                                 static_cast<float>(t.rotation.z), static_cast<float>(t.rotation.w)});
            joint.bind.scale = ToVec3(t.scale);
            joint.inverseBind = Inverse(ToMat4(node->node_to_world));
            skeleton.joints.push_back(std::move(joint));
        }
        for (const ufbx_mesh* mesh : m_scene.meshes)
        {
            if (mesh->skin_deformers.count == 0)
            {
                continue;
            }
            for (const ufbx_skin_cluster* cluster : mesh->skin_deformers.data[0]->clusters)
            {
                if (cluster->bone_node != nullptr)
                {
                    skeleton.joints[static_cast<std::size_t>(m_jointOf.at(cluster->bone_node->element_id))]
                        .inverseBind = ToMat4(cluster->geometry_to_bone);
                }
            }
        }
        ConvertSkeleton(skeleton, m_conversion);
        INTERI_TRY(assets::ValidateSkeleton(skeleton));
        m_model.skeletons.push_back(std::move(skeleton));
        return {};
    }

    Result<void> ImportAnimations()
    {
        const auto jointCount = static_cast<std::uint32_t>(m_model.skeletons[0].joints.size());
        for (const ufbx_anim_stack* stack : m_scene.anim_stacks)
        {
            ufbx_bake_opts opts{};
            opts.trim_start_time = true;
            opts.temp_allocator.memory_limit = kMemoryLimit;
            opts.result_allocator.memory_limit = kMemoryLimit;
            ufbx_error error{};
            BakedPtr baked(ufbx_bake_anim(&m_scene, stack->anim, &opts, &error));
            if (!baked)
            {
                m_model.warnings.push_back(
                    std::format("animation '{}': baking failed", ToString(stack->name, "stack")));
                continue;
            }
            assets::AnimationClip clip;
            clip.name = ToString(stack->name, "animation");
            std::size_t keys = 0;
            for (const ufbx_baked_node& node : baked->nodes)
            {
                const auto joint = m_jointOf.find(node.element_id);
                if (joint == m_jointOf.end())
                {
                    continue;
                }
                keys += node.translation_keys.count + node.rotation_keys.count + node.scale_keys.count;
                if (keys > kMaxImportItems)
                {
                    return MakeError(ErrorCode::Corrupt, "FBX: too many animation keys");
                }
                const auto add = [&](assets::ChannelPath path, auto&& list, auto&& valueOf)
                {
                    if (list.count == 0)
                    {
                        return;
                    }
                    assets::AnimationChannel channel;
                    channel.joint = static_cast<std::uint32_t>(joint->second);
                    channel.path = path;
                    float previous = -1.0f;
                    for (const auto& key : list)
                    {
                        const auto time = static_cast<float>(std::max(key.time, 0.0));
                        if (!(time > previous))
                        {
                            continue; // times must increase strictly
                        }
                        previous = time;
                        channel.times.push_back(time);
                        channel.values.push_back(valueOf(key));
                        clip.duration = std::max(clip.duration, time);
                    }
                    if (!channel.times.empty())
                    {
                        clip.channels.push_back(std::move(channel));
                    }
                };
                add(assets::ChannelPath::Translation, node.translation_keys,
                    [](const ufbx_baked_vec3& k) { return Vec4(ToVec3(k.value), 0.0f); });
                add(assets::ChannelPath::Rotation, node.rotation_keys,
                    [](const ufbx_baked_quat& k)
                    {
                        const Quat q = Normalize(Quat{static_cast<float>(k.value.x), static_cast<float>(k.value.y),
                                                      static_cast<float>(k.value.z), static_cast<float>(k.value.w)});
                        return Vec4(q.x, q.y, q.z, q.w);
                    });
                add(assets::ChannelPath::Scale, node.scale_keys,
                    [](const ufbx_baked_vec3& k) { return Vec4(ToVec3(k.value), 0.0f); });
            }
            if (clip.channels.empty())
            {
                continue;
            }
            ConvertClip(clip, m_conversion);
            if (auto valid = assets::ValidateClip(clip, jointCount); !valid)
            {
                return MakeError(ErrorCode::Corrupt,
                                 std::format("FBX animation '{}': {}", clip.name, valid.error().message));
            }
            m_model.clips.push_back({.clip = std::move(clip), .skeleton = 0});
        }
        return {};
    }

    // --- Meshes ------------------------------------------------------------------------------------------------

    Result<void> ImportMeshes()
    {
        for (const ufbx_node* node : m_scene.nodes)
        {
            if (node->mesh == nullptr)
            {
                continue;
            }
            const ufbx_mesh& mesh = *node->mesh;
            const bool skinned =
                m_settings.importSkeletons && !m_model.skeletons.empty() && mesh.skin_deformers.count > 0;
            const Mat4 placement = skinned ? m_conversion : m_conversion * ToMat4(node->geometry_to_world);
            if (m_triangles + mesh.num_triangles > kMaxImportTriangles)
            {
                return MakeError(ErrorCode::Corrupt, "model has too many triangles");
            }
            CornerMesh corners;
            corners.name = ToString(node->name, ToString(mesh.name, "mesh"));
            corners.skeleton = skinned ? 0 : -1;
            const bool hasNormals = mesh.vertex_normal.exists;
            const bool hasUvs = mesh.vertex_uv.exists;
            const bool hasTangents = mesh.vertex_tangent.exists && mesh.vertex_bitangent.exists && hasNormals;
            const ufbx_skin_deformer* skin = skinned ? mesh.skin_deformers.data[0] : nullptr;
            std::vector<std::uint32_t> triangle(std::max<std::size_t>(mesh.max_face_triangles, 1) * 3);
            for (std::size_t f = 0; f < mesh.faces.count; ++f)
            {
                const ufbx_face face = mesh.faces.data[f];
                if (face.num_indices < 3)
                {
                    continue;
                }
                const std::uint32_t triangles = ufbx_triangulate_face(triangle.data(), triangle.size(), &mesh, face);
                const ufbx_material* material = nullptr;
                if (f < mesh.face_material.count && mesh.face_material.data[f] < mesh.materials.count)
                {
                    material = mesh.materials.data[mesh.face_material.data[f]];
                }
                for (std::uint32_t t = 0; t < triangles; ++t)
                {
                    corners.materials.push_back(MaterialIndex(material));
                    for (std::uint32_t k = 0; k < 3; ++k)
                    {
                        const std::uint32_t index = triangle[3 * t + k];
                        corners.positions.push_back(ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_position, index)));
                        if (hasNormals)
                        {
                            corners.normals.push_back(ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_normal, index)));
                        }
                        if (hasUvs)
                        {
                            const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
                            corners.uvs.push_back(
                                {.u = static_cast<float>(uv.x), .v = 1.0f - static_cast<float>(uv.y)});
                        }
                        if (hasTangents)
                        {
                            const Vec3 tangent = ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_tangent, index));
                            const Vec3 bitangent = ToVec3(ufbx_get_vertex_vec3(&mesh.vertex_bitangent, index));
                            const Vec3& normal = corners.normals.back();
                            // Our v grows downwards, FBX's upwards: the "image up" bitangent is FBX's.
                            corners.tangents.emplace_back(tangent,
                                                          Dot(Cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f);
                        }
                        if (skin != nullptr)
                        {
                            corners.skin.push_back(Influence(*skin, mesh.vertex_indices.data[index]));
                        }
                    }
                }
            }
            m_triangles += corners.materials.size();
            INTERI_TRY_ASSIGN(built, BuildMesh(std::move(corners), placement, m_settings, m_model.warnings));
            m_model.meshes.push_back(std::move(built));
        }
        return {};
    }

    assets::SkinInfluence Influence(const ufbx_skin_deformer& skin, std::uint32_t vertex) const
    {
        assets::SkinInfluence influence;
        if (vertex >= skin.vertices.count)
        {
            return influence;
        }
        const ufbx_skin_vertex v = skin.vertices.data[vertex];
        float total = 0.0f;
        std::size_t filled = 0;
        for (std::uint32_t w = 0; w < v.num_weights && filled < assets::kMaxSkinInfluences; ++w)
        {
            const ufbx_skin_weight weight = skin.weights.data[v.weight_begin + w];
            if (weight.cluster_index >= skin.clusters.count || !(weight.weight > 0.0))
            {
                continue;
            }
            const ufbx_node* bone = skin.clusters.data[weight.cluster_index]->bone_node;
            const auto joint = bone != nullptr ? m_jointOf.find(bone->element_id) : m_jointOf.end();
            if (joint == m_jointOf.end())
            {
                continue;
            }
            influence.joints[filled] = static_cast<std::uint16_t>(joint->second);
            influence.weights[filled] = static_cast<float>(weight.weight);
            total += influence.weights[filled];
            ++filled;
        }
        if (total > 0.0f)
        {
            for (float& w : influence.weights)
            {
                w /= total;
            }
        }
        return influence;
    }

    const ufbx_scene& m_scene;
    std::string m_directory;
    const ModelImportSettings& m_settings;
    ImportedModel& m_model;
    Mat4 m_conversion = Mat4::Identity();
    std::map<std::uint32_t, std::string> m_textureRefs;
    std::map<std::uint32_t, std::int32_t> m_materialIndex;
    std::map<std::uint32_t, std::int32_t> m_jointOf; ///< Node element id -> joint.
    std::size_t m_triangles = 0;
};

} // namespace

Result<ImportedModel> ImportFbxData(std::span<const std::byte> data, std::string_view directory,
                                    const ModelImportSettings& settings)
{
    INTERI_TRY(ValidateSettings(settings));
    ufbx_load_opts opts{};
    opts.temp_allocator.memory_limit = kMemoryLimit;
    opts.result_allocator.memory_limit = kMemoryLimit;
    opts.load_external_files = false; // the importer reads through the IFileSystem only
    opts.generate_missing_normals = false;
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0;
    opts.space_conversion = UFBX_SPACE_CONVERSION_ADJUST_TRANSFORMS;
    ufbx_error error{};
    ScenePtr scene(ufbx_load_memory(data.data(), data.size(), &opts, &error));
    if (!scene)
    {
        return MakeError(
            error.type == UFBX_ERROR_OUT_OF_MEMORY || error.type == UFBX_ERROR_MEMORY_LIMIT ? ErrorCode::OutOfMemory
                                                                                            : ErrorCode::Corrupt,
            std::format("FBX: {}", error.description.data != nullptr ? error.description.data : "load failed"));
    }
    if (scene->nodes.count > kMaxImportItems || scene->meshes.count > kMaxImportItems)
    {
        return MakeError(ErrorCode::Corrupt, "FBX: too many objects");
    }
    ImportedModel model;
    FbxImporter importer(*scene, std::string(directory), settings, model);
    INTERI_TRY(importer.Run());
    return model;
}

} // namespace interi::asset_import
