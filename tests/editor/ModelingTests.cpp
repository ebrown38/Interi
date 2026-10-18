// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/Modeling.h>
#include <interi/platform/FileSystem.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

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

class ModelingTest : public ::testing::Test
{
protected:
    EditorEntityId Box(const Vec3& position, const Vec3& size = Vec3(1.0f), EditorEntityId parent = 0)
    {
        scene::PrimitiveParams params;
        params.size = size;
        return m_document.Scene().CreatePrimitive(params, At(position), "box", parent);
    }

    [[nodiscard]] AABB Bounds(EditorEntityId id) const { return WorldBounds(m_document.Scene(), id, {}); }
    [[nodiscard]] Vec3 World(EditorEntityId id) const { return m_document.Scene().WorldTransform(id).position; }

    EditorDocument m_document;
    EditorScene& m_scene = m_document.Scene();
};

} // namespace

TEST_F(ModelingTest, GroupThenUngroup_KeepsWorldTransformsAndUndoes)
{
    const EditorEntityId a = Box({0, 0, 0});
    const EditorEntityId b = Box({4, 2, 0});
    m_document.Select(a);
    m_document.ToggleSelected(b);
    const EditorEntityId group = GroupSelection(m_document, {});
    ASSERT_NE(group, 0u);
    EXPECT_EQ(m_document.Selection(), group);
    EXPECT_EQ(m_scene.ParentOf(a), group);
    EXPECT_EQ(m_scene.ParentOf(b), group);
    INTERI_EXPECT_VEC_NEAR(World(group), Vec3(2.0f, 1.0f, 0.0f), 1e-6f); // the bounds' centre
    INTERI_EXPECT_VEC_NEAR(World(b), Vec3(4, 2, 0), 1e-6f);
    EXPECT_EQ(m_document.Undo().Size(), 1u);

    ASSERT_TRUE(UngroupSelection(m_document));
    EXPECT_FALSE(m_scene.Contains(group)); // an empty group goes
    EXPECT_EQ(m_scene.ParentOf(b), 0u);
    INTERI_EXPECT_VEC_NEAR(World(b), Vec3(4, 2, 0), 1e-6f);
    EXPECT_EQ(m_document.SelectedIds().size(), 2u);

    ASSERT_TRUE(m_document.Undo().Undo()); // ungroup
    ASSERT_TRUE(m_document.Undo().Undo()); // group
    EXPECT_FALSE(m_scene.Contains(group));
    EXPECT_EQ(m_scene.ParentOf(a), 0u);
    INTERI_EXPECT_VEC_NEAR(World(a), Vec3(0, 0, 0), 0.0f);
}

TEST_F(ModelingTest, GroupUnderCommonParent)
{
    const EditorEntityId parent = m_scene.CreateObject("parent", At({10, 0, 0}));
    const EditorEntityId a = Box({0, 0, 0}, Vec3(1.0f), parent);
    const EditorEntityId b = Box({2, 0, 0}, Vec3(1.0f), parent);
    m_document.Select(std::vector<EditorEntityId>{a, b});
    const EditorEntityId group = GroupSelection(m_document, {});
    EXPECT_EQ(m_scene.ParentOf(group), parent);
    INTERI_EXPECT_VEC_NEAR(World(a), Vec3(10, 0, 0), 1e-6f);
}

TEST_F(ModelingTest, Duplicate_CopiesSubtreeWithRemappedLinks)
{
    const EditorEntityId root = Box({0, 0, 0});
    const EditorEntityId child = Box({1, 0, 0}, Vec3(1.0f), root);
    m_document.Select(root);
    const std::vector<EditorEntityId> copies = DuplicateSelection(m_document);
    ASSERT_EQ(copies.size(), 1u);
    EXPECT_EQ(m_scene.Size(), 4u);
    EXPECT_EQ(m_document.Selection(), copies[0]);
    const std::vector<EditorEntityId> subtree = m_scene.Subtree(copies[0]);
    ASSERT_EQ(subtree.size(), 2u);
    EXPECT_NE(subtree[1], child);
    EXPECT_EQ(m_scene.ParentOf(subtree[1]), copies[0]);
    EXPECT_EQ(m_scene.NameOf(subtree[1]), "box");
    ASSERT_TRUE(m_document.Undo().Undo());
    EXPECT_EQ(m_scene.Size(), 2u);
}

