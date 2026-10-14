// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>

namespace interi
{

struct WindowExtent
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;

    friend constexpr bool operator==(WindowExtent, WindowExtent) noexcept = default;
};

struct WindowDesc
{
    std::string_view title = "Interi";
    WindowExtent size{1280, 720};
    bool resizable = true;
    /// Create the window for Vulkan rendering (required before creating a Vulkan surface for it).
    bool vulkan = false;
};

/// An OS window. Events (resize, close) arrive through the platform's IEventSource. Main thread only.
class IWindow
{
public:
    IWindow() = default;
    IWindow(const IWindow&) = delete;
    IWindow& operator=(const IWindow&) = delete;
    IWindow(IWindow&&) = delete;
    IWindow& operator=(IWindow&&) = delete;
    virtual ~IWindow() = default;

    /// Drawable size in pixels.
    [[nodiscard]] virtual WindowExtent Size() const = 0;
    virtual void SetTitle(std::string_view title) = 0;
    /// The backend's window object (an SDL_Window* for the SDL backend), for creating render surfaces.
    [[nodiscard]] virtual void* NativeHandle() const = 0;
};

} // namespace interi
