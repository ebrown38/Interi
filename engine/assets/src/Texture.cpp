// SPDX-License-Identifier: MIT
#include <interi/assets/Texture.h>

#include "AssetIo.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <utility>

namespace interi::assets
{

namespace
{

constexpr FourCC kPayloadMagic = MakeFourCC("ITXP");

[[nodiscard]] std::size_t Blocks(std::uint32_t texels) noexcept
{
    return (std::size_t{texels} + 3) / 4;
}

[[nodiscard]] std::uint32_t HalfSize(std::uint32_t size) noexcept
{
    return std::max(1u, size >> 1);
}

[[nodiscard]] const std::array<float, 256>& SrgbToLinearTable()
{
    static const std::array<float, 256> table = []
    {
        std::array<float, 256> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
        {
            const double c = static_cast<double>(i) / 255.0;
            result[i] = static_cast<float>(c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4));
        }
        return result;
    }();
    return table;
}

[[nodiscard]] std::uint8_t LinearToSrgb8(float linear) noexcept
{
    const double c = std::clamp(static_cast<double>(linear), 0.0, 1.0);
    const double s = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(std::clamp(s, 0.0, 1.0) * 255.0));
}

[[nodiscard]] Image8 Halve(const Image8& source, ColorSpace colorSpace)
{
    Image8 result;
    result.width = HalfSize(source.width);
    result.height = HalfSize(source.height);
    result.texels.resize(std::size_t{result.width} * result.height * 4);
    const auto& table = SrgbToLinearTable();
    for (std::uint32_t y = 0; y < result.height; ++y)
    {
        for (std::uint32_t x = 0; x < result.width; ++x)
        {
            const std::uint32_t x0 = std::min(2 * x, source.width - 1);
            const std::uint32_t x1 = std::min(2 * x + 1, source.width - 1);
            const std::uint32_t y0 = std::min(2 * y, source.height - 1);
            const std::uint32_t y1 = std::min(2 * y + 1, source.height - 1);
            const std::array<const std::uint8_t*, 4> taps = {&source.texels[(std::size_t{y0} * source.width + x0) * 4],
                                                             &source.texels[(std::size_t{y0} * source.width + x1) * 4],
                                                             &source.texels[(std::size_t{y1} * source.width + x0) * 4],
                                                             &source.texels[(std::size_t{y1} * source.width + x1) * 4]};
            std::uint8_t* out = &result.texels[(std::size_t{y} * result.width + x) * 4];
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                if (colorSpace == ColorSpace::Srgb && channel < 3)
                {
                    float sum = 0.0f;
                    for (const std::uint8_t* tap : taps)
                    {
                        sum += table[tap[channel]];
                    }
                    out[channel] = LinearToSrgb8(sum * 0.25f);
                }
                else
                {
                    unsigned sum = 2; // rounds to nearest
                    for (const std::uint8_t* tap : taps)
                    {
                        sum += tap[channel];
                    }
                    out[channel] = static_cast<std::uint8_t>(sum >> 2);
                }
            }
        }
    }
    return result;
}

[[nodiscard]] ImageF Halve(const ImageF& source)
{
    ImageF result;
    result.width = HalfSize(source.width);
    result.height = HalfSize(source.height);
    result.texels.resize(std::size_t{result.width} * result.height * 4);
    for (std::uint32_t y = 0; y < result.height; ++y)
    {
        for (std::uint32_t x = 0; x < result.width; ++x)
        {
            const std::uint32_t x0 = std::min(2 * x, source.width - 1);
            const std::uint32_t x1 = std::min(2 * x + 1, source.width - 1);
            const std::uint32_t y0 = std::min(2 * y, source.height - 1);
            const std::uint32_t y1 = std::min(2 * y + 1, source.height - 1);
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const auto at = [&](std::uint32_t tx, std::uint32_t ty)
                { return source.texels[(std::size_t{ty} * source.width + tx) * 4 + channel]; };
                result.texels[(std::size_t{y} * result.width + x) * 4 + channel] =
                    (at(x0, y0) + at(x1, y0) + at(x0, y1) + at(x1, y1)) * 0.25f;
            }
        }
    }
    return result;
}

