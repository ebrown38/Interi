// SPDX-License-Identifier: MIT
#include <interi/editor/Commands.h>
#include <interi/editor/EditorScene.h>
#include <interi/editor/Reflection.h>
#include <interi/editor/UndoStack.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <memory>
#include <numbers>
#include <vector>

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

/// Three boxes, a mesh object and a group, as a small level.
void BuildSample(EditorScene& scene)
{
    scene::PrimitiveParams ground;
    ground.size = {80, 2, 80};
    (void)scene.CreatePrimitive(ground, At({0, -1, 0}), "ground");
    scene::PrimitiveParams pillar;
    pillar.size = {2, 6, 2};
    (void)scene.CreatePrimitive(pillar, At({6, 3, 6}), "pillar");
    const EditorEntityId mesh = scene.CreateObject("wall", At({4.5f, 0.0f, 0.0f}));
    (void)scene.AddComponent(mesh, scene::MeshComponent::kComponentName);
    scene.GetWorld().GetMut<scene::MeshComponent>(*scene.Find(mesh))->asset.path = "walls/wall.imesh";
    (void)scene.CreateObject("spawn", At({0, 0, 9}));
    (void)scene.CreateObject("spawn 2", At({0, 1.5f, -14}));
}

class EditorSceneTest : public ::testing::Test
{
protected:
    EditorSceneTest() { RegisterEngineReflection(m_reflection); }

    ReflectionRegistry m_reflection;
};

} // namespace

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

TEST_F(EditorSceneTest, Hierarchy_ChildFollowsParent)
{
    EditorScene scene(m_reflection);
    const EditorEntityId parent = scene.CreateObject("parent", At({0, 5, 0}));
    const EditorEntityId child = scene.CreateObject("child", At({1, 0, 0}), parent);
    INTERI_EXPECT_VEC_NEAR(scene.WorldTransform(child).position, Vec3(1, 5, 0), 0.0f);
    EXPECT_EQ(scene.ParentOf(child), parent);
    EXPECT_EQ(scene.Subtree(parent), (std::vector<EditorEntityId>{parent, child}));

    ComponentSnapshot moved{.component = scene::LocalTransformComponent::kComponentName,
                            .values = {Vec3(0, 7, 0), Quat(), Vec3(1.0f)}};
    ASSERT_TRUE(scene.WriteComponent(parent, moved));
    INTERI_EXPECT_VEC_NEAR(scene.WorldTransform(child).position, Vec3(1, 7, 0), 0.0f);
    EXPECT_EQ(scene.NameOf(child), "child");
}

TEST_F(EditorSceneTest, SnapshotRestore_KeepsIdValuesAndParent)
{
    EditorScene scene(m_reflection);
    const EditorEntityId parent = scene.CreateObject("parent", At({0, 1, 0}));
    const EditorEntityId id = scene.CreateObject("thing", At({1, 2, 3}));
    ASSERT_TRUE(scene.AddComponent(id, scene::MeshComponent::kComponentName));
    scene.GetWorld().GetMut<scene::MeshComponent>(*scene.Find(id))->asset.path = "a.imesh";
    ComponentSnapshot link{.component = scene::ParentComponent::kComponentName, .values = {values::EntityRef{parent}}};
    ASSERT_TRUE(scene.WriteComponent(id, link));
    const auto snapshot = scene.Snapshot(id);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->id, id);
    ASSERT_TRUE(scene.Destroy(id));
    EXPECT_FALSE(scene.Contains(id));
    EXPECT_FALSE(scene.Destroy(id));

    ASSERT_TRUE(scene.Restore(*snapshot));
    EXPECT_FALSE(scene.Restore(*snapshot)); // id in use
    const auto mesh = scene.ReadComponent(id, scene::MeshComponent::kComponentName);
    ASSERT_TRUE(mesh.has_value());
    EXPECT_EQ(std::get<values::AssetRef>(mesh->values[0]).path, "a.imesh");
    EXPECT_EQ(scene.ParentOf(id), parent);
    INTERI_EXPECT_VEC_NEAR(scene.WorldTransform(id).position, Vec3(1, 3, 3), 0.0f);
    EXPECT_EQ(scene.CreateObject("next"), id + 1); // ids are never reused
}

TEST_F(EditorSceneTest, DocumentRoundTrip_KeepsEverything)
{
    EditorScene scene(m_reflection);
    BuildSample(scene);
    (void)scene.CreateObject("child", At({0, 1, 0}), 1);
    const std::string text = scene::WriteScene(scene.ToDocument());

    EditorScene loaded(m_reflection);
    auto warnings = loaded.Load(*scene::ParseScene(text));
    ASSERT_TRUE(warnings.has_value()) << warnings.error().message;
    EXPECT_TRUE(warnings->empty());
    EXPECT_EQ(loaded.Size(), 6u);
    EXPECT_EQ(loaded.ParentOf(6), 1u);
    EXPECT_EQ(scene::WriteScene(loaded.ToDocument()), text);
    EXPECT_EQ(loaded.NextId(), 7u);
}

