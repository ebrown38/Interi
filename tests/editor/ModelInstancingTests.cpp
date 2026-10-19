// SPDX-License-Identifier: MIT
#include <interi/asset_import/ModelManifest.h>
#include <interi/assets/Material.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor/Prefabs.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Primitives.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

namespace
{

class ModelInstancingTest : public ::testing::Test
{
protected:
    ModelInstancingTest()
    {
        const auto write = [&](const std::string& path, const std::vector<std::byte>& bytes)
        { EXPECT_TRUE(m_files.WriteFile(path, bytes).has_value()); };
        scene::PrimitiveParams box;
        write("imported/m.fbx/Body.imesh", scene::WriteMeshAsset(*scene::GeneratePrimitive(box)));
        write("imported/m.fbx/Head.imesh", scene::WriteMeshAsset(*scene::GeneratePrimitive(box)));
        assets::PbrMaterial red;
        red.baseColor = Vec4(1.0f, 0.0f, 0.0f, 1.0f);
        write("imported/m.fbx/Red.imat", assets::WriteMaterialAsset(red));
        asset_import::ModelManifest manifest;
        manifest.source = "m.fbx";
        manifest.meshes.push_back({.name = "Body",
                                   .mesh = "imported/m.fbx/Body.imesh",
                                   .skin = {},
                                   .materials = {"imported/m.fbx/Red.imat", ""}});
        manifest.meshes.push_back({.name = "Head", .mesh = "imported/m.fbx/Head.imesh", .skin = {}, .materials = {}});
        const std::string text = asset_import::WriteModelManifest(manifest);
        write("imported/m.fbx/manifest.imodel.json",
              std::vector<std::byte>(reinterpret_cast<const std::byte*>(text.data()),
                                     reinterpret_cast<const std::byte*>(text.data()) + text.size()));
    }

    MemoryFileSystem m_files;
    EditorDocument m_document;
};

} // namespace

TEST_F(ModelInstancingTest, CreatesARootWithOneChildPerMesh)
{
    Transform at;
    at.position = Vec3(4.0f, 0.0f, 2.0f);
    auto root = InstantiateModel(m_document, m_files, "imported/m.fbx/manifest.imodel.json", at);
    ASSERT_TRUE(root.has_value()) << root.error().message;
    EXPECT_EQ(m_document.Selection(), *root);
    const std::vector<EditorEntityId> subtree = m_document.Scene().Subtree(*root);
    ASSERT_EQ(subtree.size(), 3u);
    EXPECT_EQ(m_document.Scene().WorldTransform(*root).position, Vec3(4.0f, 0.0f, 2.0f));
    const scene::SceneDocument document = m_document.Scene().ToDocument();
    const auto meshAsset = [&](EditorEntityId id)
    {
        const scene::SceneComponent* mesh = document.Find(id)->Find(scene::MeshComponent::kComponentName);
        return mesh == nullptr ? std::string() : mesh->fields.front().second;
    };
    EXPECT_NE(meshAsset(subtree[1]).find("Body.imesh"), std::string::npos);
    EXPECT_NE(meshAsset(subtree[2]).find("Head.imesh"), std::string::npos);
}

TEST_F(ModelInstancingTest, SlotColoursComeFromTheCookedMaterials)
{
    auto root = InstantiateModel(m_document, m_files, "imported/m.fbx/manifest.imodel.json", {});
    ASSERT_TRUE(root.has_value());
    const std::vector<EditorEntityId> subtree = m_document.Scene().Subtree(*root);
    const auto* body = m_document.Scene().GetWorld().Get<scene::MeshComponent>(*m_document.Scene().Find(subtree[1]));
    ASSERT_NE(body, nullptr);
    ASSERT_EQ(body->colors.items.size(), 2u);
    const auto& first = std::get<values::Color>(body->colors.items[0]);
    EXPECT_FLOAT_EQ(first.r, 1.0f);
    EXPECT_FLOAT_EQ(first.g, 0.0f);
    const auto& second = std::get<values::Color>(body->colors.items[1]);
    EXPECT_FLOAT_EQ(second.r, 0.6f); // a slot without a material is grey
}

TEST_F(ModelInstancingTest, IsOneUndoStep)
{
    const std::size_t before = m_document.Scene().Ids().size();
    ASSERT_TRUE(InstantiateModel(m_document, m_files, "imported/m.fbx/manifest.imodel.json", {}).has_value());
    EXPECT_EQ(m_document.Scene().Ids().size(), before + 3);
    ASSERT_TRUE(m_document.Undo().Undo());
    EXPECT_EQ(m_document.Scene().Ids().size(), before);
    EXPECT_FALSE(m_document.Undo().CanUndo());
    ASSERT_TRUE(m_document.Undo().Redo());
    EXPECT_EQ(m_document.Scene().Ids().size(), before + 3);
}

TEST_F(ModelInstancingTest, TwoInstances_GetDistinctIds)
{
    auto a = InstantiateModel(m_document, m_files, "imported/m.fbx/manifest.imodel.json", {});
    auto b = InstantiateModel(m_document, m_files, "imported/m.fbx/manifest.imodel.json", {});
    ASSERT_TRUE(a.has_value());
    ASSERT_TRUE(b.has_value());
    EXPECT_NE(*a, *b);
    EXPECT_EQ(m_document.Scene().Ids().size(), 6u);
}

TEST_F(ModelInstancingTest, BadManifests_AreErrors)
{
    EXPECT_EQ(InstantiateModel(m_document, m_files, "missing.imodel.json", {}).error().code, ErrorCode::NotFound);
    const std::string junk = "not json";
    ASSERT_TRUE(m_files.WriteFile("junk.imodel.json", {reinterpret_cast<const std::byte*>(junk.data()), junk.size()})
                    .has_value());
    EXPECT_FALSE(InstantiateModel(m_document, m_files, "junk.imodel.json", {}).has_value());
    EXPECT_TRUE(m_document.Scene().Ids().empty());
}
