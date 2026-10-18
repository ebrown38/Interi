// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/platform/InputEvents.h>
#include <interi/platform/InputMapper.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace interi
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
///
/// With `InputMerge::Replace` an action or axis the file names loses its earlier bindings first, so a project's (or
/// the player's) config rebinds what a module's defaults bound; `Add` keeps them.
enum class InputMerge : std::uint8_t
{
    Add,
    Replace
};
Result<std::vector<std::string>> ApplyInputConfig(std::string_view json, InputMapper& mapper,
                                                  InputMerge merge = InputMerge::Add);

/// The mapper's bindings as an input config (sorted keys), which ApplyInputConfig reads back: what a player's
/// rebinds are saved as.
[[nodiscard]] std::string CaptureInputConfig(const InputMapper& mapper);

/// Name of a bound input ("Space", "Mouse.Left"), as written in config files.
[[nodiscard]] std::string_view InputName(InputMapper::DigitalBinding binding) noexcept;

/// The first button or key pressed in `events` (a key repeat doesn't count), by name: what a rebinding UI waits for.
[[nodiscard]] std::optional<std::string> FirstPressedInput(std::span<const InputEvent> events);

/// Replaces binding `index` of `action` with the input called `inputName` (appends when `index` is past the end).
/// InvalidArgument for an unknown action or input name; the mapper is unchanged on failure.
Result<void> RebindAction(InputMapper& mapper, std::string_view action, std::size_t index, std::string_view inputName);

/// The "press a key" flow of a settings menu. A menu button raises `input.rebind:<action>`; `BeginFromAction` arms
/// the prompt for it, and every frame `Update` takes the frame's events: the first key or button pressed replaces the
/// action's binding `index` (Escape cancels). The game then saves `CaptureInputConfig(mapper)` as the player's config.
class RebindPrompt
{
public:
    enum class Status : std::uint8_t
    {
        Waiting,   ///< Still listening (or not armed).
        Rebound,   ///< `input` now drives the action.
        Cancelled, ///< Escape was pressed.
        Failed     ///< The action is unknown; see `message`.
    };
    struct Outcome
    {
        Status status = Status::Waiting;
        std::string input;   ///< The name bound (Rebound).
        std::string message; ///< Why (Failed).
    };

    static constexpr std::string_view kActionPrefix = "input.rebind:";

    /// Arms the prompt for `action`'s binding `index`.
    void Begin(std::string action, std::size_t index = 0);
    /// Arms it for a menu action "input.rebind:<action>"; false (and nothing changes) for any other string.
    bool BeginFromAction(std::string_view menuAction, std::size_t index = 0);
    void Cancel() noexcept { m_action.clear(); }
    [[nodiscard]] bool Active() const noexcept { return !m_action.empty(); }
    [[nodiscard]] const std::string& Action() const noexcept { return m_action; }

    /// Feed one frame's events (before or after InputMapper::Update). Disarms once it is no longer Waiting.
    Outcome Update(std::span<const InputEvent> events, InputMapper& mapper);

private:
    std::string m_action;
    std::size_t m_index = 0;
};

} // namespace interi