std::vector<std::byte> ToBytes(const std::vector<std::uint8_t>& texels)
{
    std::vector<std::byte> bytes(texels.size());
    std::ranges::transform(texels, bytes.begin(), [](std::uint8_t b) { return static_cast<std::byte>(b); });
    return bytes;
}

std::vector<std::byte> ToBytes(const std::vector<float>& texels)
{
    std::vector<std::byte> bytes(texels.size() * sizeof(float));
    std::memcpy(bytes.data(), texels.data(), bytes.size());
    return bytes;
}

// --- Block compression ----------------------------------------------------------------------------------------

struct Rgb
{
    int r = 0;
    int g = 0;
    int b = 0;
};

[[nodiscard]] const std::uint8_t* TexelClamped(const Image8& image, std::size_t x, std::size_t y) noexcept
{
    x = std::min(x, std::size_t{image.width} - 1);
    y = std::min(y, std::size_t{image.height} - 1);
    return &image.texels[(y * image.width + x) * 4];
}

[[nodiscard]] std::uint16_t Pack565(const Rgb& c) noexcept
{
    return static_cast<std::uint16_t>(((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3));
}

[[nodiscard]] Rgb Unpack565(std::uint16_t v) noexcept
{
    const int r = (v >> 11) & 31;
    const int g = (v >> 5) & 63;
    const int b = v & 31;
    return {(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)};
}

void Put16(std::vector<std::byte>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::byte>(v & 0xFF));
    out.push_back(static_cast<std::byte>(v >> 8));
}

[[nodiscard]] int DistanceSquared(const Rgb& a, const Rgb& b) noexcept
{
    return (a.r - b.r) * (a.r - b.r) + (a.g - b.g) * (a.g - b.g) + (a.b - b.b) * (a.b - b.b);
}

void EncodeBc1Block(const std::array<Rgb, 16>& texels, std::vector<std::byte>& out)
{
    // The endpoints are the texels with the smallest and largest projection on the principal axis of the colours
    // (a few power iterations on the covariance), so data that lies on a line is represented exactly.
    std::array<float, 3> mean{};
    for (const Rgb& t : texels)
    {
        mean[0] += static_cast<float>(t.r) / 16.0f;
        mean[1] += static_cast<float>(t.g) / 16.0f;
        mean[2] += static_cast<float>(t.b) / 16.0f;
    }
    std::array<float, 6> covariance{}; // xx, xy, xz, yy, yz, zz
    for (const Rgb& t : texels)
    {
        const float x = static_cast<float>(t.r) - mean[0];
        const float y = static_cast<float>(t.g) - mean[1];
        const float z = static_cast<float>(t.b) - mean[2];
        covariance[0] += x * x;
        covariance[1] += x * y;
        covariance[2] += x * z;
        covariance[3] += y * y;
        covariance[4] += y * z;
        covariance[5] += z * z;
    }
    std::array<float, 3> axis = {1.0f, 1.0f, 1.0f};
    for (int iteration = 0; iteration < 6; ++iteration)
    {
        const std::array<float, 3> next = {covariance[0] * axis[0] + covariance[1] * axis[1] + covariance[2] * axis[2],
                                           covariance[1] * axis[0] + covariance[3] * axis[1] + covariance[4] * axis[2],
                                           covariance[2] * axis[0] + covariance[4] * axis[1] + covariance[5] * axis[2]};
        const float length = std::max({std::abs(next[0]), std::abs(next[1]), std::abs(next[2])});
        if (length < 1e-6f)
        {
            break; // a solid block: any axis will do
        }
        axis = {next[0] / length, next[1] / length, next[2] / length};
    }
    const auto project = [&](const Rgb& t)
    {
        return static_cast<float>(t.r) * axis[0] + static_cast<float>(t.g) * axis[1] +
               static_cast<float>(t.b) * axis[2];
    };
    const auto [lowIt, highIt] = std::ranges::minmax_element(texels, {}, project);
    const Rgb a = *highIt;
    const Rgb b = *lowIt;
    std::uint16_t c0 = Pack565(a);
    std::uint16_t c1 = Pack565(b);
    if (c0 < c1)
    {
        std::swap(c0, c1);
    }
    std::uint32_t indices = 0;
    if (c0 != c1)
    {
        const Rgb p0 = Unpack565(c0);
        const Rgb p1 = Unpack565(c1);
        const std::array<Rgb, 4> palette = {p0, p1,
                                            Rgb{(2 * p0.r + p1.r) / 3, (2 * p0.g + p1.g) / 3, (2 * p0.b + p1.b) / 3},
                                            Rgb{(p0.r + 2 * p1.r) / 3, (p0.g + 2 * p1.g) / 3, (p0.b + 2 * p1.b) / 3}};
        for (std::size_t i = 0; i < 16; ++i)
        {
            std::uint32_t best = 0;
            int bestDistance = DistanceSquared(texels[i], palette[0]);
            for (std::uint32_t p = 1; p < 4; ++p)
            {
                const int d = DistanceSquared(texels[i], palette[p]);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = p;
                }
            }
            indices |= best << (2 * i);
        }
    }
    Put16(out, c0);
    Put16(out, c1);
    Put16(out, static_cast<std::uint16_t>(indices & 0xFFFF));
    Put16(out, static_cast<std::uint16_t>(indices >> 16));
}

