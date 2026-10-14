// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/platform/InputEvents.h>
#include <interi/platform/Window.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward declarations so engine code including this header doesn't pull in SDL.
struct SDL_Window;
struct SDL_Gamepad;
union SDL_Event;

namespace interi
{

struct SdlPlatformDesc
{
    /// SDL video driver to use ("x11", "wayland", "windows", "dummy", ...). Empty lets SDL choose.
    std::string videoDriver;
};

class SdlWindow;

/// Owns SDL's video and gamepad subsystems for the process and delivers their events as InputEvents. Only one may
/// exist at a time; main thread only.
class SdlPlatform final : public IEventSource
{
public:
    /// Initialises SDL. Unsupported when another SdlPlatform exists; IoError with SDL's message if SDL fails.
    [[nodiscard]] static Result<std::unique_ptr<SdlPlatform>> Create(const SdlPlatformDesc& desc = {});
    ~SdlPlatform() override;

    SdlPlatform(const SdlPlatform&) = delete;
    SdlPlatform& operator=(const SdlPlatform&) = delete;
    SdlPlatform(SdlPlatform&&) = delete;
    SdlPlatform& operator=(SdlPlatform&&) = delete;

    /// Opens a window. The window must be destroyed before the platform.
    [[nodiscard]] Result<std::unique_ptr<IWindow>> OpenWindow(const WindowDesc& desc);

    void PollEvents(std::vector<InputEvent>& events) override;

    /// Name of the video driver SDL is using.
    [[nodiscard]] std::string_view VideoDriver() const;
    /// Video drivers compiled into this SDL build.
    [[nodiscard]] static std::vector<std::string> AvailableVideoDrivers();

    /// Instance extensions a Vulkan instance needs to create surfaces for this platform's windows. Call it after
    /// opening a window with WindowDesc::vulkan (SDL loads Vulkan then, and some drivers' answers depend on it).
    /// Unsupported if the video driver has no Vulkan support (e.g. "dummy").
    [[nodiscard]] Result<std::vector<const char*>> VulkanInstanceExtensions() const;
    /// Creates a surface for `window` (opened with WindowDesc::vulkan). `instance` is a VkInstance; the result is a
    /// VkSurfaceKHR handle value owned by the caller. Opaque so neither SDL nor Vulkan headers leak through.
    [[nodiscard]] Result<std::uint64_t> CreateVulkanSurface(const IWindow& window, void* instance) const;

private:
    SdlPlatform() = default;

    void HandleGamepadConnection(const SDL_Event& event);

    std::unordered_map<std::uint32_t, SDL_Gamepad*> m_gamepads;
};

/// Physical key for an SDL scancode (an SDL_Scancode value); Key::Unknown if it has no mapping.
[[nodiscard]] Key KeyFromSdlScancode(int scancode) noexcept;

/// The engine event for an SDL event, or nothing if it is not one the engine uses.
[[nodiscard]] std::optional<InputEvent> TranslateSdlEvent(const SDL_Event& event) noexcept;

} // namespace interi
