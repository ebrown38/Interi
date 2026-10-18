// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/LevelBridge.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

constexpr const char* kPrimitive = scene::PrimitiveComponent::kComponentName;

class InspectorTest : public ::testing::Test
{
protected:
    InspectorTest()
        : m_scene(m_reflection)
    {
        RegisterEngineReflection(m_reflection);
        Transform transform;
        transform.position = {0, 1, 0};
        m_box = m_scene.CreatePrimitive({}, transform, "box");
    }

    [[nodiscard]] Vec3 Size() const
    {
        return m_scene.GetWorld().Get<scene::PrimitiveComponent>(*m_scene.Find(m_box))->size;
    }

    ReflectionRegistry m_reflection;
    EditorScene m_scene;
    UndoStack m_undo;
    EditorEntityId m_box = 0;
};

} // namespace

TEST_F(InspectorTest, EditField_RecordsUndoCommand)
{
    ASSERT_TRUE(EditField(m_scene, m_undo, m_box, kPrimitive, "size", Vec3(2, 3, 4)));
    INTERI_EXPECT_VEC_NEAR(Size(), Vec3(2, 3, 4), 0.0f);
    EXPECT_EQ(m_undo.Size(), 1u);
    EXPECT_TRUE(m_undo.IsDirty());

    ASSERT_TRUE(m_undo.Undo());
    INTERI_EXPECT_VEC_NEAR(Size(), Vec3(1.0f), 0.0f);
    ASSERT_TRUE(m_undo.Redo());
    INTERI_EXPECT_VEC_NEAR(Size(), Vec3(2, 3, 4), 0.0f);
}

TEST_F(InspectorTest, EditField_Invalid_RecordsNothing)
{
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, kPrimitive, "size", 2.0f)); // wrong type
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, kPrimitive, "nope", Vec3(1.0f)));
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, "interi.editor.Destructible", "asset", std::string("x")));
    EXPECT_FALSE(EditField(m_scene, m_undo, 999, kPrimitive, "size", Vec3(2.0f)));
    EXPECT_FALSE(EditField(m_scene, m_undo, m_box, kPrimitive, "size", Vec3(1.0f))); // same
    EXPECT_EQ(m_undo.Size(), 0u);
}

TEST_F(InspectorTest, EditField_Drag_MergesIntoOneStep)
{
    for (int i = 1; i <= 10; ++i)
    {
        ASSERT_TRUE(EditField(m_scene, m_undo, m_box, kPrimitive, "size", Vec3(1.0f + static_cast<float>(i)), 42));
    }
    m_undo.Seal();
    EXPECT_EQ(m_undo.Size(), 1u);
    ASSERT_TRUE(m_undo.Undo());
    INTERI_EXPECT_VEC_NEAR(Size(), Vec3(1.0f), 0.0f);
}

TEST_F(InspectorTest, EditTransform_Undoable)
{
    Transform moved;
    moved.position = {5, 1, 0};
    moved.rotation = YawRotation(0.25f);
    ASSERT_TRUE(EditTransform(m_scene, m_undo, m_box, moved));
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(m_box).position, Vec3(5, 1, 0), 0.0f);
    EXPECT_FALSE(EditTransform(m_scene, m_undo, m_box, moved));
    ASSERT_TRUE(m_undo.Undo());
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(m_box).position, Vec3(0, 1, 0), 0.0f);
}

TEST_F(InspectorTest, EditTransform_OfChild_SetsLocalRelativeToParent)
{
    Transform parentTransform;
    parentTransform.position = {10, 0, 0};
    parentTransform.scale = Vec3(2.0f);
    const EditorEntityId parent = m_scene.CreateObject("parent", parentTransform);
    const EditorEntityId child = m_scene.CreateObject("child", {}, parent);
    Transform target;
    target.position = {14, 2, 0};
    ASSERT_TRUE(EditTransform(m_scene, m_undo, child, target));
    INTERI_EXPECT_VEC_NEAR(m_scene.LocalTransform(child).position, Vec3(2, 1, 0), 1e-6f);
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(child).position, Vec3(14, 2, 0), 1e-5f);
}

TEST_F(InspectorTest, Reparent_KeepsWorldTransformAndUndoes)
{
    Transform at;
    at.position = {3, 0, 0};
    const EditorEntityId group = m_scene.CreateObject("group", at);
    ASSERT_TRUE(Reparent(m_scene, m_undo, m_box, group));
    EXPECT_EQ(m_scene.ParentOf(m_box), group);
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(m_box).position, Vec3(0, 1, 0), 1e-6f);
    INTERI_EXPECT_VEC_NEAR(m_scene.LocalTransform(m_box).position, Vec3(-3, 1, 0), 1e-6f);
    EXPECT_EQ(m_undo.Size(), 1u);

    EXPECT_FALSE(Reparent(m_scene, m_undo, group, m_box)); // a cycle
    EXPECT_FALSE(Reparent(m_scene, m_undo, m_box, group)); // already
    EXPECT_FALSE(Reparent(m_scene, m_undo, m_box, m_box));
    EXPECT_FALSE(Reparent(m_scene, m_undo, m_box, 999));

    ASSERT_TRUE(m_undo.Undo());
    EXPECT_EQ(m_scene.ParentOf(m_box), 0u);
    INTERI_EXPECT_VEC_NEAR(m_scene.LocalTransform(m_box).position, Vec3(0, 1, 0), 0.0f);
}

TEST_F(InspectorTest, AddRemoveComponent_Undoable)
{
    ASSERT_TRUE(EditField(m_scene, m_undo, m_box, kPrimitive, "radius", 2.0f));
    ASSERT_TRUE(RemoveComponent(m_scene, m_undo, m_box, kPrimitive));
    EXPECT_FALSE(m_scene.ReadComponent(m_box, kPrimitive).has_value());
    ASSERT_TRUE(m_undo.Undo());
    EXPECT_EQ(m_scene.GetWorld().Get<scene::PrimitiveComponent>(*m_scene.Find(m_box))->radius, 2.0f);
    EXPECT_FALSE(RemoveComponent(m_scene, m_undo, m_box, scene::NameComponent::kComponentName));

    ASSERT_TRUE(AddComponent(m_scene, m_undo, m_box, SpawnPointComponent::kComponentName));
    EXPECT_FALSE(AddComponent(m_scene, m_undo, m_box, SpawnPointComponent::kComponentName));
    ASSERT_TRUE(m_undo.Undo());
    EXPECT_FALSE(m_scene.ReadComponent(m_box, SpawnPointComponent::kComponentName).has_value());
}

TEST_F(InspectorTest, Outliner_IsATreeInPreOrder)
{
    const EditorEntityId group = m_scene.CreateObject("group");
    (void)CreateSpawnPoint(m_scene, {}, "player");
    const EditorEntityId child = m_scene.CreateObject("", {}, group);
    const auto rows = OutlinerRows(m_scene);
    ASSERT_EQ(rows.size(), 4u);
    EXPECT_EQ(rows[0].label, "box");
    EXPECT_EQ(rows[1].id, group);
    EXPECT_TRUE(rows[1].hasChildren);
    EXPECT_EQ(rows[2].id, child);
    EXPECT_EQ(rows[2].depth, 1u);
    EXPECT_EQ(rows[2].label, "Object 4");
    EXPECT_EQ(rows[3].label, "Spawn player");
}
