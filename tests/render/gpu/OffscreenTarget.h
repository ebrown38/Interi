// SPDX-License-Identifier: MIT
#pragma once

#include <interi/rhi/Device.h>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace interi::testing
{

/// An RGBA8 colour target plus a reverse-Z depth buffer that tests render into and read back.
class OffscreenTarget
{
public:
    OffscreenTarget(rhi::IDevice& device, std::uint32_t width, std::uint32_t height,
                    rhi::Format format = rhi::Format::R8G8B8A8Unorm)
        : m_device(device)
        , m_width(width)
        , m_height(height)
        , m_rowPitch((width * 4 + rhi::kTextureRowPitchAlignment - 1) / rhi::kTextureRowPitchAlignment *
                     rhi::kTextureRowPitchAlignment)
    {
        auto color = device.CreateTexture({.width = width,
                                           .height = height,
                                           .format = format,
                                           .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::CopySrc});
        auto depth = device.CreateTexture({.width = width,
                                           .height = height,
                                           .format = rhi::Format::D32Float,
                                           .usage = rhi::TextureUsage::DepthTarget});
        auto readback = device.CreateBuffer({.size = std::uint64_t{m_rowPitch} * height,
                                             .usage = rhi::BufferUsage::CopyDst,
                                             .memory = rhi::MemoryUsage::Readback});
        auto pool = device.CreateCommandPool();
        EXPECT_TRUE(color && depth && readback && pool);
        m_color = *color;
        m_depth = *depth;
        m_readback = *readback;
        m_pool = *pool;
    }

    ~OffscreenTarget()
    {
        m_device.DestroyCommandPool(m_pool);
        m_device.DestroyBuffer(m_readback);
        m_device.DestroyTexture(m_depth);
        m_device.DestroyTexture(m_color);
    }

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;
    OffscreenTarget(OffscreenTarget&&) = delete;
    OffscreenTarget& operator=(OffscreenTarget&&) = delete;

    [[nodiscard]] rhi::TextureHandle Color() const { return m_color; }
    [[nodiscard]] rhi::TextureHandle Depth() const { return m_depth; }

    /// Clears to `clear` (depth to 0), runs `draw` inside the rendering pass, and returns the pixels as RGBA bytes,
    /// rows top to bottom.
    template <typename Draw>
    std::vector<std::array<std::uint8_t, 4>> Render(std::array<float, 4> clear, Draw&& draw)
    {
        m_device.ResetCommandPool(m_pool);
        rhi::ICommandList& commands = m_device.BeginCommandList(m_pool);
        const std::array toTargets{
            rhi::TextureBarrier{m_color, rhi::ResourceState::Undefined, rhi::ResourceState::ColorTarget},
            rhi::TextureBarrier{m_depth, rhi::ResourceState::Undefined, rhi::ResourceState::DepthWrite}};
        commands.Barriers({}, toTargets);
        const std::array colors{rhi::ColorAttachment{.texture = m_color, .clearColor = clear}};
        const rhi::DepthAttachment depth{.texture = m_depth};
        commands.BeginRendering({.colors = colors, .depth = &depth, .width = m_width, .height = m_height});
        draw(commands);
        commands.EndRendering();
        const std::array toCopy{
            rhi::TextureBarrier{m_color, rhi::ResourceState::ColorTarget, rhi::ResourceState::CopySrc}};
        const std::array toWrite{
            rhi::BufferBarrier{m_readback, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst}};
        commands.Barriers(toWrite, toCopy);
        commands.CopyTextureToBuffer(m_color, m_readback, 0, m_rowPitch);
        const std::array toHost{
            rhi::BufferBarrier{m_readback, rhi::ResourceState::CopyDst, rhi::ResourceState::HostRead}};
        commands.Barriers(toHost, {});
        rhi::ICommandList* const lists[] = {&commands};
        m_device.WaitForValue(m_device.Submit(lists));

        std::vector<std::array<std::uint8_t, 4>> pixels(std::size_t{m_width} * m_height);
        const std::byte* mapped = m_device.MappedMemory(m_readback).data();
        for (std::uint32_t y = 0; y < m_height; ++y)
        {
            std::memcpy(&pixels[std::size_t{y} * m_width], mapped + std::size_t{y} * m_rowPitch, m_width * 4ull);
        }
        return pixels;
    }

private:
    rhi::IDevice& m_device;
    std::uint32_t m_width;
    std::uint32_t m_height;
    std::uint32_t m_rowPitch;
    rhi::TextureHandle m_color;
    rhi::TextureHandle m_depth;
    rhi::BufferHandle m_readback;
    rhi::CommandPoolHandle m_pool;
};

} // namespace interi::testing
