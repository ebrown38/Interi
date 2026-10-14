// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

namespace interi
{

/// Physical keyboard keys (layout-independent positions, named after the US layout).
// clang-format off
enum class Key : std::uint8_t
{
    Unknown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    Space, Enter, Escape, Tab, Backspace, Delete, Insert, Home, End, PageUp, PageDown,
    Left, Right, Up, Down,
    LeftShift, RightShift, LeftCtrl, RightCtrl, LeftAlt, RightAlt,
    Minus, Equals, LeftBracket, RightBracket, Backslash, Semicolon, Apostrophe, Grave, Comma, Period, Slash,
    Count
};
// clang-format on

enum class MouseButton : std::uint8_t
{
    Left,
    Right,
    Middle,
    X1,
    X2,
    Count
};

/// Face buttons are named by position (South = Xbox A / PlayStation Cross).
enum class GamepadButton : std::uint8_t
{
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Count
};

/// Sticks report [-1, 1] (+Y = down, as SDL reports it); triggers report [0, 1].
enum class GamepadAxis : std::uint8_t
{
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count
};

inline constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);
inline constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);
inline constexpr std::size_t kGamepadButtonCount = static_cast<std::size_t>(GamepadButton::Count);
inline constexpr std::size_t kGamepadAxisCount = static_cast<std::size_t>(GamepadAxis::Count);

/// Identifies one connected gamepad for as long as it stays connected.
using GamepadId = std::uint32_t;

struct KeyEvent
{
    Key key;
    bool down;
    bool repeat; ///< OS key repeat while held; ignored by InputMapper.
};

struct MouseButtonEvent
{
    MouseButton button;
    bool down;
};

/// Position in window pixels; delta is relative motion since the previous motion event.
struct MouseMotionEvent
{
    float x;
    float y;
    float deltaX;
    float deltaY;
};

struct MouseWheelEvent
{
    float deltaX;
    float deltaY;
};

struct GamepadConnectionEvent
{
    GamepadId gamepad;
    bool connected;
};

struct GamepadButtonEvent
{
    GamepadId gamepad;
    GamepadButton button;
    bool down;
};

struct GamepadAxisEvent
{
    GamepadId gamepad;
    GamepadAxis axis;
    float value;
};

struct WindowResizedEvent
{
    std::uint32_t width;
    std::uint32_t height;
};

/// The user asked to close the window or quit the application.
struct QuitEvent
{
};

using InputEvent = std::variant<KeyEvent, MouseButtonEvent, MouseMotionEvent, MouseWheelEvent, GamepadConnectionEvent,
                                GamepadButtonEvent, GamepadAxisEvent, WindowResizedEvent, QuitEvent>;

/// Source of platform events (an OS event queue, or a fake in tests).
class IEventSource
{
public:
    IEventSource() = default;
    IEventSource(const IEventSource&) = delete;
    IEventSource& operator=(const IEventSource&) = delete;
    IEventSource(IEventSource&&) = delete;
    IEventSource& operator=(IEventSource&&) = delete;
    virtual ~IEventSource() = default;

    /// Appends every event that arrived since the previous call to `events`, oldest first. Main thread only.
    virtual void PollEvents(std::vector<InputEvent>& events) = 0;
};

} // namespace interi
