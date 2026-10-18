// SPDX-License-Identifier: MIT
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorMath.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/LevelBridge.h>
#include <interi/platform/FileSystem.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <string>

using namespace interi;
using namespace interi::editor;

namespace
{

constexpr std::string_view kLevel = R"({
  "staticBoxes": [ { "position": [0, -1, 0], "halfExtents": [40, 1, 40] } ],
  "destructibles": [ { "asset": "generated/wall.idest", "position": [0, 0, 0] } ],
  "spawnPoints": [ { "name": "player", "position": [0, 0, 9] } ]
})";

void Write(MemoryFileSystem& files, std::string_view path, std::string_view text)
{
    ASSERT_TRUE(files.WriteFile(path, std::as_bytes(std::span(text))).has_value());
}

} // namespace

TEST(EditorDocument, ImportLevelEditSave_RoundTripsAndTracksDirty)
{
    MemoryFileSystem files;
    Write(files, "levels/test.json", kLevel);
    EditorDocument document;
    ASSERT_TRUE(document.Open(files, "levels/test.json").has_value());
    EXPECT_EQ(document.Scene().Size(), 3u);
    EXPECT_EQ(document.Path(), "levels/test.scene.json");
    EXPECT_EQ(document.ExportPath(), "levels/test.json");
    EXPECT_FALSE(document.IsDirty());

    ASSERT_TRUE(EditField(document.Scene(), document.Undo(), 1, scene::PrimitiveComponent::kComponentName, "size",
                          Vec3(40, 2, 40)));
    EXPECT_TRUE(document.IsDirty());
    ASSERT_TRUE(document.Save(files).has_value());
    EXPECT_FALSE(document.IsDirty());
    ASSERT_TRUE(document.ExportLevel(files).has_value());

    EditorDocument reopened;
    ASSERT_TRUE(reopened.Open(files, "levels/test.scene.json").has_value());
    EXPECT_EQ(reopened.Scene().Size(), 3u);
    EXPECT_TRUE(reopened.ExportPath().empty());
    const auto level = ExportLevel(reopened.Scene());
    ASSERT_EQ(level.staticBoxes.size(), 1u);
    INTERI_EXPECT_VEC_NEAR(level.staticBoxes[0].halfExtents, Vec3(20, 1, 20), 0.0f);
    EXPECT_EQ(level.destructibles.size(), 1u);

    auto exported = gameplay::LoadLevel(files, "levels/test.json");
    ASSERT_TRUE(exported.has_value());
    INTERI_EXPECT_VEC_NEAR(exported->staticBoxes[0].halfExtents, Vec3(20, 1, 20), 0.0f);
}

TEST(EditorDocument, OpenFailure_KeepsDocument)
{
    MemoryFileSystem files;
    Write(files, "good.json", kLevel);
    Write(files, "bad.json", "{ not json");
    Write(files, "bad.scene.json", R"({"version": 2, "objects": 3})");
    EditorDocument document;
    ASSERT_TRUE(document.Open(files, "good.json").has_value());
    EXPECT_FALSE(document.Open(files, "bad.json").has_value());
    EXPECT_FALSE(document.Open(files, "bad.scene.json").has_value());
    EXPECT_FALSE(document.Open(files, "missing.json").has_value());
    EXPECT_EQ(document.Path(), "good.scene.json");
    EXPECT_EQ(document.Scene().Size(), 3u);
}

TEST(EditorDocument, SaveWithoutPath_Fails)
{
    MemoryFileSystem files;
    EditorDocument document;
    EXPECT_FALSE(document.Save(files).has_value());
    EXPECT_FALSE(document.ExportLevel(files).has_value());
    ASSERT_TRUE(document.SaveAs(files, "new.scene.json").has_value());
    EXPECT_TRUE(files.Exists("new.scene.json"));
}

TEST(EditorDocument, AddAndDelete_UndoableAndSelected)
{
    EditorDocument document;
    const EditorEntityId id = document.Add([](EditorScene& scene) { return scene.CreatePrimitive({}, {}); });
    EXPECT_EQ(document.Selection(), id);
    EXPECT_TRUE(document.IsDirty());
    ASSERT_TRUE(document.DeleteSelection());
    EXPECT_FALSE(document.Scene().Contains(id));
    EXPECT_FALSE(document.DeleteSelection());
    ASSERT_TRUE(document.Undo().Undo()); // undelete
    EXPECT_TRUE(document.Scene().Contains(id));
    ASSERT_TRUE(document.Undo().Undo()); // uncreate
    EXPECT_FALSE(document.Scene().Contains(id));
    document.Select(id);
    EXPECT_EQ(document.Selection(), 0u); // gone
    EXPECT_FALSE(document.IsDirty());
}

TEST(EditorDocument, MultiSelection_DeletesSubtreesInOneStep)
{
    EditorDocument document;
    EditorScene& scene = document.Scene();
    const EditorEntityId group = scene.CreateObject("group");
    const EditorEntityId child = scene.CreateObject("child", {}, group);
    const EditorEntityId grandchild = scene.CreateObject("grandchild", {}, child);
    const EditorEntityId other = scene.CreateObject("other");
    const EditorEntityId keep = scene.CreateObject("keep");

    document.Select(child);
    document.ToggleSelected(other);
    document.ToggleSelected(grandchild);
    EXPECT_EQ(document.Selection(), grandchild);
    EXPECT_EQ(document.SelectedIds().size(), 3u);
    EXPECT_EQ(document.SelectedRoots(), (std::vector<EditorEntityId>{child, other}));
    document.ToggleSelected(grandchild);
    EXPECT_FALSE(document.IsSelected(grandchild));
    EXPECT_EQ(document.Selection(), other);

    ASSERT_TRUE(document.DeleteSelection());
    EXPECT_EQ(scene.Ids(), (std::vector<EditorEntityId>{group, keep}));
    EXPECT_EQ(document.Undo().Size(), 1u);
    ASSERT_TRUE(document.Undo().Undo());
    EXPECT_EQ(scene.Size(), 5u);
    EXPECT_EQ(scene.ParentOf(grandchild), child);
}

TEST(EditorMath, Euler_RoundTrips)
{
    for (const Vec3 degrees :
         {Vec3(0, 0, 0), Vec3(30, 0, 0), Vec3(0, 45, 0), Vec3(0, 0, -60), Vec3(20, -130, 75), Vec3(-80, 170, 10)})
    {
        INTERI_EXPECT_VEC_NEAR(EulerDegrees(FromEulerDegrees(degrees)), degrees, 1e-3f);
    }
    // Yaw alone is a rotation about +Y.
    INTERI_EXPECT_QUAT_NEAR(FromEulerDegrees({0, 90, 0}), Quat::FromAxisAngle({0, 1, 0}, 1.5707964f), 1e-6f);
}
