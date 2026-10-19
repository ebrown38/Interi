// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelImport.h>

#include "ImportUtil.h"
#include "ModelProcess.h"
#include <cgltf.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <set>

namespace interi::asset_import
{

namespace
{

constexpr std::size_t kMaxAllocation = std::size_t{1} << 30;
constexpr std::size_t kMaxNodeDepth = 4096;

void* GuardedAlloc(void*, cgltf_size size)
{
    return size > kMaxAllocation ? nullptr : std::malloc(size);
}

void GuardedFree(void*, void* pointer)
{
    std::free(pointer);
}

cgltf_result ReadFromFileSystem(const cgltf_memory_options* memory, const cgltf_file_options* file, const char* path,
                                cgltf_size* size, void** data)
{
    const auto* files = static_cast<const IFileSystem*>(file->user_data);
    const auto normalized = NormalizeVirtualPath(path);
    if (!normalized)
    {
        return cgltf_result_file_not_found;
    }
    const auto bytes = files->ReadFile(*normalized);
    if (!bytes)
    {
        return cgltf_result_file_not_found;
    }
    if (bytes->size() < *size)
    {
        return cgltf_result_data_too_short;
    }
    // The buffer is as long as the glTF says; extra bytes in the file are ignored.
    void* copy = memory->alloc_func(memory->user_data, *size == 0 ? 1 : *size);
    if (copy == nullptr)
    {
        return cgltf_result_out_of_memory;
    }
    std::memcpy(copy, bytes->data(), *size);
    *data = copy;
    return cgltf_result_success;
}

void ReleaseFromFileSystem(const cgltf_memory_options* memory, const cgltf_file_options*, void* data)
{
    memory->free_func(memory->user_data, data);
}

struct GltfDeleter
{
    void operator()(cgltf_data* data) const { cgltf_free(data); }
};
using GltfPtr = std::unique_ptr<cgltf_data, GltfDeleter>;

std::string Name(const char* name, std::string_view fallback)
{
    return name != nullptr && *name != '\0' ? std::string(name) : std::string(fallback);
}

Mat4 ToMat4(const cgltf_float* m)
{
    Mat4 result;
    for (int c = 0; c < 4; ++c)
    {
        result.columns[c] = Vec4(m[4 * c], m[4 * c + 1], m[4 * c + 2], m[4 * c + 3]);
    }
    return result;
}

Mat4 WorldMatrix(const cgltf_node* node)
{
    cgltf_float m[16];
    cgltf_node_transform_world(node, m);
    return ToMat4(m);
}

float Clamp01(float v)
{
    return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;
}

float NonNegative(float v)
{
    return std::isfinite(v) ? std::max(v, 0.0f) : 0.0f;
}

class GltfImporter
{
public:
    GltfImporter(const cgltf_data& gltf, std::string directory, const ModelImportSettings& settings,
                 ImportedModel& model)
        : m_gltf(gltf)
        , m_directory(std::move(directory))
        , m_settings(settings)
        , m_model(model)
        , m_conversion(SourceToEngine(settings))
    {
    }

    Result<void> Run()
    {
        if (m_settings.importMaterials)
        {
            ImportMaterials();
        }
        if (m_settings.importSkeletons)
        {
            INTERI_TRY(ImportSkins());
            if (m_settings.importAnimations)
            {
                INTERI_TRY(ImportAnimations());
            }
        }
        return ImportMeshes();
    }

private:
    [[nodiscard]] std::size_t NodeIndex(const cgltf_node* node) const
    {
        return static_cast<std::size_t>(node - m_gltf.nodes);
    }

    // --- Materials ---------------------------------------------------------------------------------------------

