// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelManifest.h>
#include <interi/assets/Material.h>
#include <interi/editor/MaterialPreview.h>
#include <interi/editor/Thumbnails.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <memory>

namespace interi::editor
{

namespace
{

constexpr std::size_t kMaxModelMeshes = 8;

Thumbnail Empty(std::uint32_t size)
{
    return {.size = size, .rgba = std::vector<std::uint8_t>(std::size_t{size} * size * 4, 0)};
}

/// Linear texel of a cooked texture's smallest level at least `target` wide, as RGBA floats (an sRGB texture is
/// decoded to linear light).
class TextureReader
{
public:
    TextureReader(const assets::TextureAsset& texture, std::uint32_t target)
        : m_srgb(texture.colorSpace == assets::ColorSpace::Srgb)
    {
        std::uint32_t mip = 0;
        while (mip + 1 < texture.mips.size() && std::max(1u, texture.width >> (mip + 1)) >= target &&
               std::max(1u, texture.height >> (mip + 1)) >= target)
        {
            ++mip;
        }
        if (auto decoded = assets::DecodeMip(texture, mip))
        {
            m_image = std::move(*decoded);
        }
    }

    [[nodiscard]] bool Ok() const noexcept { return m_image.width > 0; }
    [[nodiscard]] const assets::Image8& Image() const noexcept { return m_image; }

    [[nodiscard]] Vec4 Sample(float u, float v) const
    {
        if (!Ok())
        {
            return {1.0f, 1.0f, 1.0f, 1.0f};
        }
        const auto x = static_cast<std::uint32_t>(std::clamp(u - std::floor(u), 0.0f, 0.9999f) *
                                                  static_cast<float>(m_image.width));
        const auto y = static_cast<std::uint32_t>(std::clamp(v, 0.0f, 0.9999f) * static_cast<float>(m_image.height));
        const std::uint8_t* t = &m_image.texels[(std::size_t{y} * m_image.width + x) * 4];
        const auto channel = [&](int c)
        {
            const float value = static_cast<float>(t[c]) / 255.0f;
            if (!m_srgb || c == 3)
            {
                return value;
            }
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        };
        return {channel(0), channel(1), channel(2), channel(3)};
    }

private:
    bool m_srgb;
    assets::Image8 m_image;
};

Result<Thumbnail> MaterialThumbnail(const IFileSystem& files, const AssetEntry& entry, std::uint32_t size)
{
    assets::PbrMaterial material;
    if (entry.path.ends_with(".material.json"))
    {
        INTERI_TRY_ASSIGN(bytes, files.ReadFile(entry.path));
        INTERI_TRY_ASSIGN(
            parsed, assets::ParseMaterial(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
        material = std::move(parsed.material);
    }
    else
    {
        INTERI_TRY_ASSIGN(loaded, assets::LoadMaterialAsset(files, entry.path));
        material = std::move(loaded);
    }
    const TextureSampler sampler = MakeTextureSampler(files);
    const assets::Image8 image = RenderMaterialPreview(material, size, sampler);
    return Thumbnail{.size = size, .rgba = image.texels};
}

Result<Thumbnail> ModelThumbnail(const IFileSystem& files, const AssetEntry& entry, std::uint32_t size)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(entry.path));
    INTERI_TRY_ASSIGN(manifest, asset_import::ParseModelManifest(
                                    std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())));
    scene::TriangleMesh merged;
    for (std::size_t i = 0; i < manifest.meshes.size() && i < kMaxModelMeshes; ++i)
    {
        auto mesh = scene::LoadMeshAsset(files, manifest.meshes[i].mesh);
        if (!mesh)
        {
            continue;
        }
        const auto base = static_cast<std::uint32_t>(merged.positions.size());
        merged.positions.insert(merged.positions.end(), mesh->positions.begin(), mesh->positions.end());
        for (const std::uint32_t index : mesh->indices)
        {
            merged.indices.push_back(base + index);
        }
        merged.normals.insert(merged.normals.end(), mesh->normals.begin(), mesh->normals.end());
        merged.slots.insert(merged.slots.end(), mesh->slots.size(), 0);
    }
    if (merged.indices.empty())
    {
        return MakeError(ErrorCode::NotFound, std::format("{}: no readable mesh", entry.path));
    }
    return MeshThumbnail(merged, size);
}

} // namespace

