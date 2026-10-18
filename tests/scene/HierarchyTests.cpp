// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/scene/Hierarchy.h>
#include <interi/scene/SceneComponents.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <numbers>

using namespace interi;
using namespace interi::scene;

namespace
{

Transform At(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

const Transform& WorldOf(World& world, const HierarchyIndex& index, ObjectId id)
{
    return *world.Get<Transform>(index.nodes.at(id).entity);
}

} // namespace

TEST(Hierarchy, Child_WorldIsParentTimesLocal)
{
    World world;
    Transform parent = At({10.0f, 0.0f, 0.0f});
    parent.rotation = Quat::FromAxisAngle({0.0f, 1.0f, 0.0f}, std::numbers::pi_v<float> / 2.0f);
    parent.scale = Vec3{2.0f};
    (void)CreateObject(world, 1, "parent", 0, parent);
    (void)CreateObject(world, 2, "child", 1, At({1.0f, 0.0f, 0.0f}));
    (void)CreateObject(world, 3, "grandchild", 2, At({0.0f, 1.0f, 0.0f}));

    const HierarchyIndex index = PropagateTransforms(world);
    EXPECT_TRUE(index.broken.empty());
    // +X rotated 90 degrees about +Y is -Z; scaled by 2.
    INTERI_EXPECT_VEC_NEAR(WorldOf(world, index, 2).position, Vec3(10.0f, 0.0f, -2.0f), 1e-5f);
    INTERI_EXPECT_VEC_NEAR(WorldOf(world, index, 3).position, Vec3(10.0f, 2.0f, -2.0f), 1e-5f);
    INTERI_EXPECT_VEC_NEAR(WorldOf(world, index, 3).scale, Vec3(2.0f), 0.0f);
    INTERI_EXPECT_VEC_NEAR(WorldTransformOf(world, index, 3).position, Vec3(10.0f, 2.0f, -2.0f), 1e-5f);
}

TEST(Hierarchy, CreationOrder_DoesNotChangeResult)
{
    World a;
    (void)CreateObject(a, 1, "root", 0, At({1.0f, 2.0f, 3.0f}));
    (void)CreateObject(a, 5, "child", 1, At({1.0f, 0.0f, 0.0f}));
    World b;
    (void)CreateObject(b, 5, "child", 1, At({1.0f, 0.0f, 0.0f}));
    (void)CreateObject(b, 1, "root", 0, At({1.0f, 2.0f, 3.0f}));

    const HierarchyIndex ia = PropagateTransforms(a);
    const HierarchyIndex ib = PropagateTransforms(b);
    EXPECT_EQ(WorldOf(a, ia, 5), WorldOf(b, ib, 5));
    EXPECT_EQ(ia.PreOrder(), ib.PreOrder());
}

TEST(Hierarchy, PreOrder_ParentsBeforeChildrenByIdWithinSiblings)
{
    World world;
    (void)CreateObject(world, 4, "b", 0);
    (void)CreateObject(world, 1, "a", 0);
    (void)CreateObject(world, 7, "a2", 1);
    (void)CreateObject(world, 3, "a1", 1);
    (void)CreateObject(world, 2, "a1x", 3);

    const HierarchyIndex index = BuildHierarchy(world);
    EXPECT_EQ(index.PreOrder(), (std::vector<ObjectId>{1, 3, 2, 7, 4}));
    EXPECT_EQ(index.Subtree(3), (std::vector<ObjectId>{3, 2}));
    EXPECT_TRUE(index.IsWithin(2, 1));
    EXPECT_FALSE(index.IsWithin(4, 1));
}

TEST(Hierarchy, MissingParentAndLoop_BecomeRoots)
{
    World world;
    (void)CreateObject(world, 1, "orphan", 99, At({1.0f, 0.0f, 0.0f}));
    (void)CreateObject(world, 2, "loopA", 3);
    (void)CreateObject(world, 3, "loopB", 2);
    (void)CreateObject(world, 4, "intoLoop", 3);

    const HierarchyIndex index = PropagateTransforms(world);
    EXPECT_EQ(index.broken, (std::vector<ObjectId>{1, 2}));
    EXPECT_EQ(index.roots, (std::vector<ObjectId>{1, 2}));
    EXPECT_EQ(index.nodes.at(3).parent, 2u);
    EXPECT_EQ(index.nodes.at(4).parent, 3u);
    INTERI_EXPECT_VEC_NEAR(WorldOf(world, index, 1).position, Vec3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_EQ(index.PreOrder().size(), 4u);
}