    std::string TextureReference(const cgltf_texture_view& view, bool& ok)
    {
        ok = false;
        if (view.texture == nullptr || view.texture->image == nullptr)
        {
            return {};
        }
        const cgltf_image* image = view.texture->image;
        const std::size_t index = static_cast<std::size_t>(image - m_gltf.images);
        if (const auto found = m_imageRefs.find(index); found != m_imageRefs.end())
        {
            ok = !found->second.empty();
            return found->second;
        }
        std::string reference;
        if (image->buffer_view != nullptr && image->buffer_view->buffer->data != nullptr)
        {
            const std::uint8_t* base = cgltf_buffer_view_data(image->buffer_view);
            EmbeddedImage embedded;
            embedded.name = Name(image->name, std::format("image{}", index));
            embedded.bytes.resize(image->buffer_view->size);
            std::memcpy(embedded.bytes.data(), base, embedded.bytes.size());
            m_model.images.push_back(std::move(embedded));
            reference = std::format("embedded:{}", m_model.images.size() - 1);
        }
        else if (image->uri != nullptr && std::strncmp(image->uri, "data:", 5) == 0)
        {
            const char* comma = std::strchr(image->uri, ',');
            const std::string_view header(image->uri,
                                          comma == nullptr ? 0 : static_cast<std::size_t>(comma - image->uri));
            auto bytes = comma != nullptr && header.ends_with(";base64") ? DecodeBase64(comma + 1) : std::nullopt;
            if (bytes)
            {
                m_model.images.push_back(
                    {.name = Name(image->name, std::format("image{}", index)), .bytes = std::move(*bytes)});
                reference = std::format("embedded:{}", m_model.images.size() - 1);
            }
            else
            {
                m_model.warnings.push_back(std::format("image {}: unreadable data URI", index));
            }
        }
        else if (image->uri != nullptr)
        {
            auto path = ResolveReference(m_directory, image->uri);
            if (path)
            {
                reference = *path;
            }
            else
            {
                m_model.warnings.push_back(std::format("image {}: {}", index, path.error().message));
            }
        }
        m_imageRefs[index] = reference;
        ok = !reference.empty();
        return reference;
    }

    void SetTexture(assets::TextureRef& out, const cgltf_texture_view& view)
    {
        bool ok = false;
        std::string reference = TextureReference(view, ok);
        if (ok)
        {
            out.path = std::move(reference);
            out.uvSet = view.texcoord < 0 ? 0u : static_cast<std::uint32_t>(view.texcoord);
        }
    }

    void ImportMaterials()
    {
        for (std::size_t i = 0; i < m_gltf.materials_count; ++i)
        {
            const cgltf_material& source = m_gltf.materials[i];
            assets::PbrMaterial material;
            material.name = Name(source.name, std::format("material{}", i));
            if (source.has_pbr_metallic_roughness)
            {
                const cgltf_pbr_metallic_roughness& pbr = source.pbr_metallic_roughness;
                material.baseColor = Vec4(NonNegative(pbr.base_color_factor[0]), NonNegative(pbr.base_color_factor[1]),
                                          NonNegative(pbr.base_color_factor[2]), Clamp01(pbr.base_color_factor[3]));
                material.metallic = Clamp01(pbr.metallic_factor);
                material.roughness = Clamp01(pbr.roughness_factor);
                SetTexture(material.baseColorTexture, pbr.base_color_texture);
                SetTexture(material.metallicRoughnessTexture, pbr.metallic_roughness_texture);
            }
            else if (source.has_pbr_specular_glossiness)
            {
                m_model.warnings.push_back(std::format("material '{}': specular-glossiness is not supported; using "
                                                       "the diffuse color",
                                                       material.name));
                const cgltf_pbr_specular_glossiness& sg = source.pbr_specular_glossiness;
                material.baseColor = Vec4(NonNegative(sg.diffuse_factor[0]), NonNegative(sg.diffuse_factor[1]),
                                          NonNegative(sg.diffuse_factor[2]), Clamp01(sg.diffuse_factor[3]));
                material.metallic = 0.0f;
                material.roughness = Clamp01(1.0f - sg.glossiness_factor);
                SetTexture(material.baseColorTexture, sg.diffuse_texture);
            }
            else
            {
                // glTF defaults when no model is given.
                material.metallic = 1.0f;
                material.roughness = 1.0f;
            }
            const float strength =
                source.has_emissive_strength ? NonNegative(source.emissive_strength.emissive_strength) : 1.0f;
            material.emissive = Vec3(NonNegative(source.emissive_factor[0]) * strength,
                                     NonNegative(source.emissive_factor[1]) * strength,
                                     NonNegative(source.emissive_factor[2]) * strength);
            SetTexture(material.emissiveTexture, source.emissive_texture);
            SetTexture(material.normalTexture, source.normal_texture);
            material.normalScale = std::isfinite(source.normal_texture.scale) ? source.normal_texture.scale : 1.0f;
            SetTexture(material.occlusionTexture, source.occlusion_texture);
            material.occlusionStrength = Clamp01(source.occlusion_texture.scale);
            switch (source.alpha_mode)
            {
            case cgltf_alpha_mode_mask:
                material.alphaMode = assets::AlphaMode::Mask;
                break;
            case cgltf_alpha_mode_blend:
                material.alphaMode = assets::AlphaMode::Blend;
                break;
            default:
                material.alphaMode = assets::AlphaMode::Opaque;
                break;
            }
            material.alphaCutoff = Clamp01(source.alpha_cutoff);
            material.doubleSided = source.double_sided != 0;
            m_model.materials.push_back(std::move(material));
        }
    }

