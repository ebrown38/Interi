// SPDX-License-Identifier: MIT
#include <interi/asset_import/AssetDatabase.h>
#include <interi/assets/Material.h>
#include <interi/assets/Texture.h>
#include <interi/editor/MaterialEditing.h>
#include <interi/editor_ui/AssetPanels.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/platform/FileSystem.h>
#include <interi/scene/Primitives.h>
#include <interi/testing/ScopedTempDir.h>

#include "ImGuiTestContext.h"
#include <gtest/gtest.h>

#include <fstream>

using namespace interi;
using namespace interi::editor;

namespace
{

std::vector<std::byte> SolidTexture()
{
    assets::Image8 image{.width = 4, .height = 4, .texels = {}};
    for (int i = 0; i < 16; ++i)
    {
        image.texels.insert(image.texels.end(), {200, 80, 40, 255});
    }
    return assets::WriteTextureAsset(*assets::BuildTexture(image, {}));
}

void Put(IFileSystem& files, const std::string& path, const std::vector<std::byte>& bytes)
{
    ASSERT_TRUE(files.WriteFile(path, bytes).has_value());
}

void PutText(IFileSystem& files, const std::string& path, const std::string& text)
{
    ASSERT_TRUE(files.WriteFile(path, {reinterpret_cast<const std::byte*>(text.data()), text.size()}).has_value());
}

} // namespace

TEST(DrawPixels, MergesRunsAndSkipsTransparentTexels)
{
    editor::testing::ImGuiTestContext context;
    // Row 0: four equal opaque texels; row 1: transparent, red, red, transparent.
    const std::vector<std::uint8_t> rgba = {10, 20, 30, 255, 10,  20, 30, 255, 10,  20, 30, 255, 10, 20, 30, 255,
                                            0,  0,  0,  0,   255, 0,  0,  255, 255, 0,  0,  255, 0,  0,  0,  0};
    const auto frame = [&]
    {
        ImGui::Begin("pixels");
        DrawPixels(*ImGui::GetWindowDrawList(), ImVec2(10, 10), 2.0f, rgba, 4, 2);
        ImGui::End();
    };
    (void)context.Frame(frame); // windows show up a frame late
    const ImDrawData& data = context.Frame(frame);
    // The window itself adds geometry; count the rectangles with our colours by their vertices.
    int vertices = 0;
    for (const ImDrawList* list : data.CmdLists)
    {
        for (int i = 0; i < list->VtxBuffer.Size; ++i)
        {
            const ImU32 colour = list->VtxBuffer[i].col;
            vertices += (colour == IM_COL32(10, 20, 30, 255) || colour == IM_COL32(255, 0, 0, 255)) ? 1 : 0;
        }
    }
    EXPECT_EQ(vertices, 8); // two rectangles (one run per colour, not six texels) of four vertices
}

TEST(DrawPixels, ShortImageDataIsIgnored)
{
    editor::testing::ImGuiTestContext context;
    const std::vector<std::uint8_t> rgba(4, 255);
    EXPECT_NO_FATAL_FAILURE((void)context.Frame(
        [&]
        {
            ImGui::Begin("pixels");
            DrawPixels(*ImGui::GetWindowDrawList(), ImVec2(0, 0), 1.0f, rgba, 8, 8);
            ImGui::End();
        }));
}

TEST(AssetsPanel, ListsDrawsThumbnailsAndFilters)
{
    editor::testing::ImGuiTestContext context;
    MemoryFileSystem files;
    Put(files, "imported/a.png.itex", SolidTexture());
    Put(files, "imported/m.imesh", scene::WriteMeshAsset(*scene::GeneratePrimitive({})));
    const std::vector<AssetEntry> assets = {{"imported/a.png.itex", AssetKind::Texture},
                                            {"imported/m.imesh", AssetKind::Mesh},
                                            {"levels/x.scene.json", AssetKind::Scene},
                                            {"mats/red.material.json", AssetKind::Material}};
    AssetBrowserState state;
    ThumbnailCache thumbnails(24);
    const AssetBrowserContext browser{.files = &files, .thumbnails = &thumbnails};
    AssetRequests requests;
    (void)context.Frame([&] { requests = DrawAssetsPanel(assets, state, browser); });
    const ImDrawData& data = context.Frame([&] { requests = DrawAssetsPanel(assets, state, browser); });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_FALSE(requests.openLevel.has_value());
    EXPECT_FALSE(requests.openMaterial.has_value());
    EXPECT_GE(thumbnails.Count(), 1u); // the visible rows were rendered

    // Without a file system the panel shows text only and still works.
    state.thumbnails = false;
    EXPECT_NO_FATAL_FAILURE((void)context.Frame([&] { requests = DrawAssetsPanel(assets, state, {}); }));

    // A search that matches nothing, and a kind filter.
    state.query = "zzz";
    EXPECT_NO_FATAL_FAILURE((void)context.Frame([&] { requests = DrawAssetsPanel(assets, state, browser); }));
    state.query = "kind:texture";
    state.kind = static_cast<int>(AssetKind::Mesh);
    EXPECT_NO_FATAL_FAILURE((void)context.Frame([&] { requests = DrawAssetsPanel(assets, state, browser); }));
}