TEST_F(ModelingTest, Array_OffsetsEachCopy)
{
    const EditorEntityId box = Box({0, 0, 0});
    m_document.Select(box);
    const std::vector<EditorEntityId> copies = DuplicateSelection(m_document, 3, Vec3(2, 0, 0));
    ASSERT_EQ(copies.size(), 3u);
    INTERI_EXPECT_VEC_NEAR(World(copies[2]), Vec3(6, 0, 0), 0.0f);
    EXPECT_EQ(m_document.Undo().Size(), 1u);
    EXPECT_EQ(m_document.Undo().UndoLabel(), "Array");
}

TEST_F(ModelingTest, Align_MinCentreMax)
{
    const EditorEntityId a = Box({0, 0, 0}, Vec3(2.0f));
    const EditorEntityId b = Box({5, 3, 0}, Vec3(1.0f));
    m_document.Select(std::vector<EditorEntityId>{a, b});
    ASSERT_TRUE(AlignSelection(m_document, 1, AlignMode::Min, {}));
    EXPECT_FLOAT_EQ(Bounds(b).min.y, -1.0f);
    EXPECT_FLOAT_EQ(Bounds(a).min.y, -1.0f);
    ASSERT_TRUE(AlignSelection(m_document, 0, AlignMode::Max, {}));
    EXPECT_FLOAT_EQ(Bounds(a).max.x, 5.5f);
    EXPECT_FLOAT_EQ(Bounds(b).max.x, 5.5f);
    ASSERT_TRUE(AlignSelection(m_document, 0, AlignMode::Centre, {}));
    EXPECT_FLOAT_EQ(World(a).x, World(b).x);
    EXPECT_FALSE(AlignSelection(m_document, 0, AlignMode::Centre, {})); // already aligned
    m_document.Select(a);
    EXPECT_FALSE(AlignSelection(m_document, 0, AlignMode::Min, {}));
}

TEST_F(ModelingTest, Distribute_SpacesCentresEvenly)
{
    const EditorEntityId a = Box({0, 0, 0});
    const EditorEntityId b = Box({1, 0, 0});
    const EditorEntityId c = Box({2.5f, 0, 0});
    const EditorEntityId d = Box({9, 0, 0});
    m_document.Select(std::vector<EditorEntityId>{d, b, a, c});
    ASSERT_TRUE(DistributeSelection(m_document, 0, {}));
    EXPECT_FLOAT_EQ(World(a).x, 0.0f);
    EXPECT_FLOAT_EQ(World(b).x, 3.0f);
    EXPECT_FLOAT_EQ(World(c).x, 6.0f);
    EXPECT_FLOAT_EQ(World(d).x, 9.0f);
}

TEST_F(ModelingTest, Mirror_ReflectsBoundsAndKeepsAProperRotation)
{
    scene::PrimitiveParams wedge;
    wedge.shape = scene::PrimitiveShape::Wedge;
    Transform turned = At({3, 0, 0});
    turned.rotation = Quat::FromAxisAngle({0, 1, 0}, 0.3f);
    const EditorEntityId a = m_scene.CreatePrimitive(wedge, turned);
    const EditorEntityId b = Box({-1, 0, 0});
    m_document.Select(std::vector<EditorEntityId>{a, b});
    const AABB before = Bounds(a);
    const float plane = (std::min(before.min.x, Bounds(b).min.x) + std::max(before.max.x, Bounds(b).max.x)) * 0.5f;
    ASSERT_TRUE(MirrorSelection(m_document, 0, {}));
    const AABB after = Bounds(a);
    EXPECT_NEAR(after.min.x, 2.0f * plane - before.max.x, 1e-4f);
    EXPECT_NEAR(after.max.x, 2.0f * plane - before.min.x, 1e-4f);
    const Transform world = m_scene.WorldTransform(a);
    const Quat& q = world.rotation;
    EXPECT_NEAR(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w, 1.0f, 1e-5f);
    EXPECT_LT(world.scale.x * world.scale.y * world.scale.z, 0.0f);
    // The mirrored transform maps a point to the reflection of where the original put it.
    const Vec3 p{0.4f, 0.2f, -0.3f};
    const Vec3 original = turned.TransformPoint(p);
    INTERI_EXPECT_VEC_NEAR(world.TransformPoint(p), Vec3(2.0f * plane - original.x, original.y, original.z), 1e-4f);
}

