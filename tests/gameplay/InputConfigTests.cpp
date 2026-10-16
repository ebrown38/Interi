// SPDX-License-Identifier: MIT
#include <interi/gameplay/InputConfig.h>

#include <gtest/gtest.h>

#include <vector>

using interi::InputEvent;
using interi::InputMapper;
using interi::Key;
using interi::KeyEvent;
using interi::gameplay::ApplyInputConfig;

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
    EXPECT_EQ(interi::gameplay::KeyFromName("A"), Key::A);
    EXPECT_EQ(interi::gameplay::KeyFromName("Slash"), Key::Slash);
    EXPECT_EQ(interi::gameplay::KeyFromName("Num7"), Key::Num7);
    EXPECT_EQ(interi::gameplay::MouseButtonFromName("Mouse.X2"), interi::MouseButton::X2);
    EXPECT_EQ(interi::gameplay::GamepadButtonFromName("Gamepad.DpadRight"), interi::GamepadButton::DpadRight);
    EXPECT_EQ(interi::gameplay::GamepadAxisFromName("Gamepad.RightTrigger"), interi::GamepadAxis::RightTrigger);
    EXPECT_FALSE(interi::gameplay::KeyFromName("Hyper").has_value());
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
    std::FILE* file = std::fopen(INTERI_TEST_INPUT_FILE, "rb");
    ASSERT_NE(file, nullptr);
    std::string text;
    char buffer[4096];
    for (std::size_t read = 0; (read = std::fread(buffer, 1, sizeof(buffer), file)) > 0;)
    {
        text.append(buffer, read);
    }
    std::fclose(file);
    InputMapper mapper;
    const auto warnings = ApplyInputConfig(text, mapper);
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty());
}
