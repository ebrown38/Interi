// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/gameplay/InputConfig.h>

#include <nlohmann/json.hpp>

#include <array>
#include <format>
#include <variant>

namespace interi::gameplay
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

Result<std::vector<std::string>> ApplyInputConfig(std::string_view json, InputMapper& mapper)
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
        INTERI_LOG_WARN("Gameplay", "{}", warning);
    }
    return warnings;
}

} // namespace interi::gameplay