    // --- Skins and animation -----------------------------------------------------------------------------------

    /// One skeleton per skin: its joints plus all their ancestors (so animated roots keep the transforms above them),
    /// parents first.
    Result<void> ImportSkins()
    {
        m_skinJoint.assign(m_gltf.skins_count, {});
        m_nodeJoints.assign(m_gltf.nodes_count, {});
        for (std::size_t s = 0; s < m_gltf.skins_count; ++s)
        {
            const cgltf_skin& skin = m_gltf.skins[s];
            if (skin.joints_count > assets::kMaxJoints)
            {
                return MakeError(ErrorCode::Corrupt, std::format("skin {}: {} joints (at most {})", s,
                                                                 skin.joints_count, assets::kMaxJoints));
            }
            // Joint nodes and their ancestors, ordered by depth then node index.
            std::set<const cgltf_node*> members;
            for (std::size_t j = 0; j < skin.joints_count; ++j)
            {
                std::size_t depth = 0;
                for (const cgltf_node* n = skin.joints[j]; n != nullptr && depth < kMaxNodeDepth;
                     n = n->parent, ++depth)
                {
                    members.insert(n);
                }
            }
            if (members.size() > assets::kMaxJoints)
            {
                return MakeError(ErrorCode::Corrupt, std::format("skin {}: too many joints with ancestors", s));
            }
            std::vector<std::pair<std::size_t, const cgltf_node*>> ordered; // depth, node
            for (const cgltf_node* node : members)
            {
                std::size_t depth = 0;
                for (const cgltf_node* n = node->parent; n != nullptr && depth < kMaxNodeDepth; n = n->parent)
                {
                    ++depth;
                }
                ordered.emplace_back(depth, node);
            }
            std::ranges::sort(
                ordered, [&](const auto& a, const auto& b)
                { return a.first != b.first ? a.first < b.first : NodeIndex(a.second) < NodeIndex(b.second); });
            std::map<const cgltf_node*, std::int32_t> index;
            assets::Skeleton skeleton;
            skeleton.name = Name(skin.name, std::format("skin{}", s));
            const auto skeletonIndex = static_cast<std::int32_t>(m_model.skeletons.size());
            for (std::size_t order = 0; order < ordered.size(); ++order)
            {
                const cgltf_node* node = ordered[order].second;
                index.emplace(node, static_cast<std::int32_t>(order));
                assets::Joint joint;
                joint.name = Name(node->name, std::format("node{}", NodeIndex(node)));
                joint.parent = node->parent != nullptr && index.contains(node->parent) ? index.at(node->parent) : -1;
                joint.bind = LocalTransform(*node);
                joint.inverseBind = Inverse(WorldMatrix(node));
                skeleton.joints.push_back(std::move(joint));
                m_nodeJoints[NodeIndex(node)].emplace_back(skeletonIndex, static_cast<std::int32_t>(order));
            }
            m_skinJoint[s].resize(skin.joints_count);
            for (std::size_t j = 0; j < skin.joints_count; ++j)
            {
                const std::int32_t joint = index.at(skin.joints[j]);
                m_skinJoint[s][j] = joint;
                cgltf_float m[16];
                if (skin.inverse_bind_matrices != nullptr && j < skin.inverse_bind_matrices->count &&
                    cgltf_accessor_read_float(skin.inverse_bind_matrices, j, m, 16))
                {
                    skeleton.joints[static_cast<std::size_t>(joint)].inverseBind = ToMat4(m);
                }
            }
            ConvertSkeleton(skeleton, m_conversion);
            if (auto valid = assets::ValidateSkeleton(skeleton); !valid)
            {
                return valid;
            }
            m_skinSkeleton.push_back(skeletonIndex);
            m_model.skeletons.push_back(std::move(skeleton));
        }
        return {};
    }

