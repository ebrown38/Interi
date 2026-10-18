// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/platform/InputConfig.h>

#include <nlohmann/json.hpp>

#include <array>
#include <format>
#include <utility>
#include <variant>

namespace interi
{

namespace
{

using Json = nlohmann::json;

// Names in enumerator order (static_asserts keep them in step with InputEvents.h).
constexpr std::array<std::string_view, static_cast<std::size_t>(Key::Count)> kKeyNames = {
    "Unknown",     "A",
    "B",           "C",
    "D",           "E",
    "F",           "G",
    "H",           "I",
    "J",           "K",
    "L",           "M",
    "N",           "O",
    "P",           "Q",
    "R",           "S",
    "T",           "U",
    "V",           "W",
    "X",           "Y",
    "Z",           "Num0",
    "Num1",        "Num2",
    "Num3",        "Num4",
    "Num5",        "Num6",
    "Num7",        "Num8",
    "Num9",        "F1",
    "F2",          "F3",
    "F4",          "F5",
    "F6",          "F7",
    "F8",          "F9",
    "F10",         "F11",
    "F12",         "Space",
    "Enter",       "Escape",
    "Tab",         "Backspace",
    "Delete",      "Insert",
    "Home",        "End",
    "PageUp",      "PageDown",
    "Left",        "Right",
    "Up",          "Down",
    "LeftShift",   "RightShift",
    "LeftCtrl",    "RightCtrl",
    "LeftAlt",     "RightAlt",
    "Minus",       "Equals",
    "LeftBracket", "RightBracket",
    "Backslash",   "Semicolon",
    "Apostrophe",  "Grave",
    "Comma",       "Period",
    "Slash",
};
constexpr std::array<std::string_view, static_cast<std::size_t>(MouseButton::Count)> kMouseNames = {
    "Mouse.Left", "Mouse.Right", "Mouse.Middle", "Mouse.X1", "Mouse.X2",
};
constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadButton::Count)> kGamepadButtonNames = {
    "Gamepad.South",         "Gamepad.East",   "Gamepad.West",      "Gamepad.North",      "Gamepad.Back",
    "Gamepad.Guide",         "Gamepad.Start",  "Gamepad.LeftStick", "Gamepad.RightStick", "Gamepad.LeftShoulder",
    "Gamepad.RightShoulder", "Gamepad.DpadUp", "Gamepad.DpadDown",  "Gamepad.DpadLeft",   "Gamepad.DpadRight",
};
constexpr std::array<std::string_view, static_cast<std::size_t>(GamepadAxis::Count)> kGamepadAxisNames = {
    "Gamepad.LeftX", "Gamepad.LeftY", "Gamepad.RightX", "Gamepad.RightY", "Gamepad.LeftTrigger", "Gamepad.RightTrigger",
};
static_assert(kKeyNames.back() == "Slash" && kGamepadButtonNames.back() == "Gamepad.DpadRight");

template <typename Enum, std::size_t N>
std::optional<Enum> FromName(const std::array<std::string_view, N>& names, std::string_view name) noexcept
{
    for (std::size_t i = 0; i < N; ++i)
    {
        if (names[i] == name)
        {
            return static_cast<Enum>(i);
        }
    }
    return std::nullopt;
}

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

using DigitalInput = std::variant<Key, MouseButton, GamepadButton>;

Result<DigitalInput> ParseDigital(const Json& value, std::string_view action)
{
    if (!value.is_string())
    {
        return Invalid(std::format("action '{}': bindings must be input names", action));
    }
    const std::string name = value.get<std::string>();
    if (const std::optional<Key> key = KeyFromName(name); key && *key != Key::Unknown)
    {
        return *key;
    }
    if (const std::optional<MouseButton> button = MouseButtonFromName(name))
    {
        return *button;
    }
    if (const std::optional<GamepadButton> button = GamepadButtonFromName(name))
    {
        return *button;
    }
    return Invalid(std::format("action '{}': unknown input \"{}\"", action, name));
}

struct AxisConfig
{
    std::string name;
    std::optional<std::pair<Key, Key>> keys;
    std::vector<std::pair<GamepadAxis, float>> gamepad;
};

Result<AxisConfig> ParseAxis(const std::string& name, const Json& value, std::vector<std::string>& warnings)
{
    if (!value.is_object())
    {
        return Invalid(std::format("axis '{}' is not an object", name));
    }
    AxisConfig axis{.name = name, .keys = std::nullopt, .gamepad = {}};
    for (const auto& [key, field] : value.items())
    {
        if (key == "keys")
        {
            if (!field.is_array() || field.size() != 2 || !field[0].is_string() || !field[1].is_string())
            {
                return Invalid(std::format("axis '{}': \"keys\" must be [negative, positive] key names", name));
            }
            const std::optional<Key> negative = KeyFromName(field[0].get<std::string>());
            const std::optional<Key> positive = KeyFromName(field[1].get<std::string>());
            if (!negative || !positive || *negative == Key::Unknown || *positive == Key::Unknown)
            {
                return Invalid(std::format("axis '{}': unknown key in \"keys\"", name));
            }
            axis.keys = std::pair(*negative, *positive);
        }
        else if (key == "gamepad")
        {
            if (!field.is_array())
            {
                return Invalid(std::format("axis '{}': \"gamepad\" must be an array", name));
            }
            for (const Json& binding : field)
            {
                const auto axisName = binding.is_object() ? binding.find("axis") : binding.end();
                const std::optional<GamepadAxis> gamepadAxis =
                    binding.is_object() && axisName != binding.end() && axisName->is_string()
                        ? GamepadAxisFromName(axisName->get<std::string>())
                        : std::nullopt;
                if (!gamepadAxis)
                {
                    return Invalid(std::format("axis '{}': a gamepad binding needs a known \"axis\"", name));
                }
                float scale = 1.0f;
                if (const auto it = binding.find("scale"); it != binding.end())
                {
                    if (!it->is_number())
                    {
                        return Invalid(std::format("axis '{}': \"scale\" is not a number", name));
                    }
                    scale = it->get<float>();
                }
                axis.gamepad.emplace_back(*gamepadAxis, scale);
            }
        }
        else
        {
            warnings.push_back(std::format("axis '{}': unknown field \"{}\" ignored", name, key));
        }
    }
    return axis;
}

} // namespace