[[nodiscard]] std::array<int, 8> Bc4Palette(int a0, int a1) noexcept
{
    std::array<int, 8> palette{a0, a1, 0, 0, 0, 0, 0, 0};
    if (a0 > a1)
    {
        for (int i = 1; i <= 6; ++i)
        {
            palette[static_cast<std::size_t>(i) + 1] = ((7 - i) * a0 + i * a1) / 7;
        }
    }
    else
    {
        for (int i = 1; i <= 4; ++i)
        {
            palette[static_cast<std::size_t>(i) + 1] = ((5 - i) * a0 + i * a1) / 5;
        }
        palette[6] = 0;
        palette[7] = 255;
    }
    return palette;
}

void EncodeBc4Block(const std::array<int, 16>& values, std::vector<std::byte>& out)
{
    const auto [minIt, maxIt] = std::ranges::minmax_element(values);
    const int a0 = *maxIt; // a0 > a1: the eight-value mode
    const int a1 = *minIt;
    std::uint64_t indices = 0;
    if (a0 != a1)
    {
        const std::array<int, 8> palette = Bc4Palette(a0, a1);
        for (std::size_t i = 0; i < 16; ++i)
        {
            std::uint64_t best = 0;
            int bestDistance = std::abs(values[i] - palette[0]);
            for (std::uint64_t p = 1; p < 8; ++p)
            {
                const int d = std::abs(values[i] - palette[p]);
                if (d < bestDistance)
                {
                    bestDistance = d;
                    best = p;
                }
            }
            indices |= best << (3 * i);
        }
    }
    out.push_back(static_cast<std::byte>(a0));
    out.push_back(static_cast<std::byte>(a1));
    for (int i = 0; i < 6; ++i)
    {
        out.push_back(static_cast<std::byte>((indices >> (8 * i)) & 0xFF));
    }
}

void DecodeBc4Block(const std::byte* block, std::array<int, 16>& values) noexcept
{
    const std::array<int, 8> palette = Bc4Palette(std::to_integer<int>(block[0]), std::to_integer<int>(block[1]));
    std::uint64_t indices = 0;
    for (std::size_t i = 0; i < 6; ++i)
    {
        indices |= std::to_integer<std::uint64_t>(block[2 + i]) << (8 * i);
    }
    for (std::size_t i = 0; i < 16; ++i)
    {
        values[i] = palette[(indices >> (3 * i)) & 7];
    }
}

} // namespace

std::uint32_t MipCount(std::uint32_t width, std::uint32_t height) noexcept
{
    const std::uint32_t largest = std::max(width, height);
    return width == 0 || height == 0 ? 0 : static_cast<std::uint32_t>(std::bit_width(largest));
}