    static assets::JointTransform LocalTransform(const cgltf_node& node)
    {
        cgltf_float m[16];
        cgltf_node_transform_local(&node, m);
        if (node.has_matrix)
        {
            return Decompose(ToMat4(m));
        }
        assets::JointTransform t;
        if (node.has_translation)
        {
            t.translation = Vec3(node.translation[0], node.translation[1], node.translation[2]);
        }
        if (node.has_rotation)
        {
            t.rotation = Normalize(Quat{node.rotation[0], node.rotation[1], node.rotation[2], node.rotation[3]});
        }
        if (node.has_scale)
        {
            t.scale = Vec3(node.scale[0], node.scale[1], node.scale[2]);
        }
        return t;
    }

    Result<void> ImportAnimations()
    {
        for (std::size_t a = 0; a < m_gltf.animations_count; ++a)
        {
            const cgltf_animation& animation = m_gltf.animations[a];
            std::map<std::int32_t, assets::AnimationClip> clips; // by skeleton
            for (std::size_t c = 0; c < animation.channels_count; ++c)
            {
                const cgltf_animation_channel& channel = animation.channels[c];
                if (channel.target_node == nullptr || channel.sampler == nullptr)
                {
                    continue;
                }
                const auto& targets = m_nodeJoints[NodeIndex(channel.target_node)];
                if (targets.empty())
                {
                    m_model.warnings.push_back(
                        std::format("animation {}: channel {} targets a node that is not a joint", a, c));
                    continue;
                }
                assets::AnimationChannel out;
                std::size_t components = 3;
                switch (channel.target_path)
                {
                case cgltf_animation_path_type_translation:
                    out.path = assets::ChannelPath::Translation;
                    break;
                case cgltf_animation_path_type_rotation:
                    out.path = assets::ChannelPath::Rotation;
                    components = 4;
                    break;
                case cgltf_animation_path_type_scale:
                    out.path = assets::ChannelPath::Scale;
                    break;
                default:
                    m_model.warnings.push_back(std::format("animation {}: morph weight channels are not supported", a));
                    continue;
                }
                const cgltf_animation_sampler& sampler = *channel.sampler;
                const bool cubic = sampler.interpolation == cgltf_interpolation_type_cubic_spline;
                out.interpolation = sampler.interpolation == cgltf_interpolation_type_step
                                        ? assets::Interpolation::Step
                                        : assets::Interpolation::Linear;
                if (cubic)
                {
                    m_model.warnings.push_back(std::format("animation {}: cubic spline sampled as linear", a));
                }
                const std::size_t keys = sampler.input->count;
                const std::size_t perKey = cubic ? 3 : 1;
                if (keys == 0 || keys > kMaxImportItems || sampler.output->count != keys * perKey)
                {
                    return MakeError(ErrorCode::Corrupt,
                                     std::format("animation {}: channel {} has bad key counts", a, c));
                }
                out.times.resize(keys);
                if (cgltf_accessor_unpack_floats(sampler.input, out.times.data(), keys) != keys)
                {
                    return MakeError(ErrorCode::Corrupt, std::format("animation {}: unreadable key times", a));
                }
                std::vector<float> values(sampler.output->count * components);
                if (cgltf_accessor_unpack_floats(sampler.output, values.data(), values.size()) != values.size())
                {
                    return MakeError(ErrorCode::Corrupt, std::format("animation {}: unreadable key values", a));
                }
                out.values.resize(keys);
                for (std::size_t k = 0; k < keys; ++k)
                {
                    const float* v = &values[(k * perKey + (cubic ? 1 : 0)) * components];
                    if (components == 4)
                    {
                        const Quat q = Normalize(Quat{v[0], v[1], v[2], v[3]});
                        const bool usable = std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
                                            std::isfinite(q.w) &&
                                            std::abs(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w - 1.0f) < 1e-3f;
                        out.values[k] = usable ? Vec4(q.x, q.y, q.z, q.w) : Vec4(0.0f, 0.0f, 0.0f, 1.0f);
                    }
                    else
                    {
                        out.values[k] = Vec4(v[0], v[1], v[2], 0.0f);
                    }
                }
                for (const auto& [skeletonIndex, jointIndex] : targets)
                {
                    assets::AnimationClip& clip = clips[skeletonIndex];
                    for (const float t : out.times)
                    {
                        if (std::isfinite(t))
                        {
                            clip.duration = std::max(clip.duration, t);
                        }
                    }
                    clip.channels.push_back(out);
                    clip.channels.back().joint = static_cast<std::uint32_t>(jointIndex);
                }
            }
            for (auto& [skeletonIndex, clip] : clips)
            {
                clip.name = Name(animation.name, std::format("animation{}", a));
                if (clips.size() > 1)
                {
                    clip.name += std::format("#{}", m_model.skeletons[static_cast<std::size_t>(skeletonIndex)].name);
                }
                ConvertClip(clip, m_conversion);
                if (auto valid = assets::ValidateClip(
                        clip, static_cast<std::uint32_t>(
                                  m_model.skeletons[static_cast<std::size_t>(skeletonIndex)].joints.size()));
                    !valid)
                {
                    return MakeError(ErrorCode::Corrupt, std::format("animation {}: {}", a, valid.error().message));
                }
                m_model.clips.push_back({.clip = std::move(clip), .skeleton = skeletonIndex});
            }
        }
        return {};
    }

