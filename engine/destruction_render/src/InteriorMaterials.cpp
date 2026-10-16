// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Hash.h>
#include <interi/destruction_render/InteriorMaterials.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>

#include <algorithm>
#include <array>
#include <bit>

namespace interi::destruction
{

namespace
{

using Rgb = std::array<std::int32_t, 3>;

/// Hash of a lattice point in 0..255.
std::int32_t Lattice(std::uint64_t seed, std::uint32_t x, std::uint32_t y)
{
    return static_cast<std::int32_t>(HashCombine(HashCombine(seed, x), y) & 0xFFu);
}

/// Tileable value noise in 0..255: lattice cells of `cell` texels (a power of two dividing size), smooth
/// (3t^2 - 2t^3) interpolation in 8-bit fixed point.
std::int32_t ValueNoise(std::uint64_t seed, std::uint32_t x, std::uint32_t y, std::uint32_t size, std::uint32_t cellX,
                        std::uint32_t cellY)
{
    const std::uint32_t cellsX = size / cellX;
    const std::uint32_t cellsY = size / cellY;
    const std::uint32_t x0 = x / cellX;
    const std::uint32_t y0 = y / cellY;
    const std::uint32_t x1 = (x0 + 1) % cellsX;
    const std::uint32_t y1 = (y0 + 1) % cellsY;
    const auto smooth = [](std::int32_t t) { return t * t * (768 - 2 * t) / 65536; }; // t, result in 0..256
    const std::int32_t tx = smooth(static_cast<std::int32_t>((x % cellX) * 256 / cellX));
    const std::int32_t ty = smooth(static_cast<std::int32_t>((y % cellY) * 256 / cellY));
    const std::int32_t top = Lattice(seed, x0, y0) * (256 - tx) + Lattice(seed, x1, y0) * tx;
    const std::int32_t bottom = Lattice(seed, x0, y1) * (256 - tx) + Lattice(seed, x1, y1) * tx;
    return (top * (256 - ty) + bottom * ty) / 65536;
}

/// Three octaves of value noise, 0..255.
std::int32_t Fbm(std::uint64_t seed, std::uint32_t x, std::uint32_t y, std::uint32_t size, std::uint32_t cell)
{
    return (ValueNoise(seed, x, y, size, cell, cell) * 4 + ValueNoise(seed + 1, x, y, size, cell / 2, cell / 2) * 2 +
            ValueNoise(seed + 2, x, y, size, cell / 4, cell / 4)) /
           7;
}

Rgb Lerp(const Rgb& a, const Rgb& b, std::int32_t t) // t in 0..255
{
    return {(a[0] * (255 - t) + b[0] * t) / 255, (a[1] * (255 - t) + b[1] * t) / 255,
            (a[2] * (255 - t) + b[2] * t) / 255};
}

Rgb Shade(const Rgb& color, std::int32_t amount) // amount in 0..255: 128 = unchanged
{
    return {color[0] * (128 + amount) / 256, color[1] * (128 + amount) / 256, color[2] * (128 + amount) / 256};
}

/// Aggregate stone covering (x, y), if any: one stone per 16-texel cell with a hashed centre, radius and colour.
bool Stone(std::uint64_t seed, std::uint32_t x, std::uint32_t y, std::uint32_t size, Rgb& color)
{
    constexpr std::int32_t kCell = 16;
    const auto cells = static_cast<std::int32_t>(size) / kCell;
    const auto px = static_cast<std::int32_t>(x);
    const auto py = static_cast<std::int32_t>(y);
    for (std::int32_t dy = -1; dy <= 1; ++dy)
    {
        for (std::int32_t dx = -1; dx <= 1; ++dx)
        {
            const std::int32_t cx = px / kCell + dx;
            const std::int32_t cy = py / kCell + dy;
            const auto wrappedX = static_cast<std::uint32_t>((cx + cells) % cells);
            const auto wrappedY = static_cast<std::uint32_t>((cy + cells) % cells);
            const std::uint64_t hash = HashCombine(HashCombine(seed ^ 0x5A5Au, wrappedX), wrappedY);
            if ((hash & 3u) == 0)
            {
                continue; // a quarter of the cells have no stone
            }
            const std::int32_t centreX = cx * kCell + static_cast<std::int32_t>((hash >> 8) % kCell);
            const std::int32_t centreY = cy * kCell + static_cast<std::int32_t>((hash >> 16) % kCell);
            const std::int32_t radius = 3 + static_cast<std::int32_t>((hash >> 24) % 6);
            const std::int32_t ex = px - centreX;
            const std::int32_t ey = (py - centreY) * 4 / 3; // slightly elongated
            if (ex * ex + ey * ey <= radius * radius)
            {
                const auto tone = static_cast<std::int32_t>((hash >> 32) & 0x3Fu);
                color = (hash >> 40) % 3 == 0 ? Rgb{120 + tone, 100 + tone, 80 + tone / 2} // brown gravel
                                              : Rgb{95 + tone, 95 + tone, 100 + tone};     // grey gravel
                return true;
            }
        }
    }
    return false;
}

Rgb Texel(InteriorPattern pattern, std::uint64_t seed, std::uint32_t x, std::uint32_t y, std::uint32_t size)
{
    switch (pattern)
    {
    case InteriorPattern::Concrete:
    {
        Rgb stone;
        if (Stone(seed, x, y, size, stone))
        {
            return Shade(stone, 96 + ValueNoise(seed + 7, x, y, size, 4, 4) / 4);
        }
        const std::int32_t n = Fbm(seed, x, y, size, 32);
        // Cement with dark pores where fine noise dips.
        const Rgb cement = Lerp({150, 147, 140}, {196, 192, 184}, n);
        return ValueNoise(seed + 9, x, y, size, 2, 2) < 24 ? Shade(cement, 60) : cement;
    }
    case InteriorPattern::Steel:
    {
        const std::int32_t rust = Fbm(seed, x, y, size, 32);
        const Rgb steel = Lerp({150, 152, 158}, {205, 208, 214}, ValueNoise(seed + 3, x, y, size, 64, 2));
        return rust > 150 ? Lerp(steel, {130, 62, 28}, std::min((rust - 150) * 3, 255)) : steel;
    }
    case InteriorPattern::Wood:
    {
        // Fibres along x: noise stretched 16:1, plus darker growth bands across y.
        const std::int32_t fibre = ValueNoise(seed, x, y, size, 64, 4);
        const std::int32_t band = ValueNoise(seed + 5, x, y, size, 128, 16);
        const Rgb wood = Lerp({150, 104, 60}, {214, 170, 116}, fibre);
        return band > 170 ? Shade(wood, 70) : wood;
    }
    case InteriorPattern::Glass:
        return Lerp({200, 222, 214}, {220, 236, 230}, ValueNoise(seed, x, y, size, 32, 32));
    case InteriorPattern::Metal:
        return Lerp({160, 162, 166}, {190, 192, 197}, ValueNoise(seed, x, y, size, 128, 1));
    }
    return {255, 0, 255};
}

struct InteriorLook
{
    InteriorPattern pattern;
    float metallic;
    float roughness;
    float textureScale; ///< Repeats per metre.
};

constexpr std::array<InteriorLook, kInteriorPatternCount> kLooks{{
    {InteriorPattern::Concrete, 0.0f, 0.95f, 1.0f},
    {InteriorPattern::Steel, 0.9f, 0.45f, 2.0f},
    {InteriorPattern::Wood, 0.0f, 0.8f, 1.5f},
    {InteriorPattern::Glass, 0.0f, 0.1f, 1.0f},
    {InteriorPattern::Metal, 0.8f, 0.5f, 2.0f},
}};

} // namespace

render::ImageRgba8 MakeInteriorTexture(InteriorPattern pattern, std::uint32_t size, std::uint64_t seed)
{
    INTERI_ASSERT(size >= 128 && std::has_single_bit(size), "interior textures are power-of-two sized, >= 128");
    render::ImageRgba8 image{.width = size, .height = size, .texels = {}};
    image.texels.resize(std::size_t{size} * size * 4);
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const Rgb color = Texel(pattern, seed, x, y, size);
            std::uint8_t* texel = image.At(x, y);
            for (std::size_t c = 0; c < 3; ++c)
            {
                texel[c] = static_cast<std::uint8_t>(std::clamp(color[c], 0, 255));
            }
            texel[3] = 255;
        }
    }
    return image;
}

