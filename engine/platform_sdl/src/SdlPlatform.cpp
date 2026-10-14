// SPDX-License-Identifier: MIT
#include <interi/platform_sdl/SdlPlatform.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <utility>

namespace interi
{

namespace
{

std::atomic<bool> g_platformAlive{false};

[[nodiscard]] std::string SdlError(std::string_view what)
{
    return std::string(what) + ": " + SDL_GetError();
}

/// Stick values arrive as [-32768, 32767] and triggers as [0, 32767]; both map to [-1, 1] / [0, 1].
[[nodiscard]] float NormalizeAxis(std::int16_t value) noexcept
{
    return std::max(-1.0f, static_cast<float>(value) / 32767.0f);
}

[[nodiscard]] std::optional<MouseButton> TranslateMouseButton(std::uint8_t button) noexcept
{
    switch (button)
    {
    case SDL_BUTTON_LEFT:
        return MouseButton::Left;
    case SDL_BUTTON_RIGHT:
        return MouseButton::Right;
    case SDL_BUTTON_MIDDLE:
        return MouseButton::Middle;
    case SDL_BUTTON_X1:
        return MouseButton::X1;
    case SDL_BUTTON_X2:
        return MouseButton::X2;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<GamepadButton> TranslateGamepadButton(std::uint8_t button) noexcept
{
    switch (static_cast<SDL_GamepadButton>(button))
    {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return GamepadButton::South;
    case SDL_GAMEPAD_BUTTON_EAST:
        return GamepadButton::East;
    case SDL_GAMEPAD_BUTTON_WEST:
        return GamepadButton::West;
    case SDL_GAMEPAD_BUTTON_NORTH:
        return GamepadButton::North;
    case SDL_GAMEPAD_BUTTON_BACK:
        return GamepadButton::Back;
    case SDL_GAMEPAD_BUTTON_GUIDE:
        return GamepadButton::Guide;
    case SDL_GAMEPAD_BUTTON_START:
        return GamepadButton::Start;
    case SDL_GAMEPAD_BUTTON_LEFT_STICK:
        return GamepadButton::LeftStick;
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
        return GamepadButton::RightStick;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
        return GamepadButton::LeftShoulder;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
        return GamepadButton::RightShoulder;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return GamepadButton::DpadUp;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return GamepadButton::DpadDown;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return GamepadButton::DpadLeft;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return GamepadButton::DpadRight;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<GamepadAxis> TranslateGamepadAxis(std::uint8_t axis) noexcept
{
    switch (static_cast<SDL_GamepadAxis>(axis))
    {
    case SDL_GAMEPAD_AXIS_LEFTX:
        return GamepadAxis::LeftX;
    case SDL_GAMEPAD_AXIS_LEFTY:
        return GamepadAxis::LeftY;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        return GamepadAxis::RightX;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        return GamepadAxis::RightY;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        return GamepadAxis::LeftTrigger;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        return GamepadAxis::RightTrigger;
    default:
        return std::nullopt;
    }
}

} // namespace

/// An SDL window. Destroyed with its owner.
class SdlWindow final : public IWindow
{
public:
    explicit SdlWindow(SDL_Window* window) noexcept
        : m_window(window)
    {
    }
    ~SdlWindow() override { SDL_DestroyWindow(m_window); }

    SdlWindow(const SdlWindow&) = delete;
    SdlWindow& operator=(const SdlWindow&) = delete;
    SdlWindow(SdlWindow&&) = delete;
    SdlWindow& operator=(SdlWindow&&) = delete;

    [[nodiscard]] WindowExtent Size() const override
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
        return WindowExtent{static_cast<std::uint32_t>(std::max(width, 0)),
                            static_cast<std::uint32_t>(std::max(height, 0))};
    }

    void SetTitle(std::string_view title) override { SDL_SetWindowTitle(m_window, std::string(title).c_str()); }

    [[nodiscard]] void* NativeHandle() const override { return m_window; }

private:
    SDL_Window* m_window;
};

Key KeyFromSdlScancode(int scancode) noexcept
{
    const auto code = static_cast<SDL_Scancode>(scancode);
    if (code >= SDL_SCANCODE_A && code <= SDL_SCANCODE_Z)
    {
        return static_cast<Key>(static_cast<int>(Key::A) + (code - SDL_SCANCODE_A));
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_9)
    {
        return static_cast<Key>(static_cast<int>(Key::Num1) + (code - SDL_SCANCODE_1));
    }
    if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12)
    {
        return static_cast<Key>(static_cast<int>(Key::F1) + (code - SDL_SCANCODE_F1));
    }
    switch (code)
    {
    case SDL_SCANCODE_0:
        return Key::Num0;
    case SDL_SCANCODE_SPACE:
        return Key::Space;
    case SDL_SCANCODE_RETURN:
        return Key::Enter;
    case SDL_SCANCODE_ESCAPE:
        return Key::Escape;
    case SDL_SCANCODE_TAB:
        return Key::Tab;
    case SDL_SCANCODE_BACKSPACE:
        return Key::Backspace;
    case SDL_SCANCODE_DELETE:
        return Key::Delete;
    case SDL_SCANCODE_INSERT:
        return Key::Insert;
    case SDL_SCANCODE_HOME:
        return Key::Home;
    case SDL_SCANCODE_END:
        return Key::End;
    case SDL_SCANCODE_PAGEUP:
        return Key::PageUp;
    case SDL_SCANCODE_PAGEDOWN:
        return Key::PageDown;
    case SDL_SCANCODE_LEFT:
        return Key::Left;
    case SDL_SCANCODE_RIGHT:
        return Key::Right;
    case SDL_SCANCODE_UP:
        return Key::Up;
    case SDL_SCANCODE_DOWN:
        return Key::Down;
    case SDL_SCANCODE_LSHIFT:
        return Key::LeftShift;
    case SDL_SCANCODE_RSHIFT:
        return Key::RightShift;
    case SDL_SCANCODE_LCTRL:
        return Key::LeftCtrl;
    case SDL_SCANCODE_RCTRL:
        return Key::RightCtrl;
    case SDL_SCANCODE_LALT:
        return Key::LeftAlt;
    case SDL_SCANCODE_RALT:
        return Key::RightAlt;
    case SDL_SCANCODE_MINUS:
        return Key::Minus;
    case SDL_SCANCODE_EQUALS:
        return Key::Equals;
    case SDL_SCANCODE_LEFTBRACKET:
        return Key::LeftBracket;
    case SDL_SCANCODE_RIGHTBRACKET:
        return Key::RightBracket;
    case SDL_SCANCODE_BACKSLASH:
        return Key::Backslash;
    case SDL_SCANCODE_SEMICOLON:
        return Key::Semicolon;
    case SDL_SCANCODE_APOSTROPHE:
        return Key::Apostrophe;
    case SDL_SCANCODE_GRAVE:
        return Key::Grave;
    case SDL_SCANCODE_COMMA:
        return Key::Comma;
    case SDL_SCANCODE_PERIOD:
        return Key::Period;
    case SDL_SCANCODE_SLASH:
        return Key::Slash;
    default:
        return Key::Unknown;
    }
}

std::optional<InputEvent> TranslateSdlEvent(const SDL_Event& event) noexcept
{
    switch (event.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        return QuitEvent{};
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        return WindowResizedEvent{.width = static_cast<std::uint32_t>(std::max(event.window.data1, 0)),
                                  .height = static_cast<std::uint32_t>(std::max(event.window.data2, 0))};
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        const Key key = KeyFromSdlScancode(static_cast<int>(event.key.scancode));
        if (key == Key::Unknown)
        {
            return std::nullopt;
        }
        return KeyEvent{.key = key, .down = event.type == SDL_EVENT_KEY_DOWN, .repeat = event.key.repeat};
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (const auto button = TranslateMouseButton(event.button.button))
        {
            return MouseButtonEvent{.button = *button, .down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN};
        }
        return std::nullopt;
    case SDL_EVENT_MOUSE_MOTION:
        return MouseMotionEvent{
            .x = event.motion.x, .y = event.motion.y, .deltaX = event.motion.xrel, .deltaY = event.motion.yrel};
    case SDL_EVENT_MOUSE_WHEEL:
        return MouseWheelEvent{.deltaX = event.wheel.x, .deltaY = event.wheel.y};
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
        return GamepadConnectionEvent{.gamepad = event.gdevice.which,
                                      .connected = event.type == SDL_EVENT_GAMEPAD_ADDED};
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (const auto button = TranslateGamepadButton(event.gbutton.button))
        {
            return GamepadButtonEvent{
                .gamepad = event.gbutton.which, .button = *button, .down = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN};
        }
        return std::nullopt;
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        if (const auto axis = TranslateGamepadAxis(event.gaxis.axis))
        {
            return GamepadAxisEvent{
                .gamepad = event.gaxis.which, .axis = *axis, .value = NormalizeAxis(event.gaxis.value)};
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

Result<std::unique_ptr<SdlPlatform>> SdlPlatform::Create(const SdlPlatformDesc& desc)
{
    if (g_platformAlive.exchange(true))
    {
        return MakeError(ErrorCode::Unsupported, "an SdlPlatform already exists");
    }
    if (!desc.videoDriver.empty())
    {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, desc.videoDriver.c_str());
    }
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
    {
        g_platformAlive = false;
        return MakeError(ErrorCode::IoError, SdlError("SDL_Init failed"));
    }
    return std::unique_ptr<SdlPlatform>(new SdlPlatform());
}

SdlPlatform::~SdlPlatform()
{
    for (const auto& [id, gamepad] : m_gamepads)
    {
        SDL_CloseGamepad(gamepad);
    }
    SDL_Quit();
    g_platformAlive = false;
}

Result<std::unique_ptr<IWindow>> SdlPlatform::OpenWindow(const WindowDesc& desc)
{
    SDL_WindowFlags flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (desc.resizable)
    {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (desc.vulkan)
    {
        flags |= SDL_WINDOW_VULKAN;
    }
    SDL_Window* window = SDL_CreateWindow(std::string(desc.title).c_str(), static_cast<int>(desc.size.width),
                                          static_cast<int>(desc.size.height), flags);
    if (window == nullptr)
    {
        return MakeError(ErrorCode::IoError, SdlError("SDL_CreateWindow failed"));
    }
    return std::unique_ptr<IWindow>(std::make_unique<SdlWindow>(window));
}

namespace
{

/// Keeps SDL's Vulkan library loaded for a scope. SDL's Vulkan entry points must not be called by drivers without
/// Vulkan support (they crash on the "dummy" driver), and loading fails cleanly there.
class ScopedSdlVulkan
{
public:
    ScopedSdlVulkan()
        : m_loaded(SDL_Vulkan_LoadLibrary(nullptr))
    {
    }
    ~ScopedSdlVulkan()
    {
        if (m_loaded)
        {
            SDL_Vulkan_UnloadLibrary();
        }
    }
    ScopedSdlVulkan(const ScopedSdlVulkan&) = delete;
    ScopedSdlVulkan& operator=(const ScopedSdlVulkan&) = delete;
    ScopedSdlVulkan(ScopedSdlVulkan&&) = delete;
    ScopedSdlVulkan& operator=(ScopedSdlVulkan&&) = delete;

    [[nodiscard]] bool Loaded() const noexcept { return m_loaded; }

private:
    bool m_loaded;
};

} // namespace

Result<std::vector<const char*>> SdlPlatform::VulkanInstanceExtensions() const
{
    const ScopedSdlVulkan vulkan;
    if (!vulkan.Loaded())
    {
        return MakeError(ErrorCode::Unsupported, SdlError("no Vulkan support in this video driver"));
    }
    Uint32 count = 0;
    const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count);
    if (names == nullptr)
    {
        return MakeError(ErrorCode::IoError, SdlError("SDL_Vulkan_GetInstanceExtensions failed"));
    }
    return std::vector<const char*>(names, names + count);
}

Result<std::uint64_t> SdlPlatform::CreateVulkanSurface(const IWindow& window, void* instance) const
{
    const ScopedSdlVulkan vulkan;
    if (!vulkan.Loaded())
    {
        return MakeError(ErrorCode::Unsupported, SdlError("no Vulkan support in this video driver"));
    }
    VkSurfaceKHR surface{};
    if (!SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(window.NativeHandle()), static_cast<VkInstance>(instance),
                                  nullptr, &surface))
    {
        return MakeError(ErrorCode::IoError, SdlError("SDL_Vulkan_CreateSurface failed"));
    }
    return reinterpret_cast<std::uint64_t>(surface);
}

void SdlPlatform::HandleGamepadConnection(const SDL_Event& event)
{
    const SDL_JoystickID id = event.gdevice.which;
    if (event.type == SDL_EVENT_GAMEPAD_ADDED)
    {
        if (SDL_Gamepad* gamepad = SDL_OpenGamepad(id))
        {
            m_gamepads.emplace(id, gamepad);
        }
    }
    else if (const auto found = m_gamepads.find(id); found != m_gamepads.end())
    {
        SDL_CloseGamepad(found->second);
        m_gamepads.erase(found);
    }
}

void SdlPlatform::PollEvents(std::vector<InputEvent>& events)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_GAMEPAD_ADDED || event.type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            HandleGamepadConnection(event);
        }
        if (auto translated = TranslateSdlEvent(event))
        {
            events.push_back(*translated);
        }
    }
}

std::string_view SdlPlatform::VideoDriver() const
{
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver != nullptr ? driver : "";
}

std::vector<std::string> SdlPlatform::AvailableVideoDrivers()
{
    std::vector<std::string> drivers;
    const int count = SDL_GetNumVideoDrivers();
    drivers.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int i = 0; i < count; ++i)
    {
        drivers.emplace_back(SDL_GetVideoDriver(i));
    }
    return drivers;
}

} // namespace interi