    // --- Meshes ------------------------------------------------------------------------------------------------

    void CollectReachable(const cgltf_node* node, std::set<const cgltf_node*>& seen, std::size_t depth) const
    {
        if (node == nullptr || depth > kMaxNodeDepth || !seen.insert(node).second)
        {
            return;
        }
        for (std::size_t i = 0; i < node->children_count; ++i)
        {
            CollectReachable(node->children[i], seen, depth + 1);
        }
    }

    Result<void> ImportMeshes()
    {
        std::set<const cgltf_node*> reachable;
        const cgltf_scene* scene =
            m_gltf.scene != nullptr ? m_gltf.scene : (m_gltf.scenes_count > 0 ? &m_gltf.scenes[0] : nullptr);
        if (scene != nullptr)
        {
            for (std::size_t i = 0; i < scene->nodes_count; ++i)
            {
                CollectReachable(scene->nodes[i], reachable, 0);
            }
        }
        else
        {
            for (std::size_t i = 0; i < m_gltf.nodes_count; ++i)
            {
                reachable.insert(&m_gltf.nodes[i]);
            }
        }
        for (std::size_t n = 0; n < m_gltf.nodes_count; ++n)
        {
            const cgltf_node& node = m_gltf.nodes[n];
            if (node.mesh == nullptr || !reachable.contains(&node))
            {
                continue;
            }
            const bool skinned = m_settings.importSkeletons && node.skin != nullptr;
            const Mat4 placement = skinned ? m_conversion : m_conversion * WorldMatrix(&node);
            for (std::size_t p = 0; p < node.mesh->primitives_count; ++p)
            {
                const std::string base = Name(node.name, Name(node.mesh->name, std::format("mesh{}", n)));
                const std::string name = node.mesh->primitives_count > 1 ? std::format("{}_{}", base, p) : base;
                INTERI_TRY(ImportPrimitive(node, node.mesh->primitives[p], name, placement, skinned));
            }
        }
        return {};
    }