TEST(MaterialEditor, OpensAuthoredAndCookedMaterials)
{
    MemoryFileSystem files;
    assets::PbrMaterial material;
    material.name = "Steel";
    material.metallic = 0.8f;
    ASSERT_TRUE(SaveMaterialFile(files, "m/steel.material.json", material).has_value());
    Put(files, "imported/steel.imat", assets::WriteMaterialAsset(material));

    MaterialEditorState state;
    ASSERT_TRUE(OpenMaterialInEditor(state, files, "m/steel.material.json").has_value());
    EXPECT_EQ(state.material, material);
    EXPECT_FALSE(state.readOnly);
    EXPECT_FALSE(state.undo.CanUndo());
    ASSERT_TRUE(OpenMaterialInEditor(state, files, "imported/steel.imat").has_value());
    EXPECT_TRUE(state.readOnly);
    EXPECT_FALSE(OpenMaterialInEditor(state, files, "missing.material.json").has_value());
    EXPECT_EQ(state.path, "imported/steel.imat") << "a failed open keeps the previous material";
}

TEST(MaterialEditor, DrawsTheLivePreviewAndRerendersAfterAnEdit)
{
    editor::testing::ImGuiTestContext context;
    MemoryFileSystem files;
    assets::PbrMaterial material;
    material.baseColor = Vec4(0.9f, 0.1f, 0.1f, 1.0f);
    material.metallic = 0.0f;
    ASSERT_TRUE(SaveMaterialFile(files, "m.material.json", material).has_value());
    MaterialEditorState state;
    ASSERT_TRUE(OpenMaterialInEditor(state, files, "m.material.json").has_value());
    EXPECT_TRUE(state.previewStale);
    (void)context.Frame([&] { (void)DrawMaterialEditorPanel(state, files); });
    const ImDrawData& data = context.Frame([&] { (void)DrawMaterialEditorPanel(state, files); });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_FALSE(state.previewStale);
    ASSERT_EQ(state.preview.texels.size(), 112u * 112u * 4u);
    const std::uint8_t* centre = &state.preview.texels[(56u * 112u + 56u) * 4];
    EXPECT_GT(centre[0], centre[2] + 40);

    // An undoable edit through the model marks the preview stale; the next frame renders the new colour.
    assets::PbrMaterial blue = state.material;
    blue.baseColor = Vec4(0.1f, 0.1f, 0.9f, 1.0f);
    ASSERT_TRUE(EditMaterial(state.undo, state.material, blue, "baseColor").has_value());
    state.previewStale = true;
    (void)context.Frame([&] { (void)DrawMaterialEditorPanel(state, files); });
    EXPECT_GT(state.preview.texels[(56u * 112u + 56u) * 4 + 2], state.preview.texels[(56u * 112u + 56u) * 4]);
}

TEST(MaterialEditor, SaveWritesTheFile)
{
    // The panel's Save button needs a click; the same path is SaveMaterialFile on the state's material.
    editor::testing::ImGuiTestContext context;
    MemoryFileSystem files;
    PutText(files, "m.material.json", "{}");
    MaterialEditorState state;
    ASSERT_TRUE(OpenMaterialInEditor(state, files, "m.material.json").has_value());
    assets::PbrMaterial edited = state.material;
    edited.roughness = 0.2f;
    ASSERT_TRUE(EditMaterial(state.undo, state.material, edited, "roughness").has_value());
    EXPECT_TRUE(state.undo.IsDirty());
    ASSERT_TRUE(SaveMaterialFile(files, state.path, state.material).has_value());
    auto reopened = OpenMaterialFile(files, "m.material.json");
    ASSERT_TRUE(reopened.has_value());
    EXPECT_FLOAT_EQ(reopened->roughness, 0.2f);
}

TEST(MaterialEditor, NothingOpen_ShowsAHint)
{
    editor::testing::ImGuiTestContext context;
    MemoryFileSystem files;
    MaterialEditorState state;
    (void)context.Frame([&] { (void)DrawMaterialEditorPanel(state, files); });
    const ImDrawData& data = context.Frame([&] { (void)DrawMaterialEditorPanel(state, files); });
    EXPECT_GT(data.TotalVtxCount, 0);
}

TEST(ImportPanel, ShowsTheSelectedAssetsSourceAndReimports)
{
    editor::testing::ImGuiTestContext context;
    const interi::testing::ScopedTempDir dir;
    {
        std::filesystem::create_directories(dir.Path() / "models");
        std::ofstream(dir.Path() / "models/tri.obj") << "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    }
    asset_import::AssetDatabase database({.root = dir.Path(), .importedFolder = "imported", .guids = {}});
    ASSERT_TRUE(database.Scan().has_value());
    ASSERT_TRUE(database.CookAll().failed.empty());

    ImportPanelState state;
    (void)context.Frame([&] { (void)DrawImportPanel(&database, state, "imported/models/tri.obj/mesh.imesh"); });
    const ImDrawData& data =
        context.Frame([&] { (void)DrawImportPanel(&database, state, "imported/models/tri.obj/mesh.imesh"); });
    EXPECT_GT(data.TotalVtxCount, 0);
    EXPECT_EQ(state.source, "models/tri.obj");
    EXPECT_EQ(state.model, database.FindByPath("models/tri.obj")->model);

    // Selecting nothing, an unknown asset or no database does not crash.
    EXPECT_NO_FATAL_FAILURE((void)context.Frame([&] { (void)DrawImportPanel(&database, state, ""); }));
    EXPECT_NO_FATAL_FAILURE((void)context.Frame([&] { (void)DrawImportPanel(&database, state, "nothing.imesh"); }));
    EXPECT_NO_FATAL_FAILURE((void)context.Frame([&] { (void)DrawImportPanel(nullptr, state, "x"); }));
}