TextureSampler MakeTextureSampler(const IFileSystem& files, std::string importedFolder)
{
    auto readers = std::make_shared<std::map<std::string, TextureReader>>();
    return [&files, readers, folder = std::move(importedFolder)](const assets::TextureRef& ref, float u, float v)
    {
        auto found = readers->find(ref.path);
        if (found == readers->end())
        {
            auto texture = assets::LoadTextureAsset(files, ref.path);
            if (!texture && !ref.path.ends_with(".itex"))
            {
                texture = assets::LoadTextureAsset(files, std::format("{}/{}.itex", folder, ref.path));
            }
            found = readers->emplace(ref.path, TextureReader(texture ? *texture : assets::TextureAsset{}, 16)).first;
        }
        return found->second.Sample(u, v);
    };
}

Thumbnail TextureThumbnail(const assets::TextureAsset& texture, std::uint32_t size)
{
    Thumbnail thumbnail = Empty(size);
    if (size == 0 || texture.width == 0 || texture.height == 0)
    {
        return thumbnail;
    }
    const TextureReader reader(texture, size);
    if (!reader.Ok())
    {
        return thumbnail;
    }
    const assets::Image8& image = reader.Image();
    const float scale = std::min(static_cast<float>(size) / static_cast<float>(image.width),
                                 static_cast<float>(size) / static_cast<float>(image.height));
    const auto width = std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(image.width) * scale)));
    const auto height = std::max(1u, static_cast<std::uint32_t>(std::lround(static_cast<float>(image.height) * scale)));
    const std::uint32_t left = (size - std::min(width, size)) / 2;
    const std::uint32_t top = (size - std::min(height, size)) / 2;
    for (std::uint32_t y = 0; y < std::min(height, size); ++y)
    {
        for (std::uint32_t x = 0; x < std::min(width, size); ++x)
        {
            const std::uint32_t sx =
                std::min(image.width - 1, static_cast<std::uint32_t>(static_cast<float>(x) / scale));
            const std::uint32_t sy =
                std::min(image.height - 1, static_cast<std::uint32_t>(static_cast<float>(y) / scale));
            const std::uint8_t* from = &image.texels[(std::size_t{sy} * image.width + sx) * 4];
            std::uint8_t* to = &thumbnail.rgba[(std::size_t{top + y} * size + left + x) * 4];
            std::copy(from, from + 4, to);
        }
    }
    return thumbnail;
}

