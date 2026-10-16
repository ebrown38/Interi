// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/platform/InputEvents.h>
#include <interi/platform/InputMapper.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::gameplay
{

/// Input names as written in input config files: keys by their Key enumerator ("A", "Space", "LeftShift", "Num1"),
/// "Mouse.Left", "Gamepad.South", and axes "Gamepad.LeftX".
[[nodiscard]] std::optional<Key> KeyFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<MouseButton> MouseButtonFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<GamepadButton> GamepadButtonFromName(std::string_view name) noexcept;
[[nodiscard]] std::optional<GamepadAxis> GamepadAxisFromName(std::string_view name) noexcept;

/// Binds actions and axes from an input config file into `mapper`:
///
///     { "deadZone": 0.15,
///       "actions": { "jump": ["Space", "Gamepad.South"], "fire": ["Mouse.Left", "Gamepad.RightShoulder"] },
///       "axes": { "moveX": { "keys": ["A", "D"], "gamepad": [ { "axis": "Gamepad.LeftX", "scale": 1 } ] } } }
///
/// `keys` is a [negative, positive] pair. Nothing is bound unless the whole file is valid: InvalidArgument for
/// malformed JSON, wrong types or an unknown input name. Returns warnings (unknown fields), each also logged.
Result<std::vector<std::string>> ApplyInputConfig(std::string_view json, InputMapper& mapper);

} // namespace interi::gameplay
