// SPDX-License-Identifier: MIT
#include <interi/platform/InputMapper.h>

#include "FakeEventSource.h"
#include <gtest/gtest.h>

#include <vector>

using interi::GamepadAxis;
using interi::GamepadAxisEvent;
using interi::GamepadButton;
using interi::GamepadButtonEvent;
using interi::GamepadConnectionEvent;
using interi::InputEvent;
using interi::InputMapper;
using interi::Key;
using interi::KeyEvent;
using interi::MouseButton;
using interi::MouseButtonEvent;
using interi::MouseMotionEvent;
using interi::testing::FakeEventSource;

namespace
{

KeyEvent Press(Key key)
{
    return {.key = key, .down = true, .repeat = false};
}

KeyEvent Release(Key key)
{
    return {.key = key, .down = false, .repeat = false};
}

/// Drains the fake source into the mapper, as the main loop does once per frame.
class InputFrame
{
public:
    InputFrame(FakeEventSource& source, InputMapper& mapper)
        : m_source(source)
        , m_mapper(mapper)
    {
    }

    void Next()
    {
        m_events.clear();
        m_source.PollEvents(m_events);
        m_mapper.Update(m_events);
    }

private:
    FakeEventSource& m_source;
    InputMapper& m_mapper;
    std::vector<InputEvent> m_events;
};

} // namespace

TEST(InputMapper, ActionBoundToTwoKeys_EitherTriggers)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto jump = mapper.AddAction("jump");
    mapper.BindKey(jump, Key::Space);
    mapper.BindKey(jump, Key::W);

    source.Push(Press(Key::Space));
    frame.Next();
    EXPECT_TRUE(mapper.IsDown(jump));
    EXPECT_TRUE(mapper.WasPressed(jump));

    source.Push(Release(Key::Space));
    frame.Next();
    EXPECT_FALSE(mapper.IsDown(jump));
    EXPECT_TRUE(mapper.WasReleased(jump));

    source.Push(Press(Key::W));
    frame.Next();
    EXPECT_TRUE(mapper.IsDown(jump));
    EXPECT_TRUE(mapper.WasPressed(jump));
}

TEST(InputMapper, BothKeysHeld_ReleasingOneStaysDown)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto fire = mapper.AddAction("fire");
    mapper.BindKey(fire, Key::F);
    mapper.BindMouseButton(fire, MouseButton::Left);

    source.Push(Press(Key::F));
    source.Push(MouseButtonEvent{.button = MouseButton::Left, .down = true});
    frame.Next();
    EXPECT_TRUE(mapper.WasPressed(fire));

    source.Push(Release(Key::F));
    frame.Next();
    EXPECT_TRUE(mapper.IsDown(fire));
    EXPECT_FALSE(mapper.WasPressed(fire));
    EXPECT_FALSE(mapper.WasReleased(fire));
}

TEST(InputMapper, HeldAcrossFrames_PressedOnlyOnFirstFrame)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto crouch = mapper.AddAction("crouch");
    mapper.BindKey(crouch, Key::LeftCtrl);

    source.Push(Press(Key::LeftCtrl));
    frame.Next();
    EXPECT_TRUE(mapper.WasPressed(crouch));
    source.Push(KeyEvent{.key = Key::LeftCtrl, .down = true, .repeat = true});
    frame.Next();
    EXPECT_TRUE(mapper.IsDown(crouch));
    EXPECT_FALSE(mapper.WasPressed(crouch));
}

TEST(InputMapper, PressAndReleaseInOneFrame_ReportsBothEdges)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto use = mapper.AddAction("use");
    mapper.BindKey(use, Key::E);

    source.Push(Press(Key::E));
    source.Push(Release(Key::E));
    frame.Next();
    EXPECT_FALSE(mapper.IsDown(use));
    EXPECT_TRUE(mapper.WasPressed(use));
    EXPECT_TRUE(mapper.WasReleased(use));
}

TEST(InputMapper, UnboundInput_DoesNotTrigger)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto jump = mapper.AddAction("jump");
    mapper.BindKey(jump, Key::Space);

    source.Push(Press(Key::Q));
    frame.Next();
    EXPECT_FALSE(mapper.IsDown(jump));
    EXPECT_FALSE(mapper.WasPressed(jump));
}