std::size_t MipByteSize(TextureFormat format, std::uint32_t width, std::uint32_t height) noexcept
{
    switch (format)
    {
    case TextureFormat::Rgba8:
        return std::size_t{width} * height * 4;
    case TextureFormat::Rgba32F:
        return std::size_t{width} * height * 16;
    case TextureFormat::Bc1:
    case TextureFormat::Bc4:
        return Blocks(width) * Blocks(height) * 8;
    case TextureFormat::Bc5:
        return Blocks(width) * Blocks(height) * 16;
    }
    return 0;
}

std::vector<Image8> GenerateMips(const Image8& base, ColorSpace colorSpace)
{
    std::vector<Image8> chain;
    chain.push_back(base);
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        chain.push_back(Halve(chain.back(), colorSpace));
    }
    return chain;
}

std::vector<ImageF> GenerateMips(const ImageF& base)
{
    std::vector<ImageF> chain;
    chain.push_back(base);
    while (chain.back().width > 1 || chain.back().height > 1)
    {
        chain.push_back(Halve(chain.back()));
    }
    return chain;
}

Result<void> ValidateTexture(const TextureAsset& texture)
{
    const auto corrupt = [](std::string message) { return MakeError(ErrorCode::Corrupt, std::move(message)); };
    if (texture.width == 0 || texture.height == 0 || texture.width > kMaxTextureDimension ||
        texture.height > kMaxTextureDimension)
    {
        return corrupt(std::format("texture: bad dimensions {}x{}", texture.width, texture.height));
    }
    if (static_cast<std::uint8_t>(texture.format) >= kTextureFormatCount ||
        static_cast<std::uint8_t>(texture.colorSpace) > 1)
    {
        return corrupt("texture: unknown format or color space");
    }
    const std::uint32_t full = MipCount(texture.width, texture.height);
    if (texture.mips.size() != 1 && texture.mips.size() != full)
    {
        return corrupt(std::format("texture: {} levels, expected 1 or {}", texture.mips.size(), full));
    }
    for (std::size_t i = 0; i < texture.mips.size(); ++i)
    {
        const std::uint32_t w = std::max(1u, texture.width >> i);
        const std::uint32_t h = std::max(1u, texture.height >> i);
        if (texture.mips[i].size() != MipByteSize(texture.format, w, h))
        {
            return corrupt(std::format("texture: level {} has the wrong size", i));
        }
    }
    return {};
}

template <typename Archive>
void Serialize(Archive& archive, TextureAsset& texture)
{
    archive(texture.width, texture.height, texture.format, texture.colorSpace, texture.mips);
}

std::vector<std::byte> WriteTextureAsset(const TextureAsset& texture)
{
    return detail::WriteHashed(kTextureMagic, kPayloadMagic, kTextureVersion, texture);
}

Result<TextureAsset> ReadTextureAsset(std::span<const std::byte> data)
{
    INTERI_TRY_ASSIGN(texture,
                      detail::ReadHashed<TextureAsset>(data, kTextureMagic, kPayloadMagic, kTextureVersion, "texture"));
    INTERI_TRY(ValidateTexture(texture));
    return texture;
}

Result<TextureAsset> LoadTextureAsset(const IFileSystem& files, std::string_view path)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto texture = ReadTextureAsset(bytes);
    if (!texture)
    {
        return MakeError(texture.error().code, std::format("{}: {}", path, texture.error().message));
    }
    return texture;
}

std::vector<std::byte> EncodeBc1(const Image8& image)
{
    std::vector<std::byte> out;
    out.reserve(Blocks(image.width) * Blocks(image.height) * 8);
    for (std::size_t by = 0; by < Blocks(image.height); ++by)
    {
        for (std::size_t bx = 0; bx < Blocks(image.width); ++bx)
        {
            std::array<Rgb, 16> texels{};
            for (std::size_t i = 0; i < 16; ++i)
            {
                const std::uint8_t* t = TexelClamped(image, bx * 4 + i % 4, by * 4 + i / 4);
                texels[i] = {t[0], t[1], t[2]};
            }
            EncodeBc1Block(texels, out);
        }
    }
    return out;
}

