// SPDX-License-Identifier: MIT
#include <interi/editor/HudEditing.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;
using namespace interi::hud;

TEST(HudEditing, Add_UndoRedo_AndUniqueIds)
{
    UndoStack undo;
    HudDocument doc;
    EXPECT_EQ(AddHudWidget(undo, doc, WidgetType::Text), "text1");
    EXPECT_EQ(AddHudWidget(undo, doc, WidgetType::Text), "text2");
    EXPECT_EQ(AddHudWidget(undo, doc, WidgetType::Button), "button1");
    ASSERT_EQ(doc.widgets.size(), 3u);
    EXPECT_TRUE(ValidateHud(doc).has_value()); // every default widget loads
    EXPECT_TRUE(undo.Undo());
    EXPECT_EQ(doc.widgets.size(), 2u);
    EXPECT_TRUE(undo.Redo());
    EXPECT_EQ(doc.widgets.size(), 3u);
}

TEST(HudEditing, AllDefaultWidgets_Validate)
{
    UndoStack undo;
    HudDocument doc;
    for (const WidgetType type :
         {WidgetType::Text, WidgetType::Bar, WidgetType::Image, WidgetType::Button, WidgetType::List})
    {
        (void)AddHudWidget(undo, doc, type);
    }
    EXPECT_TRUE(ValidateHud(doc).has_value());
}

TEST(HudEditing, Edit_ValidEdit_MergesDragsIntoOneStep)
{
    UndoStack undo;
    HudDocument doc;
    (void)AddHudWidget(undo, doc, WidgetType::Text);
    const std::size_t before = undo.Size();
    for (const float x : {1.0f, 2.0f, 3.0f})
    {
        HudWidget widget = *doc.Find("text1");
        widget.offset[0] = x;
        ASSERT_TRUE(EditHudWidget(undo, doc, "text1", widget).has_value());
    }
    EXPECT_EQ(undo.Size(), before + 1);
    EXPECT_EQ(doc.widgets[0].offset[0], 3.0f);
    undo.Seal();
    EXPECT_TRUE(undo.Undo());
    EXPECT_EQ(doc.widgets[0].offset[0], 0.0f);
}

TEST(HudEditing, Edit_Rename_ChangesIdAndRejectsDuplicates)
{
    UndoStack undo;
    HudDocument doc;
    (void)AddHudWidget(undo, doc, WidgetType::Text);
    (void)AddHudWidget(undo, doc, WidgetType::Text);
    HudWidget widget = *doc.Find("text1");
    widget.id = "score";
    ASSERT_TRUE(EditHudWidget(undo, doc, "text1", widget).has_value());
    EXPECT_NE(doc.Find("score"), nullptr);
    EXPECT_EQ(doc.Find("text1"), nullptr);
    widget.id = "text2";
    const auto clash = EditHudWidget(undo, doc, "score", widget);
    ASSERT_FALSE(clash.has_value());
    EXPECT_EQ(clash.error().code, ErrorCode::InvalidArgument);
    EXPECT_NE(doc.Find("score"), nullptr);
}

TEST(HudEditing, Edit_InvalidWidget_ChangesNothingAndRecordsNothing)
{
    UndoStack undo;
    HudDocument doc;
    (void)AddHudWidget(undo, doc, WidgetType::Bar);
    const std::size_t size = undo.Size();
    HudWidget bar = *doc.Find("bar1");
    bar.max = bar.min; // max must be above min
    EXPECT_FALSE(EditHudWidget(undo, doc, "bar1", bar).has_value());
    EXPECT_EQ(doc.Find("bar1")->max, 100.0f);
    EXPECT_EQ(undo.Size(), size);
    EXPECT_EQ(EditHudWidget(undo, doc, "nope", bar).error().code, ErrorCode::NotFound);
}

TEST(HudEditing, RemoveMoveDuplicate)
{
    UndoStack undo;
    HudDocument doc;
    for (int i = 0; i < 3; ++i)
    {
        (void)AddHudWidget(undo, doc, WidgetType::Text);
    }
    ASSERT_TRUE(MoveHudWidget(undo, doc, "text3", -2).has_value());
    EXPECT_EQ(doc.widgets[0].id, "text3");
    EXPECT_TRUE(MoveHudWidget(undo, doc, "text3", -5).has_value()); // clamped: already first, no step
    const auto copy = DuplicateHudWidget(undo, doc, "text1");
    ASSERT_TRUE(copy.has_value());
    EXPECT_EQ(*copy, "text11");
    EXPECT_EQ(doc.widgets[2].id, "text11"); // right after its original
    ASSERT_TRUE(RemoveHudWidget(undo, doc, "text2").has_value());
    EXPECT_EQ(doc.Find("text2"), nullptr);
    EXPECT_TRUE(undo.Undo());
    EXPECT_NE(doc.Find("text2"), nullptr);
    EXPECT_FALSE(RemoveHudWidget(undo, doc, "nope").has_value());
    EXPECT_FALSE(MoveHudWidget(undo, doc, "nope", 1).has_value());
    EXPECT_FALSE(DuplicateHudWidget(undo, doc, "nope").has_value());
}