TEST_F(EditorSceneTest, Load_KeepsUnknownComponentsThroughUndo)
{
    auto document = scene::ParseScene(R"({"version": 2, "objects": [{"id": 4, "components": {
        "interi.Name": {"name": "x"}, "com.other.mod:Thing": {"power": 3}}}]})");
    ASSERT_TRUE(document.has_value());
    EditorScene scene(m_reflection);
    ASSERT_TRUE(scene.Load(*document).has_value());
    UndoStack undo;
    undo.Execute(std::make_unique<DestroyEntityCommand>(scene, 4));
    ASSERT_TRUE(undo.Undo());
    const scene::SceneDocument out = scene.ToDocument();
    ASSERT_EQ(out.objects.size(), 1u);
    EXPECT_NE(out.objects[0].Find("com.other.mod:Thing"), nullptr);
}

TEST_F(EditorSceneTest, LoadFailure_KeepsScene)
{
    EditorScene scene(m_reflection);
    (void)scene.CreateObject("keep");
    auto document = scene::ParseScene(
        R"({"version": 2, "objects": [{"id": 1, "components": {"interi.Primitive": {"segments": "many"}}}]})");
    ASSERT_TRUE(document.has_value());
    EXPECT_FALSE(scene.Load(*document).has_value());
    EXPECT_EQ(scene.NameOf(1), "keep");
}

TEST_F(EditorSceneTest, DestroyCommand_UndoRestoresSameId)
{
    EditorScene scene(m_reflection);
    BuildSample(scene);
    UndoStack undo;
    undo.Execute(std::make_unique<DestroyEntityCommand>(scene, 3));
    EXPECT_FALSE(scene.Contains(3));
    ASSERT_TRUE(undo.Undo());
    ASSERT_TRUE(scene.Contains(3));
    EXPECT_TRUE(scene.ReadComponent(3, scene::MeshComponent::kComponentName).has_value());
    ASSERT_TRUE(undo.Redo());
    EXPECT_FALSE(scene.Contains(3));
}

TEST_F(EditorSceneTest, CreateCommand_UndoRemoves)
{
    EditorScene scene(m_reflection);
    const EditorEntityId id = scene.CreatePrimitive({}, At({1, 0, 0}), "alt");
    auto snapshot = scene.Snapshot(id);
    ASSERT_TRUE(snapshot.has_value());
    ASSERT_TRUE(scene.Destroy(id));

    UndoStack undo;
    undo.Execute(std::make_unique<CreateEntityCommand>(scene, *snapshot));
    EXPECT_TRUE(scene.Contains(id));
    ASSERT_TRUE(undo.Undo());
    EXPECT_FALSE(scene.Contains(id));
}

TEST_F(EditorSceneTest, AddRemoveComponent)
{
    EditorScene scene(m_reflection);
    const EditorEntityId id = scene.CreateObject("thing");
    EXPECT_TRUE(scene.AddComponent(id, scene::PrimitiveComponent::kComponentName));
    EXPECT_FALSE(scene.AddComponent(id, scene::PrimitiveComponent::kComponentName));
    EXPECT_FALSE(scene.AddComponent(id, "no.such.Component"));
    EXPECT_TRUE(scene.RemoveComponent(id, scene::PrimitiveComponent::kComponentName));
    EXPECT_FALSE(scene.RemoveComponent(id, scene::PrimitiveComponent::kComponentName));
    EXPECT_FALSE(scene.RemoveComponent(id, scene::NameComponent::kComponentName)); // standard
}

TEST_F(EditorSceneTest, WriteComponent_WrongType_Fails)
{
    EditorScene scene(m_reflection);
    const EditorEntityId id = scene.CreatePrimitive({}, {});
    ComponentSnapshot bad{.component = scene::PrimitiveComponent::kComponentName, .values = {1.0f}};
    EXPECT_FALSE(scene.WriteComponent(id, bad));
    EXPECT_FALSE(scene.ReadComponent(id, scene::MeshComponent::kComponentName).has_value());
}

TEST_F(EditorSceneTest, MacroCommand_UndoesInReverse)
{
    EditorScene scene(m_reflection);
    const EditorEntityId parent = scene.CreateObject("parent");
    const EditorEntityId child = scene.CreateObject("child", {}, parent);
    std::vector<std::unique_ptr<ICommand>> commands;
    commands.push_back(std::make_unique<DestroyEntityCommand>(scene, child));
    commands.push_back(std::make_unique<DestroyEntityCommand>(scene, parent));
    UndoStack undo;
    undo.Execute(std::make_unique<MacroCommand>("Delete", std::move(commands)));
    EXPECT_EQ(scene.Size(), 0u);
    EXPECT_EQ(undo.UndoLabel(), "Delete");
    ASSERT_TRUE(undo.Undo());
    EXPECT_EQ(scene.ParentOf(child), parent);
}
