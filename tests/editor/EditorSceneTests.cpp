// SPDX-License-Identifier: MIT
#include <interi/editor/Commands.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <memory>
#include <numbers>

using namespace interi;
using namespace interi::editor;

namespace
{

Transform At(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

gameplay::LevelDef SampleLevel()
{
    gameplay::LevelDef level;
    level.staticBoxes.push_back({.transform = At({0, -1, 0}), .halfExtents = {40, 1, 40}});
    level.staticBoxes.push_back({.transform = At({6, 3, 6}), .halfExtents = {1, 3, 1}});
    gameplay::DestructibleDef wall;
    wall.asset = "generated/wall.idest";
    wall.transform.position = {4.5f, 0.0f, 0.0f};
    wall.transform.rotation = YawRotation(0.5f);
    level.destructibles.push_back(wall);
    level.spawnPoints.push_back({.name = "player", .position = {0, 0, 9}, .yaw = 1.25f});
    level.spawnPoints.push_back({.name = "vehicle", .position = {0, 1.5f, -14}, .yaw = std::numbers::pi_v<float>});
    return level;
}

class EditorSceneTest : public ::testing::Test
{
protected:
    EditorSceneTest() { RegisterEngineReflection(m_reflection); }

    ReflectionRegistry m_reflection;
};

} // namespace

TEST_F(EditorSceneTest, FromLevel_ToLevel_RoundTrips)
{
    EditorScene scene(m_reflection);
    const gameplay::LevelDef level = SampleLevel();
    scene.Load(level);
    EXPECT_EQ(scene.Size(), 5u);
    EXPECT_EQ(scene.KindOf(1), EntityKind::StaticBox);
    EXPECT_EQ(scene.KindOf(3), EntityKind::Destructible);
    EXPECT_EQ(scene.KindOf(5), EntityKind::SpawnPoint);

    const gameplay::LevelDef out = scene.ToLevel();
    ASSERT_EQ(out.staticBoxes.size(), 2u);
    INTERI_EXPECT_VEC_NEAR(out.staticBoxes[1].halfExtents, Vec3(1, 3, 1), 0.0f);
    INTERI_EXPECT_VEC_NEAR(out.staticBoxes[1].transform.position, Vec3(6, 3, 6), 0.0f);
    ASSERT_EQ(out.destructibles.size(), 1u);
    EXPECT_EQ(out.destructibles[0].asset, "generated/wall.idest");
    INTERI_EXPECT_QUAT_NEAR(out.destructibles[0].transform.rotation, level.destructibles[0].transform.rotation, 0.0f);
    ASSERT_EQ(out.spawnPoints.size(), 2u);
    EXPECT_EQ(out.spawnPoints[0].name, "player");
    EXPECT_NEAR(out.spawnPoints[0].yaw, 1.25f, 1e-5f);
    EXPECT_NEAR(std::abs(out.spawnPoints[1].yaw), std::numbers::pi_v<float>, 1e-5f);
    INTERI_EXPECT_VEC_NEAR(out.spawnPoints[1].position, Vec3(0, 1.5f, -14), 0.0f);
}

TEST_F(EditorSceneTest, Yaw_RoundTrips)
{
    for (const float yaw : {0.0f, 0.3f, -1.2f, 2.9f})
    {
        EXPECT_NEAR(YawOf(YawRotation(yaw)), yaw, 1e-5f) << yaw;
    }
    // Yaw 0 looks along -Z; a quarter turn left looks along -X.
    const Vec3 forward = Rotate(YawRotation(std::numbers::pi_v<float> / 2.0f), Vec3(0, 0, -1));
    INTERI_EXPECT_VEC_NEAR(forward, Vec3(-1, 0, 0), 1e-6f);
}

TEST_F(EditorSceneTest, SnapshotRestore_KeepsIdAndValues)
{
    EditorScene scene(m_reflection);
    const EditorEntityId id = scene.CreateDestructible(At({1, 2, 3}), "a.idest");
    const auto snapshot = scene.Snapshot(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->id, id);
    ASSERT_TRUE(scene.Destroy(id));
    EXPECT_FALSE(scene.Contains(id));
    EXPECT_FALSE(scene.Destroy(id));

    ASSERT_TRUE(scene.Restore(*snapshot));
    EXPECT_FALSE(scene.Restore(*snapshot)); // id in use
    const auto placement = scene.ReadComponent(id, "interi.editor.Destructible");
    ASSERT_TRUE(placement.has_value());
    EXPECT_EQ(std::get<std::string>(placement->values[0]), "a.idest");
    const Entity entity = *scene.Find(id);
    INTERI_EXPECT_VEC_NEAR(scene.GetWorld().Get<Transform>(entity)->position, Vec3(1, 2, 3), 0.0f);
    EXPECT_EQ(scene.CreateStaticBox({}, Vec3(1.0f)), id + 1); // ids are never reused
}

TEST_F(EditorSceneTest, DestroyCommand_UndoRestoresSameId)
{
    EditorScene scene(m_reflection);
    scene.Load(SampleLevel());
    UndoStack undo;
    undo.Execute(std::make_unique<DestroyEntityCommand>(scene, 3));
    EXPECT_FALSE(scene.Contains(3));
    EXPECT_EQ(scene.ToLevel().destructibles.size(), 0u);
    ASSERT_TRUE(undo.Undo());
    ASSERT_TRUE(scene.Contains(3));
    EXPECT_EQ(scene.ToLevel().destructibles.size(), 1u);
    EXPECT_EQ(scene.KindOf(3), EntityKind::Destructible);
    ASSERT_TRUE(undo.Redo());
    EXPECT_FALSE(scene.Contains(3));
}

TEST_F(EditorSceneTest, CreateCommand_UndoRemoves)
{
    EditorScene scene(m_reflection);
    const EditorEntityId id = scene.CreateSpawnPoint(At({1, 0, 0}), "alt");
    auto snapshot = scene.Snapshot(id);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_TRUE(scene.Destroy(id));

    UndoStack undo;
    undo.Execute(std::make_unique<CreateEntityCommand>(scene, *snapshot));
    EXPECT_TRUE(scene.Contains(id));
    ASSERT_TRUE(undo.Undo());
    EXPECT_FALSE(scene.Contains(id));
}

TEST_F(EditorSceneTest, WriteComponent_WrongType_Fails)
{
    EditorScene scene(m_reflection);
    const EditorEntityId id = scene.CreateStaticBox({}, Vec3(1.0f));
    ComponentSnapshot bad{.component = "interi.editor.StaticBox", .values = {1.0f}};
    EXPECT_FALSE(scene.WriteComponent(id, bad));
    EXPECT_FALSE(scene.ReadComponent(id, "interi.editor.Destructible").has_value());
}
