// SPDX-License-Identifier: MIT
#include <interi/editor/MaterialEditing.h>
#include <interi/platform/FileSystem.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;

TEST(MaterialEditing, Edit_IsUndoable)
{
    UndoStack undo;
    assets::PbrMaterial material;
    assets::PbrMaterial edited = material;
    edited.roughness = 0.25f;
    ASSERT_TRUE(EditMaterial(undo, material, edited, "roughness").has_value());
    EXPECT_FLOAT_EQ(material.roughness, 0.25f);
    EXPECT_EQ(undo.UndoLabel(), "Edit roughness");
    ASSERT_TRUE(undo.Undo());
    EXPECT_FLOAT_EQ(material.roughness, 1.0f);
    ASSERT_TRUE(undo.Redo());
    EXPECT_FLOAT_EQ(material.roughness, 0.25f);
}

TEST(MaterialEditing, InvalidEdit_ChangesNothing)
{
    UndoStack undo;
    assets::PbrMaterial material;
    assets::PbrMaterial bad = material;
    bad.metallic = 7.0f;
    auto result = EditMaterial(undo, material, bad, "metallic");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, ErrorCode::InvalidArgument);
    EXPECT_FLOAT_EQ(material.metallic, 1.0f);
    EXPECT_FALSE(undo.CanUndo());
}

TEST(MaterialEditing, NoChange_RecordsNothing)
{
    UndoStack undo;
    assets::PbrMaterial material;
    ASSERT_TRUE(EditMaterial(undo, material, material, "name").has_value());
    EXPECT_FALSE(undo.CanUndo());
}

TEST(MaterialEditing, DragsOfOneFieldMergeUntilSealed)
{
    UndoStack undo;
    assets::PbrMaterial material;
    for (const float value : {0.9f, 0.8f, 0.7f})
    {
        assets::PbrMaterial edited = material;
        edited.roughness = value;
        ASSERT_TRUE(EditMaterial(undo, material, edited, "roughness").has_value());
    }
    EXPECT_EQ(undo.Size(), 1u);
    undo.Seal();
    assets::PbrMaterial more = material;
    more.roughness = 0.1f;
    ASSERT_TRUE(EditMaterial(undo, material, more, "roughness").has_value());
    EXPECT_EQ(undo.Size(), 2u);
    ASSERT_TRUE(undo.Undo());
    EXPECT_FLOAT_EQ(material.roughness, 0.7f);
    ASSERT_TRUE(undo.Undo());
    EXPECT_FLOAT_EQ(material.roughness, 1.0f);
}

TEST(MaterialEditing, DifferentFieldsDoNotMerge)
{
    UndoStack undo;
    assets::PbrMaterial material;
    assets::PbrMaterial a = material;
    a.roughness = 0.5f;
    ASSERT_TRUE(EditMaterial(undo, material, a, "roughness").has_value());
    assets::PbrMaterial b = material;
    b.metallic = 0.5f;
    ASSERT_TRUE(EditMaterial(undo, material, b, "metallic").has_value());
    EXPECT_EQ(undo.Size(), 2u);
}

TEST(MaterialEditing, FileRoundTrip)
{
    MemoryFileSystem files;
    assets::PbrMaterial material;
    material.name = "Steel";
    material.metallic = 0.9f;
    material.baseColorTexture.path = "imported/tex/steel.png.itex";
    ASSERT_TRUE(SaveMaterialFile(files, "materials/steel.material.json", material).has_value());
    auto opened = OpenMaterialFile(files, "materials/steel.material.json");
    ASSERT_TRUE(opened.has_value()) << opened.error().message;
    EXPECT_EQ(*opened, material);
}

TEST(MaterialEditing, OpenAndSave_RejectBadMaterials)
{
    MemoryFileSystem files;
    EXPECT_EQ(OpenMaterialFile(files, "missing.material.json").error().code, ErrorCode::NotFound);
    const std::string text = "{\"metallic\": 5}";
    ASSERT_TRUE(files.WriteFile("bad.material.json", {reinterpret_cast<const std::byte*>(text.data()), text.size()})
                    .has_value());
    EXPECT_FALSE(OpenMaterialFile(files, "bad.material.json").has_value());
    assets::PbrMaterial bad;
    bad.roughness = -1.0f;
    EXPECT_FALSE(SaveMaterialFile(files, "x.material.json", bad).has_value());
    EXPECT_FALSE(files.Exists("x.material.json"));
}
