// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ImGuiInput.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

TEST(ImGuiInput, Keys_Mapped)
{
    EXPECT_EQ(ToImGuiKey(Key::A), ImGuiKey_A);
    EXPECT_EQ(ToImGuiKey(Key::Z), ImGuiKey_Z);
    EXPECT_EQ(ToImGuiKey(Key::Num7), ImGuiKey_7);
    EXPECT_EQ(ToImGuiKey(Key::F12), ImGuiKey_F12);
    EXPECT_EQ(ToImGuiKey(Key::Delete), ImGuiKey_Delete);
    EXPECT_EQ(ToImGuiKey(Key::Left), ImGuiKey_LeftArrow);
    EXPECT_EQ(ToImGuiKey(Key::Unknown), ImGuiKey_None);
    for (int key = 1; key < static_cast<int>(Key::Count); ++key)
    {
        EXPECT_NE(ToImGuiKey(static_cast<Key>(key)), ImGuiKey_None) << key;
    }
}

TEST(ImGuiInput, Events_ReachImGui)
{
    editor::testing::ImGuiTestContext context;
    ImGuiInput input;
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigInputTrickleEventQueue = false; // every queued event applies in the next frame
    input.Feed(io, MouseMotionEvent{.x = 100.0f, .y = 50.0f, .deltaX = 0.0f, .deltaY = 0.0f});
    input.Feed(io, MouseButtonEvent{.button = MouseButton::Left, .down = true});
    input.Feed(io, KeyEvent{.key = Key::LeftCtrl, .down = true, .repeat = false});
    input.Feed(io, KeyEvent{.key = Key::RightCtrl, .down = true, .repeat = false});
    input.Feed(io, KeyEvent{.key = Key::LeftCtrl, .down = false, .repeat = false}); // right still held
    input.Feed(io, KeyEvent{.key = Key::S, .down = true, .repeat = false});
    TextInputEvent text{};
    text.text[0] = 'q';
    input.Feed(io, text);
    input.Feed(io, MouseWheelEvent{.deltaX = 0.0f, .deltaY = 2.0f});

    (void)context.Frame([] {});
    EXPECT_FLOAT_EQ(io.MousePos.x, 100.0f);
    EXPECT_FLOAT_EQ(io.MousePos.y, 50.0f);
    EXPECT_TRUE(io.MouseDown[0]);
    EXPECT_TRUE(io.KeyCtrl);
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_S));
    EXPECT_FALSE(ImGui::IsKeyDown(ImGuiKey_LeftCtrl));
    EXPECT_TRUE(ImGui::IsKeyDown(ImGuiKey_RightCtrl));
}