Thumbnail MeshThumbnail(const scene::TriangleMesh& mesh, std::uint32_t size)
{
    Thumbnail thumbnail = Empty(size);
    if (size == 0 || mesh.indices.empty())
    {
        return thumbnail;
    }
    // Orthographic camera looking along -forward from a fixed direction.
    const Vec3 forward = Normalize(Vec3(-1.0f, -0.8f, -1.0f));
    const Vec3 right = Normalize(Cross(forward, Vec3(0.0f, 1.0f, 0.0f)));
    const Vec3 up = Cross(right, forward);
    const scene::MeshBounds bounds = scene::BoundsOf(mesh);
    const Vec3 center = (bounds.min + bounds.max) * 0.5f;
    float radius = 1e-6f;
    for (const Vec3& p : mesh.positions)
    {
        radius = std::max(radius, Length(p - center));
    }
    const float scale = static_cast<float>(size) * 0.46f / radius;
    const Vec3 toLight = Normalize(Vec3(-0.5f, 0.8f, 0.4f));
    std::vector<float> depth(std::size_t{size} * size, std::numeric_limits<float>::infinity());
    const auto project = [&](const Vec3& p)
    {
        const Vec3 d = p - center;
        return Vec3(static_cast<float>(size) * 0.5f + Dot(d, right) * scale,
                    static_cast<float>(size) * 0.5f - Dot(d, up) * scale, Dot(d, forward));
    };
    for (std::size_t t = 0; t < mesh.TriangleCount(); ++t)
    {
        const std::array<Vec3, 3> s = {project(mesh.positions[mesh.indices[3 * t]]),
                                       project(mesh.positions[mesh.indices[3 * t + 1]]),
                                       project(mesh.positions[mesh.indices[3 * t + 2]])};
        const float area = (s[1].x - s[0].x) * (s[2].y - s[0].y) - (s[2].x - s[0].x) * (s[1].y - s[0].y);
        if (std::abs(area) < 1e-9f)
        {
            continue;
        }
        const float minX = std::floor(std::min({s[0].x, s[1].x, s[2].x}));
        const float maxX = std::ceil(std::max({s[0].x, s[1].x, s[2].x}));
        const float minY = std::floor(std::min({s[0].y, s[1].y, s[2].y}));
        const float maxY = std::ceil(std::max({s[0].y, s[1].y, s[2].y}));
        for (auto y = static_cast<std::int32_t>(std::max(minY, 0.0f));
             y <= static_cast<std::int32_t>(std::min(maxY, static_cast<float>(size) - 1.0f)); ++y)
        {
            for (auto x = static_cast<std::int32_t>(std::max(minX, 0.0f));
                 x <= static_cast<std::int32_t>(std::min(maxX, static_cast<float>(size) - 1.0f)); ++x)
            {
                const float fx = static_cast<float>(x) + 0.5f;
                const float fy = static_cast<float>(y) + 0.5f;
                const float w0 = ((s[1].x - fx) * (s[2].y - fy) - (s[2].x - fx) * (s[1].y - fy)) / area;
                const float w1 = ((s[2].x - fx) * (s[0].y - fy) - (s[0].x - fx) * (s[2].y - fy)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
                {
                    continue;
                }
                const float z = w0 * s[0].z + w1 * s[1].z + w2 * s[2].z;
                float& stored =
                    depth[std::size_t{static_cast<std::uint32_t>(y)} * size + static_cast<std::uint32_t>(x)];
                if (z >= stored)
                {
                    continue;
                }
                stored = z;
                const Vec3 n = NormalizeOrZero(mesh.normals[3 * t] * w0 + mesh.normals[3 * t + 1] * w1 +
                                               mesh.normals[3 * t + 2] * w2);
                // Two-sided: a back face is lit by the flipped normal.
                const float lit = std::abs(Dot(n, toLight));
                const float shade = std::clamp(0.25f + 0.75f * lit, 0.0f, 1.0f);
                std::uint8_t* out =
                    &thumbnail
                         .rgba[(std::size_t{static_cast<std::uint32_t>(y)} * size + static_cast<std::uint32_t>(x)) * 4];
                out[0] = static_cast<std::uint8_t>(std::lround(shade * 190.0f));
                out[1] = static_cast<std::uint8_t>(std::lround(shade * 205.0f));
                out[2] = static_cast<std::uint8_t>(std::lround(shade * 230.0f));
                out[3] = 255;
            }
        }
    }
    return thumbnail;
}

Result<Thumbnail> MakeThumbnail(const IFileSystem& files, const AssetEntry& entry, std::uint32_t size)
{
    switch (entry.kind)
    {
    case AssetKind::Texture:
    {
        INTERI_TRY_ASSIGN(texture, assets::LoadTextureAsset(files, entry.path));
        return TextureThumbnail(texture, size);
    }
    case AssetKind::Mesh:
    {
        INTERI_TRY_ASSIGN(mesh, scene::LoadMeshAsset(files, entry.path));
        return MeshThumbnail(mesh, size);
    }
    case AssetKind::Model:
        return ModelThumbnail(files, entry, size);
    case AssetKind::Material:
        return MaterialThumbnail(files, entry, size);
    case AssetKind::Scene:
    case AssetKind::Prefab:
    case AssetKind::Skeleton:
    case AssetKind::Clip:
        break;
    }
    return MakeError(ErrorCode::Unsupported,
                     std::format("{}: no thumbnail for a {}", entry.path, ToString(entry.kind)));
}

const Thumbnail* ThumbnailCache::Get(const IFileSystem& files, const AssetEntry& entry)
{
    auto found = m_entries.find(entry.path);
    if (found == m_entries.end())
    {
        auto made = MakeThumbnail(files, entry, m_size);
        found = m_entries.emplace(entry.path, made ? std::optional<Thumbnail>(std::move(*made)) : std::nullopt).first;
    }
    return found->second ? &*found->second : nullptr;
}

} // namespace interi::editor
