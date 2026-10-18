// SPDX-License-Identifier: MIT
#include <interi/hud/MenuStack.h>

#include "HudTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::hud;

namespace
{

HudDocument Menu(const char* action)
{
    return hudtest::MustParse(std::string(R"json({"version":1,"referenceSize":[100,100],"widgets":[
      {"id":"b","type":"button","size":[100,100],"text":"x","action":")json") +
                              action + R"json("}]})json");
}

// Presses the only button of the top menu.
void Click(MenuStack& menus, const chain::IValueReader& vals)
{
    menus.Update(100, 100, vals);
    menus.Current()->PointerDown(50, 50);
    menus.Current()->PointerUp(50, 50);
}

} // namespace

TEST(MenuStack, PushPop_ShowsTheTopMenu)
{
    MenuStack menus;
    menus.Add("main", Menu("menu.push:settings"));
    menus.Add("settings", Menu("menu.pop"));
    hudtest::MapValues vals;
    EXPECT_TRUE(menus.Empty());
    EXPECT_EQ(menus.Current(), nullptr);
    ASSERT_TRUE(menus.Push("main"));
    EXPECT_EQ(menus.Top(), "main");
    Click(menus, vals);
    EXPECT_TRUE(menus.TakeActions().empty());
    EXPECT_EQ(menus.Top(), "settings");
    EXPECT_EQ(menus.Depth(), 2u);
    Click(menus, vals);
    EXPECT_TRUE(menus.TakeActions().empty());
    EXPECT_EQ(menus.Top(), "main");
}

TEST(MenuStack, Replace_SwapsTheTop_UnknownTargetsKeepIt)
{
    MenuStack menus;
    menus.Add("a", Menu("menu.replace:b"));
    menus.Add("b", Menu("menu.replace:nowhere"));
    hudtest::MapValues vals;
    ASSERT_TRUE(menus.Push("a"));
    Click(menus, vals);
    (void)menus.TakeActions();
    EXPECT_EQ(menus.Top(), "b");
    EXPECT_EQ(menus.Depth(), 1u);
    Click(menus, vals);
    (void)menus.TakeActions();
    EXPECT_EQ(menus.Top(), "b");
}

TEST(MenuStack, OtherActions_GoToTheGame)
{
    MenuStack menus;
    menus.Add("pause", Menu("game.resume"));
    hudtest::MapValues vals;
    ASSERT_TRUE(menus.Push("pause"));
    Click(menus, vals);
    const std::vector<std::string> expected = {"game.resume"};
    EXPECT_EQ(menus.TakeActions(), expected);
    EXPECT_EQ(menus.Top(), "pause"); // the game decides to Pop
}

TEST(MenuStack, Push_Unknown_Fails_PopEmptyIsSafe)
{
    MenuStack menus;
    EXPECT_FALSE(menus.Push("nope"));
    menus.Pop();
    EXPECT_TRUE(menus.TakeActions().empty());
    menus.Add("x", Menu("menu.pop"));
    EXPECT_TRUE(menus.Has("x"));
    ASSERT_TRUE(menus.Push("x"));
    hudtest::MapValues vals;
    Click(menus, vals);
    (void)menus.TakeActions();
    EXPECT_TRUE(menus.Empty()); // the last menu closed: the game resumes
}