std::vector<std::byte> EncodeBc4(const Image8& image, unsigned channel)
{
    std::vector<std::byte> out;
    out.reserve(Blocks(image.width) * Blocks(image.height) * 8);
    for (std::size_t by = 0; by < Blocks(image.height); ++by)
    {
        for (std::size_t bx = 0; bx < Blocks(image.width); ++bx)
        {
            std::array<int, 16> values{};
            for (std::size_t i = 0; i < 16; ++i)
            {
                values[i] = TexelClamped(image, bx * 4 + i % 4, by * 4 + i / 4)[channel & 3];
            }
            EncodeBc4Block(values, out);
        }
    }
    return out;
}

std::vector<std::byte> EncodeBc5(const Image8& image)
{
    std::vector<std::byte> out;
    out.reserve(Blocks(image.width) * Blocks(image.height) * 16);
    for (std::size_t by = 0; by < Blocks(image.height); ++by)
    {
        for (std::size_t bx = 0; bx < Blocks(image.width); ++bx)
        {
            for (unsigned channel = 0; channel < 2; ++channel)
            {
                std::array<int, 16> values{};
                for (std::size_t i = 0; i < 16; ++i)
                {
                    values[i] = TexelClamped(image, bx * 4 + i % 4, by * 4 + i / 4)[channel];
                }
                EncodeBc4Block(values, out);
            }
        }
    }
    return out;
}

Result<TextureAsset> BuildTexture(const Image8& image, const TextureBuildSettings& settings)
{
    if (image.width == 0 || image.height == 0 || image.width > kMaxTextureDimension ||
        image.height > kMaxTextureDimension || image.texels.size() != std::size_t{image.width} * image.height * 4)
    {
        return MakeError(ErrorCode::InvalidArgument, "texture: empty, oversized or inconsistent image");
    }
    TextureAsset texture;
    texture.width = image.width;
    texture.height = image.height;
    texture.colorSpace = settings.colorSpace;
    switch (settings.compression)
    {
    case TextureCompression::None:
        texture.format = TextureFormat::Rgba8;
        break;
    case TextureCompression::Bc1:
        texture.format = TextureFormat::Bc1;
        break;
    case TextureCompression::Bc4:
        texture.format = TextureFormat::Bc4;
        break;
    case TextureCompression::Bc5:
        texture.format = TextureFormat::Bc5;
        break;
    }
    std::vector<Image8> levels;
    if (settings.generateMips)
    {
        levels = GenerateMips(image, settings.colorSpace);
    }
    else
    {
        levels.push_back(image);
    }
    for (const Image8& level : levels)
    {
        switch (texture.format)
        {
        case TextureFormat::Bc1:
            texture.mips.push_back(EncodeBc1(level));
            break;
        case TextureFormat::Bc4:
            texture.mips.push_back(EncodeBc4(level, 0));
            break;
        case TextureFormat::Bc5:
            texture.mips.push_back(EncodeBc5(level));
            break;
        default:
            texture.mips.push_back(ToBytes(level.texels));
            break;
        }
    }
    return texture;
}

Result<TextureAsset> BuildTexture(const ImageF& image, bool generateMips)
{
    if (image.width == 0 || image.height == 0 || image.width > kMaxTextureDimension ||
        image.height > kMaxTextureDimension || image.texels.size() != std::size_t{image.width} * image.height * 4)
    {
        return MakeError(ErrorCode::InvalidArgument, "texture: empty, oversized or inconsistent image");
    }
    TextureAsset texture;
    texture.width = image.width;
    texture.height = image.height;
    texture.format = TextureFormat::Rgba32F;
    texture.colorSpace = ColorSpace::Linear;
    if (generateMips)
    {
        for (const ImageF& level : GenerateMips(image))
        {
            texture.mips.push_back(ToBytes(level.texels));
        }
    }
    else
    {
        texture.mips.push_back(ToBytes(image.texels));
    }
    return texture;
}

