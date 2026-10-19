// SPDX-License-Identifier: MIT
#pragma once

// Texture assets (.itex): a mip chain in a GPU-ready format. Magic ITEX, version, the XXH3 of the payload, then the
// payload (its own archive, ITXP). The CPU side here builds them from decoded images: mips (an exact integer box
// filter; sRGB data is averaged in linear light) and block compression (BC1, BC4, BC5), which the tests check by
// decoding.

#include <interi/core/Archive.h>
#include <interi/core/Result.h>
#include <interi/platform/FileSystem.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace interi::assets
{

enum class TextureFormat : std::uint8_t
{
    Rgba8,   ///< 4 bytes per texel.
    Rgba32F, ///< 16 bytes per texel (HDR sources).
    Bc1,     ///< 8 bytes per 4x4 block: colour, no alpha.
    Bc4,     ///< 8 bytes per block: one channel (roughness, height...).
    Bc5,     ///< 16 bytes per block: two channels (tangent-space normals: x, y).
};
inline constexpr std::uint8_t kTextureFormatCount = 5;

enum class ColorSpace : std::uint8_t
{
    Linear, ///< Data (normals, masks, HDR).
    Srgb,   ///< Colour; the GPU converts when sampling. Alpha is always linear.
};

/// Largest width or height accepted anywhere (decoding, mips, assets).
inline constexpr std::uint32_t kMaxTextureDimension = 16384;

/// 8-bit RGBA texels, rows top to bottom.
struct Image8
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> texels; ///< width * height * 4.
};

/// 32-bit float RGBA texels.
struct ImageF
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> texels; ///< width * height * 4.
};

/// Levels down to 1x1 for a base of this size.
[[nodiscard]] std::uint32_t MipCount(std::uint32_t width, std::uint32_t height) noexcept;
/// Size in bytes of one level of this size.
[[nodiscard]] std::size_t MipByteSize(TextureFormat format, std::uint32_t width, std::uint32_t height) noexcept;

/// The chain from `base` down to 1x1 (base first). Each texel is the average of a 2x2 block (edges clamp).
[[nodiscard]] std::vector<Image8> GenerateMips(const Image8& base, ColorSpace colorSpace);
[[nodiscard]] std::vector<ImageF> GenerateMips(const ImageF& base);

struct TextureAsset
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    TextureFormat format = TextureFormat::Rgba8;
    ColorSpace colorSpace = ColorSpace::Srgb;
    std::vector<std::vector<std::byte>> mips; ///< Level 0 first; level i is max(1, width >> i) wide.

    friend bool operator==(const TextureAsset&, const TextureAsset&) = default;
};

/// Sizes agree with the format and dimensions, at most kMaxTextureDimension, a full chain or a single level.
[[nodiscard]] Result<void> ValidateTexture(const TextureAsset& texture);

inline constexpr FourCC kTextureMagic = MakeFourCC("ITEX");
inline constexpr std::uint32_t kTextureVersion = 1;

[[nodiscard]] std::vector<std::byte> WriteTextureAsset(const TextureAsset& texture);
/// Corrupt for a bad hash or an invalid texture, VersionMismatch for a newer version.
[[nodiscard]] Result<TextureAsset> ReadTextureAsset(std::span<const std::byte> data);
[[nodiscard]] Result<TextureAsset> LoadTextureAsset(const IFileSystem& files, std::string_view path);

enum class TextureCompression : std::uint8_t
{
    None,
    Bc1, ///< Drops alpha.
    Bc4, ///< Keeps red.
    Bc5, ///< Keeps red and green.
};

struct TextureBuildSettings
{
    ColorSpace colorSpace = ColorSpace::Srgb;
    bool generateMips = true;
    TextureCompression compression = TextureCompression::None;
};

/// Mips and compression for a decoded image. InvalidArgument for an empty or oversized image or a texel array of the
/// wrong size.
[[nodiscard]] Result<TextureAsset> BuildTexture(const Image8& image, const TextureBuildSettings& settings);
/// An HDR texture (Rgba32F, linear).
[[nodiscard]] Result<TextureAsset> BuildTexture(const ImageF& image, bool generateMips);

/// Block encoders for one level. The output has 8 (BC1, BC4) or 16 (BC5) bytes per 4x4 block, rows of blocks top to
/// bottom; texels beyond the edge repeat the last row and column.
[[nodiscard]] std::vector<std::byte> EncodeBc1(const Image8& image);
[[nodiscard]] std::vector<std::byte> EncodeBc4(const Image8& image, unsigned channel);
[[nodiscard]] std::vector<std::byte> EncodeBc5(const Image8& image);

/// One level as RGBA8: Rgba8 as is, BC1 as colour with alpha 255, BC4 as (r, 0, 0, 255), BC5 as (r, g, 0, 255),
/// Rgba32F clamped to [0, 1]. Colour is not converted between spaces.
[[nodiscard]] Result<Image8> DecodeMip(const TextureAsset& texture, std::uint32_t mip);

} // namespace interi::assets
