// SPDX-License-Identifier: MIT
#include <interi/platform/InputConfig.h>

#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <vector>

using interi::ApplyInputConfig;
using interi::InputEvent;
using interi::InputMapper;
using interi::Key;
using interi::KeyEvent;

namespace
{

constexpr const char* kConfig = R"({
  "deadZone": 0.2,
  "actions": {
    "jump": ["Space", "Gamepad.South"],
    "fire": ["Mouse.Left", "Gamepad.RightShoulder"]
  },
  "axes": {
    "moveX": { "keys": ["A", "D"], "gamepad": [ { "axis": "Gamepad.LeftX", "scale": 1 } ] }
  }
})";

} // namespace

TEST(InputConfig, Names_RoundTripEnumerators)
{
    EXPECT_EQ(interi::KeyFromName("A"), Key::A);
    EXPECT_EQ(interi::KeyFromName("Slash"), Key::Slash);
    EXPECT_EQ(interi::KeyFromName("Num7"), Key::Num7);
    EXPECT_EQ(interi::MouseButtonFromName("Mouse.X2"), interi::MouseButton::X2);
    EXPECT_EQ(interi::GamepadButtonFromName("Gamepad.DpadRight"), interi::GamepadButton::DpadRight);
    EXPECT_EQ(interi::GamepadAxisFromName("Gamepad.RightTrigger"), interi::GamepadAxis::RightTrigger);
    EXPECT_FALSE(interi::KeyFromName("Hyper").has_value());
}

TEST(InputConfig, Load_BindsActions)
{
    InputMapper mapper;
    const auto warnings = ApplyInputConfig(kConfig, mapper);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty());

    const InputMapper::ActionId jump = mapper.AddAction("jump"); // existing id
    const InputMapper::ActionId fire = mapper.AddAction("fire");
    const InputMapper::AxisId moveX = mapper.AddAxis("moveX");

    const std::vector<InputEvent> frame{KeyEvent{.key = Key::Space, .down = true, .repeat = false},
                                        KeyEvent{.key = Key::D, .down = true, .repeat = false},
                                        interi::MouseButtonEvent{.button = interi::MouseButton::Left, .down = true}};
    mapper.Update(frame);
    EXPECT_TRUE(mapper.WasPressed(jump));
    EXPECT_TRUE(mapper.IsDown(fire));
    EXPECT_EQ(mapper.AxisValue(moveX), 1.0f);
}

TEST(InputConfig, UnknownInput_FailsAndBindsNothing)
{
    InputMapper mapper;
    const auto result = ApplyInputConfig(R"({ "actions": { "jump": ["Space"], "fire": ["Mouse.Fourth"] } })", mapper);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, interi::ErrorCode::InvalidArgument);
    EXPECT_NE(result.error().message.find("Mouse.Fourth"), std::string::npos);
    // Nothing was bound: pressing Space does nothing for a freshly added "jump".
    const InputMapper::ActionId jump = mapper.AddAction("jump");
    const std::vector<InputEvent> frame{KeyEvent{.key = Key::Space, .down = true, .repeat = false}};
    mapper.Update(frame);
    EXPECT_FALSE(mapper.IsDown(jump));
}

TEST(InputConfig, Invalid_Fails)
{
    InputMapper mapper;
    const char* const cases[] = {
        "not json",
        R"({ "actions": ["jump"] })",
        R"({ "actions": { "jump": "Space" } })",
        R"({ "axes": { "x": { "keys": ["A"] } } })",
        R"({ "axes": { "x": { "gamepad": [ { "axis": "Gamepad.Wheel" } ] } } })",
        R"({ "deadZone": 1.5 })",
    };
    for (const char* json : cases)
    {
        EXPECT_FALSE(ApplyInputConfig(json, mapper).has_value()) << json;
    }
}

TEST(InputConfig, UnknownField_Warns)
{
    InputMapper mapper;
    const auto warnings = ApplyInputConfig(R"({ "mouseSensitivity": 2, "axes": { "x": { "invert": true } } })", mapper);
    ASSERT_TRUE(warnings.has_value());
    EXPECT_EQ(warnings->size(), 2u);
}

TEST(InputConfig, ShippedFile_Applies)
{
    std::ifstream file(INTERI_TEST_INPUT_FILE, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    InputMapper mapper;
    const auto warnings = ApplyInputConfig(text, mapper);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty());
}

TEST(InputConfig, ReplaceMode_RebindsWhatEarlierConfigBound)
{
    InputMapper mapper;
    ASSERT_TRUE(ApplyInputConfig(kConfig, mapper).has_value());
    ASSERT_TRUE(ApplyInputConfig(R"({ "actions": { "jump": ["E"] } })", mapper, interi::InputMerge::Replace));
    const auto jump = mapper.FindAction("jump");
    ASSERT_TRUE(jump.has_value());
    ASSERT_EQ(mapper.ActionBindings(*jump).size(), 1u);
    EXPECT_EQ(interi::InputName(mapper.ActionBindings(*jump)[0]), "E");
    // "fire" was not named: untouched.
    EXPECT_EQ(mapper.ActionBindings(*mapper.FindAction("fire")).size(), 2u);
}

TEST(InputConfig, AddMode_KeepsEarlierBindings)
{
    InputMapper mapper;
    ASSERT_TRUE(ApplyInputConfig(kConfig, mapper).has_value());
    ASSERT_TRUE(ApplyInputConfig(R"({ "actions": { "jump": ["E"] } })", mapper));
    EXPECT_EQ(mapper.ActionBindings(*mapper.FindAction("jump")).size(), 3u);
}