    Result<void> ImportPrimitive(const cgltf_node& node, const cgltf_primitive& primitive, const std::string& name,
                                 const Mat4& placement, bool skinned)
    {
        if (primitive.type != cgltf_primitive_type_triangles && primitive.type != cgltf_primitive_type_triangle_strip &&
            primitive.type != cgltf_primitive_type_triangle_fan)
        {
            m_model.warnings.push_back(
                std::format("mesh '{}': skipped a primitive that is not made of triangles", name));
            return {};
        }
        if (primitive.has_draco_mesh_compression)
        {
            m_model.warnings.push_back(std::format("mesh '{}': Draco compression is not supported", name));
            return {};
        }
        if (primitive.targets_count > 0)
        {
            m_model.warnings.push_back(std::format("mesh '{}': morph targets are ignored", name));
        }
        const cgltf_accessor* position = nullptr;
        const cgltf_accessor* normal = nullptr;
        const cgltf_accessor* tangent = nullptr;
        const cgltf_accessor* uv = nullptr;
        const cgltf_accessor* joints = nullptr;
        const cgltf_accessor* weights = nullptr;
        for (std::size_t i = 0; i < primitive.attributes_count; ++i)
        {
            const cgltf_attribute& attribute = primitive.attributes[i];
            if (attribute.index != 0)
            {
                continue;
            }
            switch (attribute.type)
            {
            case cgltf_attribute_type_position:
                position = attribute.data;
                break;
            case cgltf_attribute_type_normal:
                normal = attribute.data;
                break;
            case cgltf_attribute_type_tangent:
                tangent = attribute.data;
                break;
            case cgltf_attribute_type_texcoord:
                uv = attribute.data;
                break;
            case cgltf_attribute_type_joints:
                joints = attribute.data;
                break;
            case cgltf_attribute_type_weights:
                weights = attribute.data;
                break;
            default:
                break;
            }
        }
        if (position == nullptr || position->count == 0)
        {
            return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': no POSITION", name));
        }
        const std::size_t vertexCount = position->count;
        if (vertexCount > kMaxImportItems * 8)
        {
            return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': too many vertices", name));
        }
        const auto unpack = [&](const cgltf_accessor* accessor, std::size_t components,
                                std::vector<float>& out) -> Result<bool>
        {
            if (accessor == nullptr)
            {
                return false;
            }
            if (accessor->count != vertexCount)
            {
                return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': attribute counts differ", name));
            }
            out.resize(vertexCount * components);
            if (cgltf_accessor_unpack_floats(accessor, out.data(), out.size()) != out.size())
            {
                return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': unreadable attribute", name));
            }
            return true;
        };
        std::vector<float> positions;
        std::vector<float> normals;
        std::vector<float> tangents;
        std::vector<float> uvs;
        INTERI_TRY(unpack(position, 3, positions));
        INTERI_TRY_ASSIGN(hasNormals, unpack(normal, 3, normals));
        INTERI_TRY_ASSIGN(hasTangents, unpack(tangent, 4, tangents));
        INTERI_TRY_ASSIGN(hasUvs, unpack(uv, 2, uvs));
        const bool hasSkin = skinned && joints != nullptr && weights != nullptr && node.skin != nullptr;
        std::vector<float> jointWeights;
        std::vector<cgltf_uint> jointIds;
        if (hasSkin)
        {
            INTERI_TRY(unpack(weights, 4, jointWeights));
            if (joints->count != vertexCount)
            {
                return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': attribute counts differ", name));
            }
            jointIds.resize(vertexCount * 4);
            for (std::size_t v = 0; v < vertexCount; ++v)
            {
                if (!cgltf_accessor_read_uint(joints, v, &jointIds[4 * v], 4))
                {
                    return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': unreadable joints", name));
                }
            }
        }
        // The vertex index of every corner.
        std::vector<std::uint32_t> order;
        const std::size_t indexCount = primitive.indices != nullptr ? primitive.indices->count : vertexCount;
        if (indexCount > kMaxImportItems * 24)
        {
            return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': too many indices", name));
        }
        std::vector<std::uint32_t> raw(indexCount);
        for (std::size_t i = 0; i < indexCount; ++i)
        {
            const std::size_t value =
                primitive.indices != nullptr ? cgltf_accessor_read_index(primitive.indices, i) : i;
            if (value >= vertexCount)
            {
                return MakeError(ErrorCode::Corrupt, std::format("mesh '{}': index out of range", name));
            }
            raw[i] = static_cast<std::uint32_t>(value);
        }
        if (primitive.type == cgltf_primitive_type_triangles)
        {
            raw.resize(raw.size() / 3 * 3);
            order = std::move(raw);
        }
        else if (primitive.type == cgltf_primitive_type_triangle_strip)
        {
            for (std::size_t i = 2; i < raw.size(); ++i)
            {
                const bool even = (i % 2) == 0;
                order.insert(order.end(), {raw[i - 2], even ? raw[i - 1] : raw[i], even ? raw[i] : raw[i - 1]});
            }
        }
        else
        {
            for (std::size_t i = 2; i < raw.size(); ++i)
            {
                order.insert(order.end(), {raw[0], raw[i - 1], raw[i]});
            }
        }
        if (order.size() / 3 + m_triangles > kMaxImportTriangles)
        {
            return MakeError(ErrorCode::Corrupt, "model has too many triangles");
        }
        m_triangles += order.size() / 3;

        CornerMesh corners;
        corners.name = name;
        corners.skeleton = hasSkin ? m_skinSkeleton[static_cast<std::size_t>(node.skin - m_gltf.skins)] : -1;
        const std::int32_t material = primitive.material != nullptr && m_settings.importMaterials
                                          ? static_cast<std::int32_t>(primitive.material - m_gltf.materials)
                                          : -1;
        corners.materials.assign(order.size() / 3, material);
        corners.positions.reserve(order.size());
        for (const std::uint32_t v : order)
        {
            corners.positions.emplace_back(positions[3 * v], positions[3 * v + 1], positions[3 * v + 2]);
            if (hasNormals)
            {
                corners.normals.emplace_back(normals[3 * v], normals[3 * v + 1], normals[3 * v + 2]);
            }
            if (hasUvs)
            {
                corners.uvs.push_back({.u = uvs[2 * v], .v = uvs[2 * v + 1]});
            }
            if (hasTangents)
            {
                corners.tangents.emplace_back(tangents[4 * v], tangents[4 * v + 1], tangents[4 * v + 2],
                                              tangents[4 * v + 3]);
            }
            if (hasSkin)
            {
                const auto& remap = m_skinJoint[static_cast<std::size_t>(node.skin - m_gltf.skins)];
                assets::SkinInfluence influence;
                float total = 0.0f;
                for (std::size_t k = 0; k < 4; ++k)
                {
                    const float w =
                        std::isfinite(jointWeights[4 * v + k]) ? std::max(jointWeights[4 * v + k], 0.0f) : 0.0f;
                    const cgltf_uint j = jointIds[4 * v + k];
                    if (w > 0.0f && j < remap.size())
                    {
                        influence.joints[k] = static_cast<std::uint16_t>(remap[j]);
                        influence.weights[k] = w;
                        total += w;
                    }
                }
                if (total > 0.0f)
                {
                    for (float& w : influence.weights)
                    {
                        w /= total;
                    }
                }
                corners.skin.push_back(influence);
            }
        }
        INTERI_TRY_ASSIGN(mesh, BuildMesh(std::move(corners), placement, m_settings, m_model.warnings));
        m_model.meshes.push_back(std::move(mesh));
        return {};
    }

