// SPDX-License-Identifier: MIT
#pragma once

// Image decoding (PNG, JPEG, TGA and Radiance HDR on stb_image) and the import of an image into a texture asset. Input
// is hostile: dimensions are capped (kMaxTextureDimension, and kMaxImagePixels in total) before any pixel is decoded.

#include <interi/assets/Texture.h>
#include <interi/core/Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace interi::asset_import
{

inline constexpr std::uint64_t kMaxImagePixels = std::uint64_t{64} * 1024 * 1024;

/// A decoded image: 8-bit RGBA, or float RGBA for .hdr files (`hdr`).
struct DecodedImage
{
    assets::Image8 ldr;
    assets::ImageF hdr;
    bool isHdr = false;
    bool hasAlpha = false; ///< The source had an alpha channel (LDR only).
};

/// Corrupt for data that isn't a supported image, or one that is too big.
[[nodiscard]] Result<DecodedImage> DecodeImage(std::span<const std::byte> data);

/// Whether `path` has an image extension (.png, .jpg, .jpeg, .tga, .hdr).
[[nodiscard]] bool IsImagePath(std::string_view path);

struct TextureImportSettings
{
    assets::ColorSpace colorSpace = assets::ColorSpace::Srgb; ///< LDR only; HDR images are linear.
    bool generateMips = true;
    assets::TextureCompression compression = assets::TextureCompression::None; ///< LDR only.
    bool flipY = false;                                                        ///< Mirror the rows.

    friend bool operator==(const TextureImportSettings&, const TextureImportSettings&) = default;
};

/// Decodes and builds the texture asset.
[[nodiscard]] Result<assets::TextureAsset> ImportTexture(std::span<const std::byte> data,
                                                         const TextureImportSettings& settings);

} // namespace interi::asset_import
