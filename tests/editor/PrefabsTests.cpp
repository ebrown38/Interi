// SPDX-License-Identifier: MIT
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/Prefabs.h>
#include <interi/platform/FileSystem.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

constexpr const char* kPrimitive = scene::PrimitiveComponent::kComponentName;
constexpr const char* kCratePath = "prefabs/crate.prefab.json";

Transform At(const Vec3& position)
{
    Transform transform;
    transform.position = position;
    return transform;
}

class PrefabsTest : public ::testing::Test
{
protected:
    PrefabsTest()
    {
        m_crate = m_scene.CreatePrimitive({}, At({5, 0, 0}), "crate");
        scene::PrimitiveParams wedge;
        wedge.shape = scene::PrimitiveShape::Wedge;
        m_lid = m_scene.CreatePrimitive(wedge, At({0, 1, 0}), "lid", m_crate);
    }

    [[nodiscard]] std::string Shape(EditorEntityId id) const
    {
        return m_scene.GetWorld().Get<scene::PrimitiveComponent>(*m_scene.Find(id))->shape.name;
    }

    /// The instance's one expanded child (the lid).
    [[nodiscard]] EditorEntityId LidOf(EditorEntityId root) const
    {
        for (const EditorEntityId id : m_scene.Subtree(root))
        {
            if (id != root)
            {
                return id;
            }
        }
        return 0;
    }

    MemoryFileSystem m_files;
    EditorDocument m_document;
    EditorScene& m_scene = m_document.Scene();
    EditorEntityId m_crate = 0;
    EditorEntityId m_lid = 0;
};

} // namespace

TEST_F(PrefabsTest, CreatePrefab_WritesFileAndMakesAnInstance)
{
    m_document.Select(m_crate);
    ASSERT_TRUE(CreatePrefabFromSelection(m_document, m_files, kCratePath).has_value());
    EXPECT_TRUE(m_files.Exists(kCratePath));
    EXPECT_EQ(InstanceRootOf(m_scene, m_crate), m_crate);
    EXPECT_FALSE(m_scene.Contains(m_lid)); // the lid is now an expanded object with a derived id
    const EditorEntityId lid = LidOf(m_crate);
    EXPECT_TRUE(scene::IsPrefabObjectId(lid));
    EXPECT_EQ(InstanceRootOf(m_scene, lid), m_crate);
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(lid).position, Vec3(5, 1, 0), 0.0f);
    EXPECT_EQ(m_document.Undo().Size(), 1u);

    ASSERT_TRUE(m_document.Undo().Undo());
    EXPECT_TRUE(m_scene.Contains(m_lid));
    EXPECT_EQ(InstanceRootOf(m_scene, m_crate), 0u);
}

TEST_F(PrefabsTest, OverrideSurvivesSaveAndReopen_RevertDropsIt)
{
    m_document.Select(m_crate);
    ASSERT_TRUE(CreatePrefabFromSelection(m_document, m_files, kCratePath).has_value());
    const EditorEntityId lid = LidOf(m_crate);
    ASSERT_TRUE(EditField(m_scene, m_document.Undo(), lid, kPrimitive, "shape", values::EnumValue{"Sphere"}));
    ASSERT_TRUE(m_document.SaveAs(m_files, "scenes/a.scene.json").has_value());

    // The saved scene holds only the instance root, with the override.
    auto saved = scene::LoadScene(m_files, "scenes/a.scene.json");
    ASSERT_TRUE(saved.has_value());
    EXPECT_EQ(saved->objects.size(), 1u);

    EditorDocument reopened;
    ASSERT_TRUE(reopened.Open(m_files, "scenes/a.scene.json").has_value());
    EXPECT_EQ(reopened.Scene().Size(), 2u);
    EXPECT_EQ(reopened.Scene().GetWorld().Get<scene::PrimitiveComponent>(*reopened.Scene().Find(lid))->shape.name,
              "Sphere");

    ASSERT_TRUE(RevertInstance(reopened, m_files, m_crate).has_value());
    EXPECT_EQ(reopened.Scene().GetWorld().Get<scene::PrimitiveComponent>(*reopened.Scene().Find(lid))->shape.name,
              "Wedge");
    ASSERT_TRUE(reopened.Undo().Undo());
    EXPECT_EQ(reopened.Scene().GetWorld().Get<scene::PrimitiveComponent>(*reopened.Scene().Find(lid))->shape.name,
              "Sphere");
}

TEST_F(PrefabsTest, Instantiate_AddsAnotherInstance)
{
    m_document.Select(m_crate);
    ASSERT_TRUE(CreatePrefabFromSelection(m_document, m_files, kCratePath).has_value());
    auto second = InstantiatePrefab(m_document, m_files, kCratePath, At({-5, 0, 0}));
    ASSERT_TRUE(second.has_value()) << second.error().message;
    EXPECT_EQ(m_document.Selection(), *second);
    EXPECT_FALSE(scene::IsPrefabObjectId(*second));
    EXPECT_EQ(m_scene.NameOf(*second), "crate"); // the prefab root's name
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(LidOf(*second)).position, Vec3(-5, 1, 0), 0.0f);
    EXPECT_FALSE(InstantiatePrefab(m_document, m_files, "prefabs/missing.prefab.json", {}).has_value());
}

TEST_F(PrefabsTest, Apply_ChangesThePrefabAndOtherInstances)
{
    m_document.Select(m_crate);
    ASSERT_TRUE(CreatePrefabFromSelection(m_document, m_files, kCratePath).has_value());
    auto second = InstantiatePrefab(m_document, m_files, kCratePath, At({-5, 0, 0}));
    ASSERT_TRUE(second.has_value());
    const EditorEntityId secondLid = LidOf(*second);
    // The second instance overrides its root's size; the first changes its lid and applies.
    ASSERT_TRUE(EditField(m_scene, m_document.Undo(), *second, kPrimitive, "size", Vec3(3.0f)));
    ASSERT_TRUE(EditField(m_scene, m_document.Undo(), LidOf(m_crate), kPrimitive, "shape", values::EnumValue{"Cone"}));
    ASSERT_TRUE(ApplyInstance(m_document, m_files, m_crate).has_value());

    EXPECT_EQ(Shape(secondLid), "Cone"); // from the prefab now
    EXPECT_EQ(m_scene.GetWorld().Get<scene::PrimitiveComponent>(*m_scene.Find(*second))->size.x, 3.0f); // kept
    INTERI_EXPECT_VEC_NEAR(m_scene.WorldTransform(m_crate).position, Vec3(5, 0, 0), 0.0f);
    auto prefab = scene::LoadScene(m_files, kCratePath);
    ASSERT_TRUE(prefab.has_value());
    EXPECT_NE(scene::WriteScene(*prefab).find("Cone"), std::string::npos);
}

TEST_F(PrefabsTest, NotAnInstance_Fails)
{
    EXPECT_FALSE(RevertInstance(m_document, m_files, m_crate).has_value());
    EXPECT_FALSE(ApplyInstance(m_document, m_files, m_crate).has_value());
    m_document.Select(std::vector<EditorEntityId>{});
    EXPECT_FALSE(CreatePrefabFromSelection(m_document, m_files, kCratePath).has_value());
}