Result<InteriorMaterials> CreateInteriorMaterials(rhi::IDevice& device, rhi::UploadQueue& uploads,
                                                  rhi::ICommandList& commands, std::uint32_t size)
{
    InteriorMaterials result;
    for (const InteriorLook& look : kLooks)
    {
        const std::vector<render::ImageRgba8> mips =
            render::GenerateMips(MakeInteriorTexture(look.pattern, size, static_cast<std::uint64_t>(look.pattern) + 1));
        Result<rhi::TextureHandle> texture =
            render::CreateTexture(device, uploads, commands, mips, rhi::Format::R8G8B8A8Srgb, "Interior material");
        if (!texture)
        {
            for (const rhi::TextureHandle created : result.textures)
            {
                device.DestroyTexture(created);
            }
            return std::unexpected(std::move(texture.error()));
        }
        result.textures.push_back(*texture);
        result.materials.push_back({.baseColor = {1.0f, 1.0f, 1.0f},
                                    .metallic = look.metallic,
                                    .roughness = look.roughness,
                                    .albedoTexture = device.BindlessIndex(*texture),
                                    .textureScale = look.textureScale});
    }
    return result;
}

void DestroyInteriorMaterials(rhi::ResourceManager& resources, InteriorMaterials& materials)
{
    for (const rhi::TextureHandle texture : materials.textures)
    {
        resources.DestroyTexture(texture);
    }
    materials = {};
}

} // namespace interi::destruction