TEST_F(ModelingTest, Snap_MovesToGrid)
{
    const EditorEntityId a = Box({0.3f, 1.1f, -0.6f});
    m_document.Select(a);
    ASSERT_TRUE(SnapSelection(m_document, 0.5f));
    INTERI_EXPECT_VEC_NEAR(World(a), Vec3(0.5f, 1.0f, -0.5f), 1e-6f);
    EXPECT_FALSE(SnapSelection(m_document, 0.5f));
}

TEST_F(ModelingTest, SetPivot_KeepsGeometryAndChildrenInPlace)
{
    Transform scaled = At({1, 0, 0});
    scaled.scale = Vec3(2.0f);
    scaled.rotation = Quat::FromAxisAngle({0, 1, 0}, 0.5f);
    scene::PrimitiveParams params;
    const EditorEntityId box = m_scene.CreatePrimitive(params, scaled);
    const EditorEntityId child = Box({0.5f, 0.5f, 0}, Vec3(1.0f), box);
    const AABB before = Bounds(box);
    const Vec3 childBefore = World(child);
    ASSERT_TRUE(SetPivot(m_document, box, PivotPlacement::BoundsBottom, {}));
    INTERI_EXPECT_VEC_NEAR(World(box), Vec3(1.0f, before.min.y, 0.0f), 1e-5f);
    INTERI_EXPECT_VEC_NEAR(Bounds(box).min, before.min, 1e-5f);
    INTERI_EXPECT_VEC_NEAR(Bounds(box).max, before.max, 1e-5f);
    INTERI_EXPECT_VEC_NEAR(World(child), childBefore, 1e-5f);
    EXPECT_EQ(m_document.Undo().Size(), 1u);
    EXPECT_FALSE(SetPivot(m_document, box, PivotPlacement::BoundsBottom, {}));
}

TEST_F(ModelingTest, Csg_ReplacesOperandsWithAMeshObject)
{
    MemoryFileSystem files;
    const EditorEntityId a = Box({0, 0, 0}, Vec3(2.0f));
    const EditorEntityId b = Box({1, 0, 0}, Vec3(2.0f));
    m_document.Select(std::vector<EditorEntityId>{a, b});
    auto result = CsgSelection(m_document, files, modeling::CsgOperation::Union, "meshes/union.imesh");
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(m_scene.Contains(a));
    EXPECT_FALSE(m_scene.Contains(b));
    EXPECT_EQ(m_document.Selection(), *result);
    INTERI_EXPECT_VEC_NEAR(World(*result), Vec3(0.5f, 0, 0), 1e-5f);
    auto mesh = ObjectMesh(m_scene, *result, files);
    ASSERT_TRUE(mesh.has_value()) << mesh.error().message;
    EXPECT_NEAR(scene::MeshVolume(*mesh), 12.0, 1e-4);

    ASSERT_TRUE(m_document.Undo().Undo());
    EXPECT_TRUE(m_scene.Contains(a));
    EXPECT_FALSE(m_scene.Contains(*result));
    EXPECT_TRUE(files.Exists("meshes/union.imesh")); // written once, kept

    m_document.Select(a);
    EXPECT_FALSE(CsgSelection(m_document, files, modeling::CsgOperation::Union, "x.imesh").has_value());
}
