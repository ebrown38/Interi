// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Types.h>

#include <cstdint>

namespace interi::rhi
{

struct SwapchainDesc
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    /// Wait for vertical blank (FIFO). Off picks a tearing-free low-latency mode when available.
    bool vsync = true;
};

/// The images presented to a window. Per frame: AcquireNextImage, record commands that leave the image in
/// ResourceState::Present, submit them with IDevice::Submit(lists, swapchain), then Present. Frame thread only.
class ISwapchain
{
public:
    ISwapchain() = default;
    ISwapchain(const ISwapchain&) = delete;
    ISwapchain& operator=(const ISwapchain&) = delete;
    ISwapchain(ISwapchain&&) = delete;
    ISwapchain& operator=(ISwapchain&&) = delete;
    virtual ~ISwapchain() = default;

    /// The image to render this frame (contents undefined). Recreates the swapchain first if it was resized or the
    /// window system reported it out of date. Fails while the size is zero (minimised window): skip the frame.
    [[nodiscard]] virtual Result<TextureHandle> AcquireNextImage() = 0;
    /// Presents the acquired image once the submission that rendered it completes.
    [[nodiscard]] virtual Result<void> Present() = 0;
    /// Requests a new size (the window's drawable size); applied by the next AcquireNextImage.
    virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

    [[nodiscard]] virtual Format ImageFormat() const = 0;
    [[nodiscard]] virtual std::uint32_t Width() const = 0;
    [[nodiscard]] virtual std::uint32_t Height() const = 0;
    [[nodiscard]] virtual std::uint32_t ImageCount() const = 0;
};

} // namespace interi::rhi