TEST(InputMapper, AddActionTwice_ReturnsSameId)
{
    InputMapper mapper;
    EXPECT_EQ(mapper.AddAction("jump"), mapper.AddAction("jump"));
    EXPECT_NE(mapper.AddAction("jump"), mapper.AddAction("fire"));
}

TEST(InputMapper, GamepadButton_TriggersAndMergesPads)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto jump = mapper.AddAction("jump");
    mapper.BindGamepadButton(jump, GamepadButton::South);

    source.Push(GamepadConnectionEvent{.gamepad = 1, .connected = true});
    source.Push(GamepadConnectionEvent{.gamepad = 2, .connected = true});
    source.Push(GamepadButtonEvent{.gamepad = 1, .button = GamepadButton::South, .down = true});
    source.Push(GamepadButtonEvent{.gamepad = 2, .button = GamepadButton::South, .down = true});
    frame.Next();
    EXPECT_TRUE(mapper.IsDown(jump));

    source.Push(GamepadButtonEvent{.gamepad = 1, .button = GamepadButton::South, .down = false});
    frame.Next();
    EXPECT_TRUE(mapper.IsDown(jump)); // pad 2 still holds it

    source.Push(GamepadConnectionEvent{.gamepad = 2, .connected = false});
    frame.Next();
    EXPECT_FALSE(mapper.IsDown(jump)); // disconnecting releases its buttons
    EXPECT_TRUE(mapper.WasReleased(jump));
}

TEST(InputMapper, GamepadAxis_DeadZoneAndRescale)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto moveX = mapper.AddAxis("moveX");
    mapper.BindGamepadAxis(moveX, GamepadAxis::LeftX);
    mapper.SetDeadZone(0.2f);

    source.Push(GamepadAxisEvent{.gamepad = 1, .axis = GamepadAxis::LeftX, .value = 0.1f});
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), 0.0f);

    source.Push(GamepadAxisEvent{.gamepad = 1, .axis = GamepadAxis::LeftX, .value = 0.6f});
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), 0.5f); // (0.6 - 0.2) / (1 - 0.2)

    source.Push(GamepadAxisEvent{.gamepad = 1, .axis = GamepadAxis::LeftX, .value = -1.0f});
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), -1.0f);
}

TEST(InputMapper, KeyAxis_SumsAndClamps)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    const auto moveX = mapper.AddAxis("moveX");
    mapper.BindKeys(moveX, Key::A, Key::D);
    mapper.BindKeys(moveX, Key::Left, Key::Right);

    source.Push(Press(Key::D));
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), 1.0f);
    source.Push(Press(Key::Right));
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), 1.0f); // clamped
    source.Push(Press(Key::A));
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), 1.0f); // +1 +1 -1
    source.Push(Release(Key::D));
    source.Push(Release(Key::Right));
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.AxisValue(moveX), -1.0f);
}

TEST(InputMapper, MouseMotion_AccumulatesPerFrame)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    source.Push(MouseMotionEvent{.x = 10, .y = 10, .deltaX = 3, .deltaY = -1});
    source.Push(MouseMotionEvent{.x = 12, .y = 9, .deltaX = 2, .deltaY = -1});
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.MouseDeltaX(), 5.0f);
    EXPECT_FLOAT_EQ(mapper.MouseDeltaY(), -2.0f);
    frame.Next();
    EXPECT_FLOAT_EQ(mapper.MouseDeltaX(), 0.0f);
}

TEST(InputMapper, QuitEvent_Reported)
{
    FakeEventSource source;
    InputMapper mapper;
    InputFrame frame(source, mapper);
    frame.Next();
    EXPECT_FALSE(mapper.QuitRequested());
    source.Push(interi::QuitEvent{});
    frame.Next();
    EXPECT_TRUE(mapper.QuitRequested());
}

TEST(MockWindow, ReportsSize)
{
    interi::testing::MockWindow window;
    EXPECT_CALL(window, Size()).WillOnce(::testing::Return(interi::WindowExtent{800, 600}));
    EXPECT_EQ(window.Size(), (interi::WindowExtent{800, 600}));
}