TEST(InputConfig, Capture_RoundTripsThroughApply)
{
    InputMapper first;
    ASSERT_TRUE(ApplyInputConfig(kConfig, first).has_value());
    const std::string captured = interi::CaptureInputConfig(first);
    InputMapper second;
    const auto warnings = ApplyInputConfig(captured, second);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty());
    EXPECT_EQ(interi::CaptureInputConfig(second), captured);
    EXPECT_EQ(second.DeadZone(), first.DeadZone());
}

TEST(InputConfig, RebindAction_ReplacesOneBinding)
{
    InputMapper mapper;
    ASSERT_TRUE(ApplyInputConfig(kConfig, mapper).has_value());
    ASSERT_TRUE(interi::RebindAction(mapper, "jump", 0, "Enter").has_value());
    const auto bindings = mapper.ActionBindings(*mapper.FindAction("jump"));
    ASSERT_EQ(bindings.size(), 2u);
    EXPECT_EQ(interi::InputName(bindings[0]), "Enter");
    EXPECT_EQ(interi::InputName(bindings[1]), "Gamepad.South");
    const std::vector<InputEvent> events = {KeyEvent{Key::Enter, true, false}};
    mapper.Update(events);
    EXPECT_TRUE(mapper.IsDown(*mapper.FindAction("jump")));
}

TEST(InputConfig, RebindAction_BadNames_FailAndLeaveMapperUnchanged)
{
    InputMapper mapper;
    ASSERT_TRUE(ApplyInputConfig(kConfig, mapper).has_value());
    EXPECT_FALSE(interi::RebindAction(mapper, "nope", 0, "A").has_value());
    EXPECT_FALSE(interi::RebindAction(mapper, "jump", 0, "NotAKey").has_value());
    EXPECT_EQ(interi::InputName(mapper.ActionBindings(*mapper.FindAction("jump"))[0]), "Space");
}

TEST(InputConfig, FirstPressedInput_IgnoresRepeatsAndReleases)
{
    const std::vector<InputEvent> events = {KeyEvent{Key::A, false, false}, KeyEvent{Key::B, true, true},
                                            interi::MouseButtonEvent{interi::MouseButton::Right, true}};
    EXPECT_EQ(interi::FirstPressedInput(events), "Mouse.Right");
    EXPECT_EQ(interi::FirstPressedInput(std::span<const InputEvent>{}), std::nullopt);
}

TEST(RebindPrompt, ArmsFromMenuActionsOnly)
{
    interi::RebindPrompt prompt;
    EXPECT_FALSE(prompt.BeginFromAction("game.resume"));
    EXPECT_FALSE(prompt.BeginFromAction("input.rebind:"));
    EXPECT_FALSE(prompt.Active());
    EXPECT_TRUE(prompt.BeginFromAction("input.rebind:jump"));
    EXPECT_TRUE(prompt.Active());
    EXPECT_EQ(prompt.Action(), "jump");
}

TEST(RebindPrompt, NextPress_RebindsAndDisarms)
{
    InputMapper mapper;
    ASSERT_TRUE(ApplyInputConfig(kConfig, mapper).has_value());
    interi::RebindPrompt prompt;
    EXPECT_EQ(prompt.Update(std::vector<InputEvent>{KeyEvent{Key::K, true, false}}, mapper).status,
              interi::RebindPrompt::Status::Waiting); // not armed: events are ignored
    ASSERT_TRUE(prompt.BeginFromAction("input.rebind:jump"));
    EXPECT_EQ(prompt.Update(std::vector<InputEvent>{KeyEvent{Key::K, false, false}}, mapper).status,
              interi::RebindPrompt::Status::Waiting); // a release is not a press
    const auto outcome = prompt.Update(std::vector<InputEvent>{KeyEvent{Key::K, true, false}}, mapper);
    EXPECT_EQ(outcome.status, interi::RebindPrompt::Status::Rebound);
    EXPECT_EQ(outcome.input, "K");
    EXPECT_FALSE(prompt.Active());
    EXPECT_EQ(interi::InputName(mapper.ActionBindings(*mapper.FindAction("jump"))[0]), "K");
    // Disarmed: the next press changes nothing.
    EXPECT_EQ(prompt.Update(std::vector<InputEvent>{KeyEvent{Key::L, true, false}}, mapper).status,
              interi::RebindPrompt::Status::Waiting);
    EXPECT_EQ(interi::InputName(mapper.ActionBindings(*mapper.FindAction("jump"))[0]), "K");
}

TEST(RebindPrompt, EscapeCancels_UnknownActionFails)
{
    InputMapper mapper;
    ASSERT_TRUE(ApplyInputConfig(kConfig, mapper).has_value());
    interi::RebindPrompt prompt;
    ASSERT_TRUE(prompt.BeginFromAction("input.rebind:jump"));
    EXPECT_EQ(prompt.Update(std::vector<InputEvent>{KeyEvent{Key::Escape, true, false}}, mapper).status,
              interi::RebindPrompt::Status::Cancelled);
    EXPECT_EQ(interi::InputName(mapper.ActionBindings(*mapper.FindAction("jump"))[0]), "Space"); // untouched
    ASSERT_TRUE(prompt.BeginFromAction("input.rebind:nope"));
    const auto failed = prompt.Update(std::vector<InputEvent>{KeyEvent{Key::K, true, false}}, mapper);
    EXPECT_EQ(failed.status, interi::RebindPrompt::Status::Failed);
    EXPECT_FALSE(failed.message.empty());
    EXPECT_FALSE(prompt.Active());
}