std::optional<Key> KeyFromName(std::string_view name) noexcept
{
    return FromName<Key>(kKeyNames, name);
}

std::optional<MouseButton> MouseButtonFromName(std::string_view name) noexcept
{
    return FromName<MouseButton>(kMouseNames, name);
}

std::optional<GamepadButton> GamepadButtonFromName(std::string_view name) noexcept
{
    return FromName<GamepadButton>(kGamepadButtonNames, name);
}

std::optional<GamepadAxis> GamepadAxisFromName(std::string_view name) noexcept
{
    return FromName<GamepadAxis>(kGamepadAxisNames, name);
}

Result<std::vector<std::string>> ApplyInputConfig(std::string_view json, InputMapper& mapper, InputMerge merge)
{
    const Json root = Json::parse(json, nullptr, false);
    if (root.is_discarded() || !root.is_object())
    {
        return Invalid("input config is not a JSON object");
    }
    std::vector<std::string> warnings;
    std::optional<float> deadZone;
    std::vector<std::pair<std::string, std::vector<DigitalInput>>> actions;
    std::vector<AxisConfig> axes;
    for (const auto& [key, value] : root.items())
    {
        if (key == "deadZone")
        {
            if (!value.is_number() || value.get<float>() < 0.0f || value.get<float>() >= 1.0f)
            {
                return Invalid("\"deadZone\" must be a number in [0, 1)");
            }
            deadZone = value.get<float>();
        }
        else if (key == "actions")
        {
            if (!value.is_object())
            {
                return Invalid("\"actions\" must be an object");
            }
            for (const auto& [action, bindings] : value.items())
            {
                if (!bindings.is_array())
                {
                    return Invalid(std::format("action '{}': bindings must be an array", action));
                }
                std::vector<DigitalInput> inputs;
                for (const Json& binding : bindings)
                {
                    INTERI_TRY_ASSIGN(input, ParseDigital(binding, action));
                    inputs.push_back(input);
                }
                actions.emplace_back(action, std::move(inputs));
            }
        }
        else if (key == "axes")
        {
            if (!value.is_object())
            {
                return Invalid("\"axes\" must be an object");
            }
            for (const auto& [name, axis] : value.items())
            {
                INTERI_TRY_ASSIGN(parsed, ParseAxis(name, axis, warnings));
                axes.push_back(std::move(parsed));
            }
        }
        else
        {
            warnings.push_back(std::format("input config: unknown field \"{}\" ignored", key));
        }
    }

    // Everything parsed: bind.
    if (deadZone)
    {
        mapper.SetDeadZone(*deadZone);
    }
    for (const auto& [name, inputs] : actions)
    {
        const InputMapper::ActionId action = mapper.AddAction(name);
        if (merge == InputMerge::Replace)
        {
            mapper.ClearAction(action);
        }
        for (const DigitalInput& input : inputs)
        {
            if (const auto* key = std::get_if<Key>(&input))
            {
                mapper.BindKey(action, *key);
            }
            else if (const auto* mouse = std::get_if<MouseButton>(&input))
            {
                mapper.BindMouseButton(action, *mouse);
            }
            else
            {
                mapper.BindGamepadButton(action, std::get<GamepadButton>(input));
            }
        }
    }
    for (const AxisConfig& config : axes)
    {
        const InputMapper::AxisId axis = mapper.AddAxis(config.name);
        if (merge == InputMerge::Replace)
        {
            mapper.ClearAxis(axis);
        }
        if (config.keys)
        {
            mapper.BindKeys(axis, config.keys->first, config.keys->second);
        }
        for (const auto& [gamepadAxis, scale] : config.gamepad)
        {
            mapper.BindGamepadAxis(axis, gamepadAxis, scale);
        }
    }
    for (const std::string& warning : warnings)
    {
        INTERI_LOG_WARN("Input", "{}", warning);
    }
    return warnings;
}

