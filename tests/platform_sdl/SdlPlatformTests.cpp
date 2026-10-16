// SPDX-License-Identifier: MIT
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using interi::ErrorCode;
using interi::Key;
using interi::SdlPlatform;
using interi::SdlPlatformDesc;

namespace
{

SDL_Event MakeEvent(SDL_EventType type)
{
    SDL_Event event{};
    event.type = type;
    return event;
}

} // namespace

TEST(SdlTranslation, Scancodes_MapToKeys)
{
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_A), Key::A);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_Z), Key::Z);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_1), Key::Num1);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_9), Key::Num9);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_0), Key::Num0);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_F1), Key::F1);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_F12), Key::F12);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_SPACE), Key::Space);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_LSHIFT), Key::LeftShift);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_SLASH), Key::Slash);
    EXPECT_EQ(interi::KeyFromSdlScancode(SDL_SCANCODE_KP_ENTER), Key::Unknown);
}

TEST(SdlTranslation, KeyEvent_Translated)
{
    SDL_Event event = MakeEvent(SDL_EVENT_KEY_DOWN);
    event.key.scancode = SDL_SCANCODE_W;
    event.key.repeat = true;
    const auto translated = interi::TranslateSdlEvent(event);
    ASSERT_TRUE(translated.has_value());
    const auto& key = std::get<interi::KeyEvent>(*translated);
    EXPECT_EQ(key.key, Key::W);
    EXPECT_TRUE(key.down);
    EXPECT_TRUE(key.repeat);
}

TEST(SdlTranslation, GamepadAxis_Normalized)
{
    SDL_Event event = MakeEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
    event.gaxis.which = 7;
    event.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTX;
    event.gaxis.value = -32768;
    auto translated = interi::TranslateSdlEvent(event);
    ASSERT_TRUE(translated.has_value());
    EXPECT_EQ(std::get<interi::GamepadAxisEvent>(*translated).value, -1.0f);
    EXPECT_EQ(std::get<interi::GamepadAxisEvent>(*translated).gamepad, 7u);

    event.gaxis.axis = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    event.gaxis.value = 32767;
    translated = interi::TranslateSdlEvent(event);
    ASSERT_TRUE(translated.has_value());
    EXPECT_EQ(std::get<interi::GamepadAxisEvent>(*translated).axis, interi::GamepadAxis::RightTrigger);
    EXPECT_EQ(std::get<interi::GamepadAxisEvent>(*translated).value, 1.0f);
}

TEST(SdlTranslation, MouseAndWindowEvents_Translated)
{
    SDL_Event button = MakeEvent(SDL_EVENT_MOUSE_BUTTON_UP);
    button.button.button = SDL_BUTTON_RIGHT;
    const auto translatedButton = interi::TranslateSdlEvent(button);
    ASSERT_TRUE(translatedButton.has_value());
    EXPECT_EQ(std::get<interi::MouseButtonEvent>(*translatedButton).button, interi::MouseButton::Right);
    EXPECT_FALSE(std::get<interi::MouseButtonEvent>(*translatedButton).down);

    SDL_Event resize = MakeEvent(SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED);
    resize.window.data1 = 1920;
    resize.window.data2 = 1080;
    const auto translatedResize = interi::TranslateSdlEvent(resize);
    ASSERT_TRUE(translatedResize.has_value());
    EXPECT_EQ(std::get<interi::WindowResizedEvent>(*translatedResize).width, 1920u);

    EXPECT_TRUE(std::holds_alternative<interi::QuitEvent>(*interi::TranslateSdlEvent(MakeEvent(SDL_EVENT_QUIT))));
    EXPECT_FALSE(interi::TranslateSdlEvent(MakeEvent(SDL_EVENT_CLIPBOARD_UPDATE)).has_value());
}

TEST(SdlTranslation, TextInput_TranslatedAndCutAtCharacterBoundary)
{
    SDL_Event text = MakeEvent(SDL_EVENT_TEXT_INPUT);
    text.text.text = "h\xC3\xA9";
    const auto translated = interi::TranslateSdlEvent(text);
    ASSERT_TRUE(translated.has_value());
    EXPECT_STREQ(std::get<interi::TextInputEvent>(*translated).text.data(), "h\xC3\xA9");

    // 30 ASCII bytes then a 2-byte character: 32 bytes do not fit with the terminator, so the character is dropped.
    const std::string longText = std::string(30, 'a') + "\xC3\xA9";
    EXPECT_EQ(std::string(interi::TranslateText(longText.c_str()).text.data()), std::string(30, 'a'));
    EXPECT_STREQ(interi::TranslateText(nullptr).text.data(), "");
}

#if defined(__linux__)
TEST(SdlPlatform, LinuxBuild_HasDesktopVideoDriver)
{
    const std::vector<std::string> drivers = SdlPlatform::AvailableVideoDrivers();
    const bool desktop =
        std::ranges::find(drivers, "x11") != drivers.end() || std::ranges::find(drivers, "wayland") != drivers.end();
    EXPECT_TRUE(desktop) << "SDL was built without x11 and wayland support";
}
#endif

