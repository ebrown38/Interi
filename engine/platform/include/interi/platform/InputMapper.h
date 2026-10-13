// SPDX-License-Identifier: MIT
#pragma once

#include <interi/platform/InputEvents.h>

#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi
{

/// Maps raw keyboard, mouse and gamepad input to named game actions (digital) and axes (analog).
///
/// Per frame: call Update with that frame's events, then query. An action is down while any of its bound inputs is
/// down; WasPressed/WasReleased report the edges that happened during the last Update (a press and release within
/// one frame reports both). Input from every connected gamepad is merged. Not thread-safe.
class InputMapper
{
public:
    using ActionId = std::uint32_t;
    using AxisId = std::uint32_t;

    /// Registers an action, or returns the existing id for `name`.
    ActionId AddAction(std::string_view name);
    /// Registers an axis, or returns the existing id for `name`.
    AxisId AddAxis(std::string_view name);

    void BindKey(ActionId action, Key key);
    void BindMouseButton(ActionId action, MouseButton button);
    void BindGamepadButton(ActionId action, GamepadButton button);
    /// Adds `scale * value` of a gamepad axis (after the dead zone) to the axis.
    void BindGamepadAxis(AxisId axis, GamepadAxis gamepadAxis, float scale = 1.0f);
    /// Adds -1 while `negative` is held and +1 while `positive` is held.
    void BindKeys(AxisId axis, Key negative, Key positive);

    /// Stick and trigger values with magnitude below this read as 0; above it they are rescaled to keep the full
    /// range. Default 0.15.
    void SetDeadZone(float deadZone) noexcept { m_deadZone = deadZone; }

    /// Starts a new frame and applies `events` in order.
    void Update(std::span<const InputEvent> events);

    [[nodiscard]] bool IsDown(ActionId action) const;
    [[nodiscard]] bool WasPressed(ActionId action) const;
    [[nodiscard]] bool WasReleased(ActionId action) const;
    /// Sum of the axis' bindings, clamped to [-1, 1].
    [[nodiscard]] float AxisValue(AxisId axis) const;

    /// Mouse motion accumulated during the last Update, in pixels.
    [[nodiscard]] float MouseDeltaX() const noexcept { return m_mouseDeltaX; }
    [[nodiscard]] float MouseDeltaY() const noexcept { return m_mouseDeltaY; }
    /// True once a QuitEvent was seen in the last Update.
    [[nodiscard]] bool QuitRequested() const noexcept { return m_quitRequested; }

private:
    enum class Source : std::uint8_t
    {
        Key,
        MouseButton,
        GamepadButton
    };

    struct DigitalBinding
    {
        Source source;
        std::uint16_t code;
    };

    struct Action
    {
        std::string name;
        std::vector<DigitalBinding> bindings;
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    struct AxisBinding
    {
        bool fromKeys;
        GamepadAxis gamepadAxis;
        float scale;
        Key negative;
        Key positive;
    };

    struct Axis
    {
        std::string name;
        std::vector<AxisBinding> bindings;
    };

    struct Gamepad
    {
        GamepadId id;
        std::bitset<kGamepadButtonCount> buttons;
        std::array<float, kGamepadAxisCount> axes{};
    };

    void Apply(const InputEvent& event);
    void RefreshActions();
    [[nodiscard]] bool IsBindingDown(const DigitalBinding& binding) const;
    [[nodiscard]] float GamepadAxisValue(GamepadAxis axis) const;
    Gamepad& FindOrAddGamepad(GamepadId id);

    std::vector<Action> m_actions;
    std::vector<Axis> m_axes;
    std::vector<Gamepad> m_gamepads;
    std::bitset<kKeyCount> m_keys;
    std::bitset<kMouseButtonCount> m_mouseButtons;
    float m_deadZone = 0.15f;
    float m_mouseDeltaX = 0.0f;
    float m_mouseDeltaY = 0.0f;
    bool m_quitRequested = false;
};

} // namespace interi