std::string_view InputName(InputMapper::DigitalBinding binding) noexcept
{
    switch (binding.source)
    {
    case InputMapper::Source::Key:
        return kKeyNames[binding.code];
    case InputMapper::Source::MouseButton:
        return kMouseNames[binding.code];
    case InputMapper::Source::GamepadButton:
        return kGamepadButtonNames[binding.code];
    }
    return {};
}

std::string CaptureInputConfig(const InputMapper& mapper)
{
    Json root = Json::object();
    root["deadZone"] = mapper.DeadZone();
    Json actions = Json::object();
    for (std::size_t i = 0; i < mapper.ActionCount(); ++i)
    {
        const auto id = static_cast<InputMapper::ActionId>(i);
        Json bindings = Json::array();
        for (const InputMapper::DigitalBinding& binding : mapper.ActionBindings(id))
        {
            bindings.push_back(std::string(InputName(binding)));
        }
        actions[std::string(mapper.ActionName(id))] = std::move(bindings);
    }
    root["actions"] = std::move(actions);
    Json axes = Json::object();
    for (std::size_t i = 0; i < mapper.AxisCount(); ++i)
    {
        const auto id = static_cast<InputMapper::AxisId>(i);
        Json axis = Json::object();
        Json gamepad = Json::array();
        for (const InputMapper::AxisBinding& binding : mapper.AxisBindings(id))
        {
            if (binding.fromKeys)
            {
                axis["keys"] = Json::array({std::string(kKeyNames[static_cast<std::size_t>(binding.negative)]),
                                            std::string(kKeyNames[static_cast<std::size_t>(binding.positive)])});
            }
            else
            {
                Json entry = Json::object();
                entry["axis"] = std::string(kGamepadAxisNames[static_cast<std::size_t>(binding.gamepadAxis)]);
                entry["scale"] = binding.scale;
                gamepad.push_back(std::move(entry));
            }
        }
        if (!gamepad.empty())
        {
            axis["gamepad"] = std::move(gamepad);
        }
        axes[std::string(mapper.AxisName(id))] = std::move(axis);
    }
    root["axes"] = std::move(axes);
    return root.dump(2);
}

