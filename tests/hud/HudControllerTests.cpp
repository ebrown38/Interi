// SPDX-License-Identifier: MIT
#include <interi/hud/HudController.h>

#include "HudTestUtil.h"
#include <gtest/gtest.h>

using namespace interi;
using namespace interi::hud;

namespace
{

// Two buttons stacked in the middle of a 100x100 screen, and one hidden until "advanced" is on.
constexpr const char* kDoc = R"json({
  "version": 1, "referenceSize": [100, 100],
  "widgets": [
    { "id": "a", "type": "button", "anchor": "topLeft", "offset": [0, 0], "size": [50, 20], "text": "A", "action": "do.a" },
    { "id": "b", "type": "button", "anchor": "topLeft", "offset": [0, 30], "size": [50, 20], "text": "B", "action": "do.b" },
    { "id": "c", "type": "button", "anchor": "topLeft", "offset": [0, 60], "size": [50, 20], "text": "C", "action": "do.c",
      "visibleIf": "advanced" }
  ]})json";

class HudControllerTest : public ::testing::Test
{
protected:
    HudControllerTest()
        : m_controller(hudtest::MustParse(kDoc))
    {
        m_controller.Update(100, 100, m_values);
    }

    hudtest::MapValues m_values;
    HudController m_controller;
};

} // namespace

TEST_F(HudControllerTest, Click_ReleaseOnTheSameButton_RaisesItsAction)
{
    m_controller.PointerDown(10, 40);
    EXPECT_TRUE(m_controller.TakeActions().empty()); // not yet
    m_controller.PointerUp(10, 40);
    const std::vector<std::string> expected = {"do.b"};
    EXPECT_EQ(m_controller.TakeActions(), expected);
    EXPECT_TRUE(m_controller.TakeActions().empty());
}

TEST_F(HudControllerTest, Click_DraggedOffTheButton_Cancels)
{
    m_controller.PointerDown(10, 10);
    m_controller.PointerUp(10, 40); // released over another button
    EXPECT_TRUE(m_controller.TakeActions().empty());
    m_controller.PointerDown(10, 10);
    m_controller.PointerUp(90, 90); // released over nothing
    EXPECT_TRUE(m_controller.TakeActions().empty());
}

TEST_F(HudControllerTest, Hover_SetsHoveredAndFocus)
{
    m_controller.PointerMove(10, 10);
    EXPECT_EQ(m_controller.Hovered(), "a");
    EXPECT_EQ(m_controller.Focused(), "a");
    m_controller.PointerMove(90, 90);
    EXPECT_EQ(m_controller.Hovered(), "");
    EXPECT_EQ(m_controller.Focused(), "a"); // focus stays
}

TEST_F(HudControllerTest, FocusNavigation_WrapsOverVisibleButtons)
{
    m_controller.FocusNext();
    EXPECT_EQ(m_controller.Focused(), "a");
    m_controller.FocusNext();
    EXPECT_EQ(m_controller.Focused(), "b");
    m_controller.FocusNext(); // c is hidden
    EXPECT_EQ(m_controller.Focused(), "a");
    m_controller.FocusPrevious();
    EXPECT_EQ(m_controller.Focused(), "b");
    m_controller.Activate();
    const std::vector<std::string> expected = {"do.b"};
    EXPECT_EQ(m_controller.TakeActions(), expected);
}

TEST_F(HudControllerTest, FocusPrevious_FromNothing_SelectsTheFirst)
{
    m_controller.FocusPrevious();
    EXPECT_EQ(m_controller.Focused(), "b"); // wraps backwards from index 0 to the last visible
}

TEST_F(HudControllerTest, Activate_WithoutFocus_DoesNothing)
{
    m_controller.Activate();
    EXPECT_TRUE(m_controller.TakeActions().empty());
}

TEST_F(HudControllerTest, ButtonThatBecomesHidden_LosesFocusAndCannotBeClicked)
{
    m_values.Set("advanced", true);
    m_controller.Update(100, 100, m_values);
    m_controller.FocusPrevious();
    EXPECT_EQ(m_controller.Focused(), "c");
    m_values.Set("advanced", false);
    m_controller.Update(100, 100, m_values);
    EXPECT_EQ(m_controller.Focused(), "");
    m_controller.PointerDown(10, 70);
    m_controller.PointerUp(10, 70);
    EXPECT_TRUE(m_controller.TakeActions().empty());
}

TEST_F(HudControllerTest, SetDocument_ResetsState)
{
    m_controller.FocusNext();
    m_controller.SetDocument(hudtest::MustParse(kDoc));
    EXPECT_EQ(m_controller.Focused(), "");
    EXPECT_TRUE(m_controller.Frame().items.empty());
}
