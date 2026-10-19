// SPDX-License-Identifier: MIT
#include <interi/asset_import/TextureImport.h>

#include "ImportUtil.h"

#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#define STBI_ONLY_HDR
#define STBI_MAX_DIMENSIONS 16384
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>

namespace interi::asset_import
{

namespace
{

struct StbiDeleter
{
    void operator()(void* pixels) const { stbi_image_free(pixels); }
};

template <typename T>
void FlipRows(std::vector<T>& texels, std::uint32_t width, std::uint32_t height)
{
    const std::size_t row = std::size_t{width} * 4;
    for (std::uint32_t y = 0; y < height / 2; ++y)
    {
        std::swap_ranges(texels.begin() + static_cast<std::ptrdiff_t>(y * row),
                         texels.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                         texels.begin() + static_cast<std::ptrdiff_t>((height - 1 - y) * row));
    }
}

} // namespace

bool IsImagePath(std::string_view path)
{
    const std::string extension = ExtensionOf(path);
    return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
           extension == ".hdr";
}

Result<DecodedImage> DecodeImage(std::span<const std::byte> data)
{
    if (data.empty() || data.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return MakeError(ErrorCode::Corrupt, "image: empty or too large");
    }
    const auto* bytes = reinterpret_cast<const stbi_uc*>(data.data());
    const int size = static_cast<int>(data.size());
    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info_from_memory(bytes, size, &width, &height, &channels) == 0 || width <= 0 || height <= 0)
    {
        return MakeError(ErrorCode::Corrupt, "image: not a PNG, JPEG, TGA or HDR file");
    }
    if (std::cmp_greater(width, assets::kMaxTextureDimension) ||
        std::cmp_greater(height, assets::kMaxTextureDimension) ||
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) > kMaxImagePixels)
    {
        return MakeError(ErrorCode::Corrupt, "image: dimensions too large");
    }
    const auto w = static_cast<std::uint32_t>(width);
    const auto h = static_cast<std::uint32_t>(height);
    const std::size_t count = std::size_t{w} * h * 4;
    DecodedImage image;
    if (stbi_is_hdr_from_memory(bytes, size) != 0)
    {
        std::unique_ptr<float, StbiDeleter> pixels(stbi_loadf_from_memory(bytes, size, &width, &height, &channels, 4));
        if (!pixels)
        {
            return MakeError(ErrorCode::Corrupt, "image: corrupt HDR data");
        }
        image.isHdr = true;
        image.hdr.width = w;
        image.hdr.height = h;
        image.hdr.texels.assign(pixels.get(), pixels.get() + count);
        for (float& v : image.hdr.texels)
        {
            if (!std::isfinite(v))
            {
                v = 0.0f;
            }
        }
        return image;
    }
    std::unique_ptr<stbi_uc, StbiDeleter> pixels(stbi_load_from_memory(bytes, size, &width, &height, &channels, 4));
    if (!pixels)
    {
        return MakeError(ErrorCode::Corrupt, "image: corrupt image data");
    }
    image.ldr.width = w;
    image.ldr.height = h;
    image.ldr.texels.assign(pixels.get(), pixels.get() + count);
    image.hasAlpha = channels == 2 || channels == 4;
    return image;
}

Result<assets::TextureAsset> ImportTexture(std::span<const std::byte> data, const TextureImportSettings& settings)
{
    INTERI_TRY_ASSIGN(image, DecodeImage(data));
    if (image.isHdr)
    {
        if (settings.flipY)
        {
            FlipRows(image.hdr.texels, image.hdr.width, image.hdr.height);
        }
        return assets::BuildTexture(image.hdr, settings.generateMips);
    }
    if (settings.flipY)
    {
        FlipRows(image.ldr.texels, image.ldr.width, image.ldr.height);
    }
    return assets::BuildTexture(image.ldr, {.colorSpace = settings.colorSpace,
                                            .generateMips = settings.generateMips,
                                            .compression = settings.compression});
}

} // namespace interi::asset_import
