// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace interi::rhi
{
class IDevice;
class ICommandList;
class UploadQueue;
} // namespace interi::rhi

namespace interi::render
{

/// CPU image of 4-byte RGBA texels, rows top to bottom.
struct ImageRgba8
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> texels; ///< width * height * 4 bytes.

    [[nodiscard]] std::uint8_t* At(std::uint32_t x, std::uint32_t y) noexcept { return &texels[(std::size_t{y} * width + x) * 4]; }
    [[nodiscard]] const std::uint8_t* At(std::uint32_t x, std::uint32_t y) const noexcept
    {
        return &texels[(std::size_t{y} * width + x) * 4];
    }
};

/// The full mip chain of `base` (base first, down to 1 x 1). Each level averages 2 x 2 texels of the previous one
/// (edge texels repeat for odd sizes), with integer rounding on the stored bytes, so the result is exact on every
/// platform. sRGB data is averaged as encoded.
[[nodiscard]] std::vector<ImageRgba8> GenerateMips(const ImageRgba8& base);

/// Creates a sampled texture of `format` (4 bytes per texel) from `mips` and records its upload into `commands`:
/// a transition to CopyDst, one copy per level, then a transition to ShaderRead. Usable once those commands ran.
[[nodiscard]] Result<rhi::TextureHandle> CreateTexture(rhi::IDevice& device, rhi::UploadQueue& uploads,
                                                       rhi::ICommandList& commands, std::span<const ImageRgba8> mips,
                                                       rhi::Format format, std::string_view debugName = {});

} // namespace interi::render
