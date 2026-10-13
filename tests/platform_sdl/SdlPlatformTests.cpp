// SPDX-License-Identifier: MIT
#include <interi/platform/InputMapper.h>
#include <interi/platform_sdl/SdlPlatform.h>

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
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