// Real SDL on the headless dummy driver: create a window, push an event through SDL's queue, map it to an action.
TEST(SdlPlatform, DummyDriver_WindowAndEventsWork)
{
    auto platform = SdlPlatform::Create(SdlPlatformDesc{.videoDriver = "dummy"});
    ASSERT_TRUE(platform.has_value()) << platform.error().message;
    EXPECT_EQ((*platform)->VideoDriver(), "dummy");
    EXPECT_EQ(SdlPlatform::Create().error().code, ErrorCode::Unsupported); // only one at a time

    {
        auto window = (*platform)->OpenWindow(interi::WindowDesc{.title = "test", .size = {320, 240}});
        ASSERT_TRUE(window.has_value()) << window.error().message;
        EXPECT_EQ((*window)->Size(), (interi::WindowExtent{320, 240}));
        EXPECT_NE((*window)->NativeHandle(), nullptr);
        (*window)->SetTitle("renamed");
    }

    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_SPACE;
    ASSERT_TRUE(SDL_PushEvent(&key));
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    ASSERT_TRUE(SDL_PushEvent(&quit));

    interi::InputMapper mapper;
    const auto jump = mapper.AddAction("jump");
    mapper.BindKey(jump, Key::Space);
    std::vector<interi::InputEvent> events;
    (*platform)->PollEvents(events);
    mapper.Update(events);
    EXPECT_TRUE(mapper.WasPressed(jump));
    EXPECT_TRUE(mapper.QuitRequested());
}

TEST(SdlTranslation, EveryKey_ReachableFromExactlyOneScancode)
{
    std::vector<int> hits(interi::kKeyCount, 0);
    for (int scancode = 0; scancode < SDL_SCANCODE_COUNT; ++scancode)
    {
        ++hits[static_cast<std::size_t>(interi::KeyFromSdlScancode(scancode))];
    }
    for (std::size_t key = 1; key < interi::kKeyCount; ++key) // skip Key::Unknown
    {
        EXPECT_EQ(hits[key], 1) << "key " << key;
    }
}

TEST(SdlTranslation, EveryGamepadButtonAndAxis_Translated)
{
    std::vector<int> buttons(interi::kGamepadButtonCount, 0);
    for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
    {
        SDL_Event event = MakeEvent(SDL_EVENT_GAMEPAD_BUTTON_DOWN);
        event.gbutton.button = static_cast<std::uint8_t>(button);
        if (const auto translated = interi::TranslateSdlEvent(event))
        {
            ++buttons[static_cast<std::size_t>(std::get<interi::GamepadButtonEvent>(*translated).button)];
        }
    }
    EXPECT_TRUE(std::ranges::all_of(buttons, [](int count) { return count == 1; }));

    std::vector<int> axes(interi::kGamepadAxisCount, 0);
    for (int axis = 0; axis < SDL_GAMEPAD_AXIS_COUNT; ++axis)
    {
        SDL_Event event = MakeEvent(SDL_EVENT_GAMEPAD_AXIS_MOTION);
        event.gaxis.axis = static_cast<std::uint8_t>(axis);
        if (const auto translated = interi::TranslateSdlEvent(event))
        {
            ++axes[static_cast<std::size_t>(std::get<interi::GamepadAxisEvent>(*translated).axis)];
        }
    }
    EXPECT_TRUE(std::ranges::all_of(axes, [](int count) { return count == 1; }));

    std::vector<int> mouse(interi::kMouseButtonCount, 0);
    for (int button = 0; button <= 8; ++button)
    {
        SDL_Event event = MakeEvent(SDL_EVENT_MOUSE_BUTTON_DOWN);
        event.button.button = static_cast<std::uint8_t>(button);
        if (const auto translated = interi::TranslateSdlEvent(event))
        {
            ++mouse[static_cast<std::size_t>(std::get<interi::MouseButtonEvent>(*translated).button)];
        }
    }
    EXPECT_TRUE(std::ranges::all_of(mouse, [](int count) { return count == 1; }));
}

// A video driver without Vulkan support ("dummy") must report an error rather than crash: callers try Vulkan first
// and fall back to running without rendering.
TEST(SdlPlatform, DummyDriver_VulkanUnavailable_ReturnsErrors)
{
    auto platform = SdlPlatform::Create(SdlPlatformDesc{.videoDriver = "dummy"});
    ASSERT_TRUE(platform.has_value()) << platform.error().message;
    EXPECT_FALSE((*platform)->OpenWindow(interi::WindowDesc{.title = "vk", .vulkan = true}).has_value());
    auto window = (*platform)->OpenWindow(interi::WindowDesc{.title = "plain"});
    ASSERT_TRUE(window.has_value());

    const auto extensions = (*platform)->VulkanInstanceExtensions();
    ASSERT_FALSE(extensions.has_value());
    EXPECT_EQ(extensions.error().code, ErrorCode::Unsupported);
    const auto surface = (*platform)->CreateVulkanSurface(**window, nullptr);
    ASSERT_FALSE(surface.has_value());
    EXPECT_EQ(surface.error().code, ErrorCode::Unsupported);
}
