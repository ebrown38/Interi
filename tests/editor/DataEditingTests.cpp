// SPDX-License-Identifier: MIT
// Undoable edits of the shared SDK test project's data (tests/sdk/modules; tests/values describes it).
#include <interi/editor/DataEditing.h>
#include <interi/editor/UndoStack.h>

#include "../sdk/SdkTestModules.h"
#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace interi;
using namespace interi::editor;

namespace
{

constexpr std::string_view kJump = "com.test.a:JumpHeight";
constexpr std::string_view kGuns = "com.test.a:Guns";
constexpr std::string_view kSoldier = "com.test.a:Soldier";

class DataEditing : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Result<std::unique_ptr<ProjectData>> created = ProjectData::Create(interi::testing::ResolveSdkTestProject());
        ASSERT_TRUE(created.has_value()) << created.error().message;
        project = std::move(*created);
    }

    [[nodiscard]] values::DataRegistry& Data() const { return project->Data(); }

    std::unique_ptr<ProjectData> project;
    UndoStack undo;
};

} // namespace

TEST_F(DataEditing, EditValue_UndoRedoAndMerge)
{
    ASSERT_TRUE(EditValue(Data(), undo, kJump, 3.0f, 7).has_value());
    ASSERT_TRUE(EditValue(Data(), undo, kJump, 3.5f, 7).has_value()); // same drag: merges
    EXPECT_EQ(Data().Values().Get<float>(kJump), 3.5f);
    EXPECT_EQ(undo.Size(), 1u);
    undo.Undo();
    EXPECT_EQ(Data().Values().GetProjectValue(kJump), nullptr);
    EXPECT_EQ(Data().Values().Get<float>(kJump), 2.5f);
    undo.Redo();
    EXPECT_EQ(Data().Values().Get<float>(kJump), 3.5f);

    undo.Seal();
    ASSERT_TRUE(EditValue(Data(), undo, kJump, std::nullopt).has_value()); // reset
    EXPECT_EQ(Data().Values().Get<float>(kJump), 2.5f);
    EXPECT_EQ(undo.Size(), 2u);
    undo.Undo();
    EXPECT_EQ(Data().Values().Get<float>(kJump), 3.5f);
}

TEST_F(DataEditing, InvalidEdit_RejectedAndNotRecorded)
{
    const Result<void> result = EditValue(Data(), undo, kJump, 25.0f);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("maximum"), std::string::npos);
    EXPECT_FALSE(EditValue(Data(), undo, "com.test.a:Nothing", 1.0f).has_value());
    EXPECT_FALSE(EditAttributeDefault(Data(), undo, kSoldier, "attr.Health", 9999.0f).has_value());
    EXPECT_FALSE(EditCell(Data(), undo, kGuns, "nope", "range", 1.0f).has_value());
    EXPECT_EQ(undo.Size(), 0u);
    EXPECT_TRUE(EditValue(Data(), undo, kJump, std::nullopt).has_value()) << "resetting nothing is no edit";
    EXPECT_EQ(undo.Size(), 0u);
}

TEST_F(DataEditing, AttributeDefault_Undo)
{
    ASSERT_TRUE(EditAttributeDefault(Data(), undo, kSoldier, "attr.Health", 175.0f).has_value());
    EXPECT_EQ(Data().Characters().Layout(kSoldier)->slots[0].schema.defaultValue, values::Value(175.0f));
    undo.Undo();
    EXPECT_EQ(Data().Characters().Layout(kSoldier)->slots[0].schema.defaultValue, values::Value(150.0f));
}

TEST_F(DataEditing, Rows_AddEditRemoveUndo)
{
    ASSERT_TRUE(AddTableRow(Data(), undo, kGuns, "shotgun").has_value());
    EXPECT_FALSE(AddTableRow(Data(), undo, kGuns, "shotgun").has_value());
    ASSERT_TRUE(EditCell(Data(), undo, kGuns, "shotgun", "range", 20.0f).has_value());
    const Result<void> tooFar = EditCell(Data(), undo, kGuns, "shotgun", "range", 120.0f);
    ASSERT_FALSE(tooFar.has_value());
    EXPECT_NE(tooFar.error().message.find("row 'shotgun', column 'range'"), std::string::npos);
    EXPECT_EQ(Data().Tables().Get<float>(kGuns, "shotgun", "range"), 20.0f);

    ASSERT_TRUE(RemoveTableRow(Data(), undo, kGuns, "shotgun").has_value());
    EXPECT_FALSE(Data().Tables().HasRow(kGuns, "shotgun"));
    undo.Undo(); // the row comes back with its cells
    EXPECT_EQ(Data().Tables().Get<float>(kGuns, "shotgun", "range"), 20.0f);
    undo.Undo();
    EXPECT_EQ(Data().Tables().Get<float>(kGuns, "shotgun", "range"), 50.0f);
    undo.Undo();
    EXPECT_FALSE(Data().Tables().HasRow(kGuns, "shotgun"));
    undo.Redo();
    EXPECT_TRUE(Data().Tables().HasRow(kGuns, "shotgun"));
}

TEST_F(DataEditing, Provenance_BadgeAndTooltip)
{
    EXPECT_EQ(ProvenanceBadge(Data().Values().Provenance(kJump)), "com.test.b");
    EXPECT_EQ(ProvenanceTooltip(Data().Values().Provenance(kJump)), "com.test.b (overrides com.test.a)\ncom.test.a");
    ASSERT_TRUE(EditValue(Data(), undo, kJump, 3.0f).has_value());
    EXPECT_EQ(ProvenanceBadge(Data().Values().Provenance(kJump)), "project");
    EXPECT_EQ(ProvenanceTooltip(Data().Values().Provenance("value.Gravity")),
              "com.test.b (shadows com.test.a)\ncom.test.a (shadows com.test.ext)\ncom.test.ext");
    EXPECT_EQ(ProvenanceBadge({}), "");
}
