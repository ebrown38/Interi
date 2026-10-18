// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/platform/InputMapper.h>

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace interi
{

namespace
{

template <typename T>
[[nodiscard]] std::size_t ToIndex(T value) noexcept
{
    return static_cast<std::size_t>(static_cast<std::underlying_type_t<T>>(value));
}

template <typename Entry>
[[nodiscard]] std::uint32_t FindOrAdd(std::vector<Entry>& entries, std::string_view name)
{
    const auto found = std::ranges::find(entries, name, &Entry::name);
    if (found != entries.end())
    {
        return static_cast<std::uint32_t>(found - entries.begin());
    }
    entries.emplace_back().name = std::string(name);
    return static_cast<std::uint32_t>(entries.size() - 1);
}

} // namespace

InputMapper::ActionId InputMapper::AddAction(std::string_view name)
{
    return FindOrAdd(m_actions, name);
}

InputMapper::AxisId InputMapper::AddAxis(std::string_view name)
{
    return FindOrAdd(m_axes, name);
}

std::optional<InputMapper::ActionId> InputMapper::FindAction(std::string_view name) const
{
    for (std::size_t i = 0; i < m_actions.size(); ++i)
    {
        if (m_actions[i].name == name)
        {
            return static_cast<ActionId>(i);
        }
    }
    return std::nullopt;
}

std::optional<InputMapper::AxisId> InputMapper::FindAxis(std::string_view name) const
{
    for (std::size_t i = 0; i < m_axes.size(); ++i)
    {
        if (m_axes[i].name == name)
        {
            return static_cast<AxisId>(i);
        }
    }
    return std::nullopt;
}

std::string_view InputMapper::ActionName(ActionId action) const
{
    INTERI_ASSERT(action < m_actions.size());
    return m_actions[action].name;
}

std::string_view InputMapper::AxisName(AxisId axis) const
{
    INTERI_ASSERT(axis < m_axes.size());
    return m_axes[axis].name;
}

std::span<const InputMapper::DigitalBinding> InputMapper::ActionBindings(ActionId action) const
{
    INTERI_ASSERT(action < m_actions.size());
    return m_actions[action].bindings;
}

std::span<const InputMapper::AxisBinding> InputMapper::AxisBindings(AxisId axis) const
{
    INTERI_ASSERT(axis < m_axes.size());
    return m_axes[axis].bindings;
}

void InputMapper::ClearAction(ActionId action)
{
    INTERI_ASSERT(action < m_actions.size());
    m_actions[action].bindings.clear();
}

void InputMapper::ClearAxis(AxisId axis)
{
    INTERI_ASSERT(axis < m_axes.size());
    m_axes[axis].bindings.clear();
}

void InputMapper::BindKey(ActionId action, Key key)
{
    INTERI_ASSERT(action < m_actions.size());
    m_actions[action].bindings.push_back({Source::Key, static_cast<std::uint16_t>(key)});
}

void InputMapper::BindMouseButton(ActionId action, MouseButton button)
{
    INTERI_ASSERT(action < m_actions.size());
    m_actions[action].bindings.push_back({Source::MouseButton, static_cast<std::uint16_t>(button)});
}

void InputMapper::BindGamepadButton(ActionId action, GamepadButton button)
{
    INTERI_ASSERT(action < m_actions.size());
    m_actions[action].bindings.push_back({Source::GamepadButton, static_cast<std::uint16_t>(button)});
}

void InputMapper::BindGamepadAxis(AxisId axis, GamepadAxis gamepadAxis, float scale)
{
    INTERI_ASSERT(axis < m_axes.size());
    m_axes[axis].bindings.push_back(AxisBinding{.fromKeys = false,
                                                .gamepadAxis = gamepadAxis,
                                                .scale = scale,
                                                .negative = Key::Unknown,
                                                .positive = Key::Unknown});
}

void InputMapper::BindKeys(AxisId axis, Key negative, Key positive)
{
    INTERI_ASSERT(axis < m_axes.size());
    m_axes[axis].bindings.push_back(AxisBinding{.fromKeys = true,
                                                .gamepadAxis = GamepadAxis::LeftX,
                                                .scale = 1.0f,
                                                .negative = negative,
                                                .positive = positive});
}

void InputMapper::Update(std::span<const InputEvent> events)
{
    for (Action& action : m_actions)
    {
        action.pressed = false;
        action.released = false;
    }
    m_mouseDeltaX = 0.0f;
    m_mouseDeltaY = 0.0f;
    m_quitRequested = false;
    for (const InputEvent& event : events)
    {
        Apply(event);
        RefreshActions();
    }
}

void InputMapper::Apply(const InputEvent& event)
{
    if (const auto* key = std::get_if<KeyEvent>(&event))
    {
        if (key->key != Key::Unknown && key->key != Key::Count)
        {
            m_keys.set(ToIndex(key->key), key->down);
        }
    }
    else if (const auto* mouse = std::get_if<MouseButtonEvent>(&event))
    {
        m_mouseButtons.set(ToIndex(mouse->button), mouse->down);
    }
    else if (const auto* motion = std::get_if<MouseMotionEvent>(&event))
    {
        m_mouseDeltaX += motion->deltaX;
        m_mouseDeltaY += motion->deltaY;
    }
    else if (const auto* connection = std::get_if<GamepadConnectionEvent>(&event))
    {
        if (connection->connected)
        {
            (void)FindOrAddGamepad(connection->gamepad);
        }
        else
        {
            std::erase_if(m_gamepads, [&](const Gamepad& pad) { return pad.id == connection->gamepad; });
        }
    }
    else if (const auto* button = std::get_if<GamepadButtonEvent>(&event))
    {
        FindOrAddGamepad(button->gamepad).buttons.set(ToIndex(button->button), button->down);
    }
    else if (const auto* axis = std::get_if<GamepadAxisEvent>(&event))
    {
        FindOrAddGamepad(axis->gamepad).axes[ToIndex(axis->axis)] = axis->value;
    }
    else if (std::holds_alternative<QuitEvent>(event))
    {
        m_quitRequested = true;
    }
}

void InputMapper::RefreshActions()
{
    for (Action& action : m_actions)
    {
        const bool down =
            std::ranges::any_of(action.bindings, [this](const DigitalBinding& b) { return IsBindingDown(b); });
        action.pressed = action.pressed || (down && !action.down);
        action.released = action.released || (!down && action.down);
        action.down = down;
    }
}

bool InputMapper::IsBindingDown(const DigitalBinding& binding) const
{
    switch (binding.source)
    {
    case Source::Key:
        return m_keys.test(binding.code);
    case Source::MouseButton:
        return m_mouseButtons.test(binding.code);
    case Source::GamepadButton:
        return std::ranges::any_of(m_gamepads, [&](const Gamepad& pad) { return pad.buttons.test(binding.code); });
    }
    return false;
}

float InputMapper::GamepadAxisValue(GamepadAxis axis) const
{
    // The pad pushed furthest wins, so a resting second pad doesn't cancel the active one.
    float strongest = 0.0f;
    for (const Gamepad& pad : m_gamepads)
    {
        const float value = pad.axes[ToIndex(axis)];
        if (std::fabs(value) > std::fabs(strongest))
        {
            strongest = value;
        }
    }
    const float magnitude = std::fabs(strongest);
    if (magnitude <= m_deadZone)
    {
        return 0.0f;
    }
    const float rescaled = std::min(1.0f, (magnitude - m_deadZone) / (1.0f - m_deadZone));
    return std::copysign(rescaled, strongest);
}

InputMapper::Gamepad& InputMapper::FindOrAddGamepad(GamepadId id)
{
    const auto found = std::ranges::find(m_gamepads, id, &Gamepad::id);
    if (found != m_gamepads.end())
    {
        return *found;
    }
    return m_gamepads.emplace_back(Gamepad{.id = id, .buttons = {}, .axes = {}});
}

bool InputMapper::IsDown(ActionId action) const
{
    INTERI_ASSERT(action < m_actions.size());
    return m_actions[action].down;
}

bool InputMapper::WasPressed(ActionId action) const
{
    INTERI_ASSERT(action < m_actions.size());
    return m_actions[action].pressed;
}

bool InputMapper::WasReleased(ActionId action) const
{
    INTERI_ASSERT(action < m_actions.size());
    return m_actions[action].released;
}

float InputMapper::AxisValue(AxisId axis) const
{
    INTERI_ASSERT(axis < m_axes.size());
    float sum = 0.0f;
    for (const AxisBinding& binding : m_axes[axis].bindings)
    {
        if (binding.fromKeys)
        {
            sum += (m_keys.test(ToIndex(binding.positive)) ? 1.0f : 0.0f) -
                   (m_keys.test(ToIndex(binding.negative)) ? 1.0f : 0.0f);
        }
        else
        {
            sum += binding.scale * GamepadAxisValue(binding.gamepadAxis);
        }
    }
    return std::clamp(sum, -1.0f, 1.0f);
}

} // namespace interi
