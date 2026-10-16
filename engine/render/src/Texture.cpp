// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/render/Texture.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/UploadQueue.h>

#include <algorithm>
#include <array>

namespace interi::render
{

std::vector<ImageRgba8> GenerateMips(const ImageRgba8& base)
{
    INTERI_ASSERT(base.width > 0 && base.height > 0 && base.texels.size() == std::size_t{base.width} * base.height * 4);
    std::vector<ImageRgba8> mips{base};
    while (mips.back().width > 1 || mips.back().height > 1)
    {
        const ImageRgba8& source = mips.back();
        ImageRgba8 level{
            .width = std::max(source.width / 2, 1u), .height = std::max(source.height / 2, 1u), .texels = {}};
        level.texels.resize(std::size_t{level.width} * level.height * 4);
        for (std::uint32_t y = 0; y < level.height; ++y)
        {
            const std::uint32_t y0 = std::min(y * 2, source.height - 1);
            const std::uint32_t y1 = std::min(y * 2 + 1, source.height - 1);
            for (std::uint32_t x = 0; x < level.width; ++x)
            {
                const std::uint32_t x0 = std::min(x * 2, source.width - 1);
                const std::uint32_t x1 = std::min(x * 2 + 1, source.width - 1);
                for (std::uint32_t c = 0; c < 4; ++c)
                {
                    const std::uint32_t sum =
                        source.At(x0, y0)[c] + source.At(x1, y0)[c] + source.At(x0, y1)[c] + source.At(x1, y1)[c];
                    level.At(x, y)[c] = static_cast<std::uint8_t>((sum + 2) / 4);
                }
            }
        }
        mips.push_back(std::move(level));
    }
    return mips;
}

Result<rhi::TextureHandle> CreateTexture(rhi::IDevice& device, rhi::UploadQueue& uploads, rhi::ICommandList& commands,
                                         std::span<const ImageRgba8> mips, rhi::Format format,
                                         std::string_view debugName)
{
    INTERI_ASSERT(!mips.empty());
    INTERI_TRY_ASSIGN(texture, device.CreateTexture({.width = mips[0].width,
                                                     .height = mips[0].height,
                                                     .mipLevels = static_cast<std::uint32_t>(mips.size()),
                                                     .format = format,
                                                     .usage = rhi::TextureUsage::CopyDst | rhi::TextureUsage::Sampled,
                                                     .debugName = debugName}));
    const std::array toCopy{rhi::TextureBarrier{texture, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst}};
    commands.Barriers({}, toCopy);
    for (std::uint32_t mip = 0; mip < mips.size(); ++mip)
    {
        if (Result<void> uploaded = uploads.UploadTexture(commands, texture, mip, 0, mips[mip].width, mips[mip].height,
                                                          4, std::as_bytes(std::span(mips[mip].texels)));
            !uploaded)
        {
            device.DestroyTexture(texture);
            return std::unexpected(std::move(uploaded.error()));
        }
    }
    const std::array toRead{rhi::TextureBarrier{texture, rhi::ResourceState::CopyDst, rhi::ResourceState::ShaderRead}};
    commands.Barriers({}, toRead);
    return texture;
}

} // namespace interi::render