std::optional<std::string> FirstPressedInput(std::span<const InputEvent> events)
{
    for (const InputEvent& event : events)
    {
        if (const auto* key = std::get_if<KeyEvent>(&event))
        {
            if (key->down && !key->repeat && key->key != Key::Unknown)
            {
                return std::string(kKeyNames[static_cast<std::size_t>(key->key)]);
            }
        }
        else if (const auto* mouse = std::get_if<MouseButtonEvent>(&event))
        {
            if (mouse->down)
            {
                return std::string(kMouseNames[static_cast<std::size_t>(mouse->button)]);
            }
        }
        else if (const auto* pad = std::get_if<GamepadButtonEvent>(&event))
        {
            if (pad->down)
            {
                return std::string(kGamepadButtonNames[static_cast<std::size_t>(pad->button)]);
            }
        }
    }
    return std::nullopt;
}

Result<void> RebindAction(InputMapper& mapper, std::string_view action, std::size_t index, std::string_view inputName)
{
    const std::optional<InputMapper::ActionId> id = mapper.FindAction(action);
    if (!id)
    {
        return Invalid(std::format("unknown action '{}'", action));
    }
    const Json name = std::string(inputName);
    INTERI_TRY_ASSIGN(input, ParseDigital(name, action));
    // Rebuild the action's bindings with `index` replaced (the mapper only appends).
    std::vector<DigitalInput> inputs;
    for (const InputMapper::DigitalBinding& binding : mapper.ActionBindings(*id))
    {
        switch (binding.source)
        {
        case InputMapper::Source::Key:
            inputs.emplace_back(static_cast<Key>(binding.code));
            break;
        case InputMapper::Source::MouseButton:
            inputs.emplace_back(static_cast<MouseButton>(binding.code));
            break;
        case InputMapper::Source::GamepadButton:
            inputs.emplace_back(static_cast<GamepadButton>(binding.code));
            break;
        }
    }
    if (index < inputs.size())
    {
        inputs[index] = input;
    }
    else
    {
        inputs.push_back(input);
    }
    mapper.ClearAction(*id);
    for (const DigitalInput& entry : inputs)
    {
        if (const auto* key = std::get_if<Key>(&entry))
        {
            mapper.BindKey(*id, *key);
        }
        else if (const auto* mouse = std::get_if<MouseButton>(&entry))
        {
            mapper.BindMouseButton(*id, *mouse);
        }
        else
        {
            mapper.BindGamepadButton(*id, std::get<GamepadButton>(entry));
        }
    }
    return {};
}

void RebindPrompt::Begin(std::string action, std::size_t index)
{
    m_action = std::move(action);
    m_index = index;
}

bool RebindPrompt::BeginFromAction(std::string_view menuAction, std::size_t index)
{
    if (!menuAction.starts_with(kActionPrefix) || menuAction.size() == kActionPrefix.size())
    {
        return false;
    }
    Begin(std::string(menuAction.substr(kActionPrefix.size())), index);
    return true;
}

RebindPrompt::Outcome RebindPrompt::Update(std::span<const InputEvent> events, InputMapper& mapper)
{
    if (!Active())
    {
        return {};
    }
    const std::optional<std::string> pressed = FirstPressedInput(events);
    if (!pressed)
    {
        return {};
    }
    const std::string action = std::exchange(m_action, {});
    if (*pressed == "Escape")
    {
        return {.status = Status::Cancelled, .input = {}, .message = {}};
    }
    if (auto done = RebindAction(mapper, action, m_index, *pressed); !done)
    {
        return {.status = Status::Failed, .input = {}, .message = done.error().message};
    }
    return {.status = Status::Rebound, .input = *pressed, .message = {}};
}

} // namespace interi
