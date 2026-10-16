// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

class InspectorTest : public ::testing::Test
{
protected:
    InspectorTest()
        : m_scene(m_reflection)
    {
        RegisterEngineReflection(m_reflection);
        Transform transform;
        transform.position = {0, 1, 0};
        m_box = m_scene.CreateStaticBox(transform, Vec3(1.0f));
    }

    [[nodiscard]] Vec3 HalfExtents() const
    {
        return m_scene.GetWorld().Get<StaticBoxComponent>(*m_scene.Find(m_box))->halfExtents;
    }

    ReflectionRegistry m_reflection;
    EditorScene m_scene;
    UndoStack m_undo;
    EditorEntityId m_box = 0;
};

} // namespace

TEST_F(InspectorTest, EditField_RecordsUndoCommand)
{
    ASSERT_TRUE(EditField(m_scene, m_undo, m_box, "interi.editor.StaticBox", "halfExtents", Vec3(2, 3, 4)));
    INTERI_EXPECT_VEC_NEAR(HalfExtents(), Vec3(2, 3, 4), 0.0f);
    EXPECT_EQ(m_undo.Size(), 1u);
    EXPECT_TRUE(m_undo.IsDirty());

    ASSERT_TRUE(m_undo.Undo());
    INTERI_EXPECT_VEC_NEAR(HalfExtents(), Vec3(1.0f), 0.0f);
    ASSERT_TRUE(m_undo.Redo());
    INTERI_EXPECT_VEC_NEAR(HalfExtents(), Vec3(2, 3, 4), 0.0f);
}

TEST_F(InspectorTest, EditField_Invalid_RecordsNothing)
{
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, "interi.editor.StaticBox", "halfExtents", 2.0f)); // wrong type
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, "interi.editor.StaticBox", "nope", Vec3(1.0f)));
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, "interi.editor.Destructible", "asset", std::string("x")));
    EXPECT_FALSE(EditField(m_scene, m_undo, 999, "interi.editor.StaticBox", "halfExtents", Vec3(2.0f)));
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, "interi.editor.StaticBox", "halfExtents", Vec3(1.0f))); // same
    EXPECT_EQ(m_undo.Size(), 0u);
}

TEST_F(InspectorTest, EditField_Drag_MergesIntoOneStep)
{
    for (int i = 1; i <= 10; ++i)
    {
        ASSERT_TRUE(EditField(m_scene, m_undo, m_box, "interi.editor.StaticBox", "halfExtents",
                              Vec3(1.0f + static_cast<float>(i)), 42));
    }
    m_undo.Seal();
    EXPECT_EQ(m_undo.Size(), 1u);
    ASSERT_TRUE(m_undo.Undo());
    INTERI_EXPECT_VEC_NEAR(HalfExtents(), Vec3(1.0f), 0.0f);
}

TEST_F(InspectorTest, EditTransform_Undoable)
{
    Transform moved;
    moved.position = {5, 1, 0};
    moved.rotation = YawRotation(0.25f);
    ASSERT_TRUE(EditTransform(m_scene, m_undo, m_box, moved));
    const Entity entity = *m_scene.Find(m_box);
    INTERI_EXPECT_VEC_NEAR(m_scene.GetWorld().Get<Transform>(entity)->position, Vec3(5, 1, 0), 0.0f);
    EXPECT_FALSE(EditTransform(m_scene, m_undo, m_box, moved));
    ASSERT_TRUE(m_undo.Undo());
    INTERI_EXPECT_VEC_NEAR(m_scene.GetWorld().Get<Transform>(entity)->position, Vec3(0, 1, 0), 0.0f);
}

TEST_F(InspectorTest, Outliner_LabelsById)
{
    (void)m_scene.CreateDestructible({}, "generated/wall.idest");
    (void)m_scene.CreateSpawnPoint({}, "player");
    const auto rows = OutlinerRows(m_scene);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].label, "Static box 1");
    EXPECT_EQ(rows[1].label, "Destructible 2 (generated/wall.idest)");
    EXPECT_EQ(rows[2].label, "Spawn 3 (player)");
    EXPECT_EQ(rows[2].kind, EntityKind::SpawnPoint);
}