Result<Image8> DecodeMip(const TextureAsset& texture, std::uint32_t mip)
{
    INTERI_TRY(ValidateTexture(texture));
    if (mip >= texture.mips.size())
    {
        return MakeError(ErrorCode::InvalidArgument, "texture: no such level");
    }
    Image8 image;
    image.width = std::max(1u, texture.width >> mip);
    image.height = std::max(1u, texture.height >> mip);
    image.texels.assign(std::size_t{image.width} * image.height * 4, 0);
    const std::vector<std::byte>& data = texture.mips[mip];
    const auto put = [&](std::size_t x, std::size_t y, int r, int g, int b)
    {
        if (x < image.width && y < image.height)
        {
            std::uint8_t* out = &image.texels[(y * image.width + x) * 4];
            out[0] = static_cast<std::uint8_t>(r);
            out[1] = static_cast<std::uint8_t>(g);
            out[2] = static_cast<std::uint8_t>(b);
            out[3] = 255;
        }
    };
    switch (texture.format)
    {
    case TextureFormat::Rgba8:
        std::ranges::transform(data, image.texels.begin(),
                               [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
        break;
    case TextureFormat::Rgba32F:
        for (std::size_t i = 0; i < image.texels.size(); ++i)
        {
            float value = 0.0f;
            std::memcpy(&value, data.data() + i * sizeof(float), sizeof(float));
            image.texels[i] = static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        }
        break;
    case TextureFormat::Bc1:
        for (std::size_t by = 0; by < Blocks(image.height); ++by)
        {
            for (std::size_t bx = 0; bx < Blocks(image.width); ++bx)
            {
                const std::byte* block = data.data() + (by * Blocks(image.width) + bx) * 8;
                const auto word = [&](std::size_t i)
                {
                    return static_cast<std::uint16_t>(std::to_integer<unsigned>(block[i]) |
                                                      (std::to_integer<unsigned>(block[i + 1]) << 8));
                };
                const std::uint16_t c0 = word(0);
                const std::uint16_t c1 = word(2);
                const Rgb p0 = Unpack565(c0);
                const Rgb p1 = Unpack565(c1);
                std::array<Rgb, 4> palette{p0, p1, {}, {}};
                if (c0 > c1)
                {
                    palette[2] = {(2 * p0.r + p1.r) / 3, (2 * p0.g + p1.g) / 3, (2 * p0.b + p1.b) / 3};
                    palette[3] = {(p0.r + 2 * p1.r) / 3, (p0.g + 2 * p1.g) / 3, (p0.b + 2 * p1.b) / 3};
                }
                else
                {
                    palette[2] = {(p0.r + p1.r) / 2, (p0.g + p1.g) / 2, (p0.b + p1.b) / 2};
                }
                const std::uint32_t indices = word(4) | (std::uint32_t{word(6)} << 16);
                for (std::size_t i = 0; i < 16; ++i)
                {
                    const Rgb& c = palette[(indices >> (2 * i)) & 3];
                    put(bx * 4 + i % 4, by * 4 + i / 4, c.r, c.g, c.b);
                }
            }
        }
        break;
    case TextureFormat::Bc4:
    case TextureFormat::Bc5:
    {
        const std::size_t blockBytes = texture.format == TextureFormat::Bc4 ? 8 : 16;
        for (std::size_t by = 0; by < Blocks(image.height); ++by)
        {
            for (std::size_t bx = 0; bx < Blocks(image.width); ++bx)
            {
                const std::byte* block = data.data() + (by * Blocks(image.width) + bx) * blockBytes;
                std::array<int, 16> red{};
                std::array<int, 16> green{};
                DecodeBc4Block(block, red);
                if (blockBytes == 16)
                {
                    DecodeBc4Block(block + 8, green);
                }
                for (std::size_t i = 0; i < 16; ++i)
                {
                    put(bx * 4 + i % 4, by * 4 + i / 4, red[i], green[i], 0);
                }
            }
        }
        break;
    }
    }
    return image;
}

} // namespace interi::assets