    const cgltf_data& m_gltf;
    std::string m_directory;
    const ModelImportSettings& m_settings;
    ImportedModel& m_model;
    Mat4 m_conversion;
    std::map<std::size_t, std::string> m_imageRefs;
    std::vector<std::vector<std::int32_t>> m_skinJoint; ///< Per skin: skin joint index -> skeleton joint index.
    std::vector<std::vector<std::pair<std::int32_t, std::int32_t>>> m_nodeJoints; ///< Per node: (skeleton, joint)s.
    std::vector<std::int32_t> m_skinSkeleton; ///< Per skin: index into the model's skeletons.
    std::size_t m_triangles = 0;
};

} // namespace

Result<ImportedModel> ImportGltfData(std::span<const std::byte> data, const IFileSystem& files,
                                     std::string_view directory, const ModelImportSettings& settings)
{
    INTERI_TRY(ValidateSettings(settings));
    cgltf_options options{};
    options.memory.alloc_func = &GuardedAlloc;
    options.memory.free_func = &GuardedFree;
    options.file.read = &ReadFromFileSystem;
    options.file.release = &ReleaseFromFileSystem;
    options.file.user_data = const_cast<IFileSystem*>(&files); // only read through a const pointer
    cgltf_data* raw = nullptr;
    if (const cgltf_result result = cgltf_parse(&options, data.data(), data.size(), &raw);
        result != cgltf_result_success)
    {
        return MakeError(result == cgltf_result_out_of_memory ? ErrorCode::OutOfMemory : ErrorCode::Corrupt,
                         std::format("glTF: parse failed ({})", static_cast<int>(result)));
    }
    GltfPtr gltf(raw);
    if (gltf->nodes_count > kMaxImportItems || gltf->accessors_count > kMaxImportItems * 4 ||
        gltf->meshes_count > kMaxImportItems)
    {
        return MakeError(ErrorCode::Corrupt, "glTF: too many objects");
    }
    // A fake file name in `directory`: the buffer loader resolves relative URIs next to it.
    const std::string anchor = directory.empty() ? std::string("model.gltf") : std::string(directory) + "/model.gltf";
    if (const cgltf_result result = cgltf_load_buffers(&options, gltf.get(), anchor.c_str());
        result != cgltf_result_success)
    {
        return MakeError(result == cgltf_result_file_not_found ? ErrorCode::NotFound : ErrorCode::Corrupt,
                         std::format("glTF: loading buffers failed ({})", static_cast<int>(result)));
    }
    if (const cgltf_result result = cgltf_validate(gltf.get()); result != cgltf_result_success)
    {
        return MakeError(ErrorCode::Corrupt, std::format("glTF: validation failed ({})", static_cast<int>(result)));
    }
    ImportedModel model;
    GltfImporter importer(*gltf, std::string(directory), settings, model);
    INTERI_TRY(importer.Run());
    return model;
}

} // namespace interi::asset_import
