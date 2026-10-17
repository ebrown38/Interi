// SPDX-License-Identifier: MIT
#include <interi/editor/EditorDocument.h>
#include <interi/editor/EditorMath.h>
#include <interi/editor/Inspector.h>
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

TEST(EditorDocument, OpenEditSave_RoundTripsAndTracksDirty)
{
    MemoryFileSystem files;
    Write(files, "levels/test.json", kLevel);
    EditorDocument document;
    ASSERT_TRUE(document.Open(files, "levels/test.json").has_value());
    EXPECT_EQ(document.Scene().Size(), 3u);
    EXPECT_FALSE(document.IsDirty());

    ASSERT_TRUE(
        EditField(document.Scene(), document.Undo(), 1, "interi.editor.StaticBox", "halfExtents", Vec3(20, 1, 20)));
    EXPECT_TRUE(document.IsDirty());
    ASSERT_TRUE(document.Save(files).has_value());
    EXPECT_FALSE(document.IsDirty());

    EditorDocument reopened;
    ASSERT_TRUE(reopened.Open(files, "levels/test.json").has_value());
    const auto level = reopened.Scene().ToLevel();
    ASSERT_EQ(level.staticBoxes.size(), 1u);
    INTERI_EXPECT_VEC_NEAR(level.staticBoxes[0].halfExtents, Vec3(20, 1, 20), 0.0f);
    EXPECT_EQ(level.destructibles.size(), 1u);
}

TEST(EditorDocument, OpenFailure_KeepsDocument)
{
    MemoryFileSystem files;
    Write(files, "good.json", kLevel);
    Write(files, "bad.json", "{ not json");
    EditorDocument document;
    ASSERT_TRUE(document.Open(files, "good.json").has_value());
    EXPECT_FALSE(document.Open(files, "bad.json").has_value());
    EXPECT_FALSE(document.Open(files, "missing.json").has_value());
    EXPECT_EQ(document.Path(), "good.json");
    EXPECT_EQ(document.Scene().Size(), 3u);
}

TEST(EditorDocument, SaveWithoutPath_Fails)
{
    MemoryFileSystem files;
    EditorDocument document;
    EXPECT_FALSE(document.Save(files).has_value());
    ASSERT_TRUE(document.SaveAs(files, "new.json").has_value());
    EXPECT_TRUE(files.Exists("new.json"));
}

TEST(EditorDocument, AddAndDelete_UndoableAndSelected)
{
    EditorDocument document;
    const EditorEntityId id =
        document.Add([](EditorScene& scene) { return scene.CreateStaticBox(Transform(), Vec3(1.0f)); });
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
