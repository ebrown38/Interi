// SPDX-License-Identifier: MIT
#include <interi/editor/AssetBrowser.h>
#include <interi/editor/MaterialEditing.h>
#include <interi/editor/MaterialPreview.h>
#include <interi/editor/Thumbnails.h>
#include <interi/editor_ui/AssetPanels.h>

#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <format>

namespace interi::editor
{

namespace
{

using asset_import::ModelImportSettings;

constexpr std::array<const char*, 2> kUpNames = {"Y up", "Z up"};
constexpr std::array<const char*, 2> kHandedNames = {"Right-handed", "Left-handed"};
constexpr std::array<const char*, 3> kNormalNames = {"From file", "Smooth", "Flat"};
constexpr std::array<const char*, 2> kSpaceNames = {"Linear", "sRGB"};
constexpr std::array<const char*, 4> kCompressionNames = {"None", "BC1 (colour)", "BC4 (one channel)", "BC5 (normals)"};
constexpr std::array<const char*, 3> kAlphaNames = {"Opaque", "Mask", "Blend"};

template <std::size_t N, typename Enum>
bool EnumCombo(const char* label, const std::array<const char*, N>& names, Enum& value)
{
    bool changed = false;
    const auto current = static_cast<std::size_t>(value);
    if (ImGui::BeginCombo(label, current < N ? names[current] : "?"))
    {
        for (std::size_t i = 0; i < N; ++i)
        {
            if (ImGui::Selectable(names[i], i == current))
            {
                value = static_cast<Enum>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

/// Records an edit made through `edit(copy)`; rejected edits show their reason.
template <typename Edit>
bool Edited(MaterialEditorState& state, const char* field, Edit&& edit)
{
    assets::PbrMaterial copy = state.material;
    edit(copy);
    if (auto result = EditMaterial(state.undo, state.material, std::move(copy), field); !result)
    {
        state.message = result.error().message;
        return false;
    }
    state.message.clear();
    state.previewStale = true;
    return true;
}

bool TextureSlot(MaterialEditorState& state, const char* label, const char* field,
                 assets::TextureRef assets::PbrMaterial::* slot)
{
    bool edited = false;
    std::string path = (state.material.*slot).path;
    ImGui::PushID(field);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.0f);
    const bool changed = ImGui::InputTextWithHint("##path", label, &path);
    if (changed)
    {
        edited = Edited(state, field,
                        [&](assets::PbrMaterial& m)
                        {
                            (m.*slot).path = path;
                            (m.*slot).guid = {};
                        });
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        state.undo.Seal();
    }
    if (ImGui::BeginDragDropTarget())
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload))
        {
            const std::string dropped = static_cast<const char*>(payload->Data);
            if (dropped.ends_with(".itex"))
            {
                edited = Edited(state, field,
                                [&](assets::PbrMaterial& m)
                                {
                                    (m.*slot).path = TextureSourceOf(dropped);
                                    (m.*slot).guid = {};
                                });
                state.undo.Seal();
            }
            else
            {
                state.message = std::format("'{}' is not a texture", dropped);
            }
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!(state.material.*slot).IsSet());
    if (ImGui::SmallButton("Clear"))
    {
        edited = Edited(state, field, [&](assets::PbrMaterial& m) { m.*slot = {}; });
        state.undo.Seal();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return edited;
}

/// A slider / drag bound to `value` in `copy`; seals the undo merge run when released.
template <typename Set>
bool Slider(MaterialEditorState& state, const char* label, const char* field, float current, float min, float max,
            Set&& set)
{
    float value = current;
    bool edited = false;
    if (ImGui::SliderFloat(label, &value, min, max))
    {
        edited = Edited(state, field, [&](assets::PbrMaterial& m) { set(m, value); });
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        state.undo.Seal();
    }
    return edited;
}

} // namespace

void DrawPixels(ImDrawList& list, ImVec2 origin, float cell, std::span<const std::uint8_t> rgba, std::uint32_t width,
                std::uint32_t height)
{
    if (rgba.size() < std::size_t{width} * height * 4)
    {
        return;
    }
    for (std::uint32_t y = 0; y < height; ++y)
    {
        std::uint32_t x = 0;
        while (x < width)
        {
            const std::uint8_t* start = &rgba[(std::size_t{y} * width + x) * 4];
            if (start[3] == 0)
            {
                ++x;
                continue;
            }
            std::uint32_t end = x + 1;
            while (end < width && std::equal(start, start + 4, &rgba[(std::size_t{y} * width + end) * 4]))
            {
                ++end;
            }
            list.AddRectFilled(
                ImVec2(origin.x + static_cast<float>(x) * cell, origin.y + static_cast<float>(y) * cell),
                ImVec2(origin.x + static_cast<float>(end) * cell, origin.y + static_cast<float>(y + 1) * cell),
                IM_COL32(start[0], start[1], start[2], start[3]));
            x = end;
        }
    }
}

Result<void> OpenMaterialInEditor(MaterialEditorState& state, const IFileSystem& files, std::string path)
{
    assets::PbrMaterial material;
    bool readOnly = false;
    if (path.ends_with(".imat"))
    {
        INTERI_TRY_ASSIGN(loaded, assets::LoadMaterialAsset(files, path));
        material = std::move(loaded);
        readOnly = true;
    }
    else
    {
        INTERI_TRY_ASSIGN(opened, OpenMaterialFile(files, path));
        material = std::move(opened);
    }
    state.path = std::move(path);
    state.material = std::move(material);
    state.readOnly = readOnly;
    state.undo.Clear();
    state.undo.MarkClean();
    state.message.clear();
    state.previewStale = true;
    return {};
}

MaterialEditorResult DrawMaterialEditorPanel(MaterialEditorState& state, IFileSystem& files)
{
    MaterialEditorResult result;
    if (ImGui::Begin(kMaterialEditorWindow))
    {
        if (state.path.empty())
        {
            ImGui::TextUnformatted("Double-click a .material.json in the Assets panel to edit it.");
        }
        else
        {
            ImGui::TextUnformatted(state.path.c_str());
            if (state.readOnly)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(cooked: read-only)");
            }
            ImGui::BeginDisabled(!state.undo.CanUndo());
            if (ImGui::Button("Undo"))
            {
                (void)state.undo.Undo();
                state.previewStale = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!state.undo.CanRedo());
            if (ImGui::Button("Redo"))
            {
                (void)state.undo.Redo();
                state.previewStale = true;
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(state.readOnly);
            if (ImGui::Button(state.undo.IsDirty() ? "Save*" : "Save"))
            {
                const auto saved = SaveMaterialFile(files, state.path, state.material);
                state.message = saved ? std::format("saved {}", state.path) : saved.error().message;
                if (saved)
                {
                    state.undo.MarkClean();
                    result.saved = true;
                }
            }
            ImGui::EndDisabled();

            if (state.previewStale)
            {
                state.preview = RenderMaterialPreview(state.material, state.previewSize, MakeTextureSampler(files));
                state.previewStale = false;
            }
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const auto size = static_cast<float>(state.previewSize);
            ImDrawList& list = *ImGui::GetWindowDrawList();
            list.AddRectFilled(origin, ImVec2(origin.x + size, origin.y + size), IM_COL32(32, 34, 38, 255));
            DrawPixels(list, origin, 1.0f, state.preview.texels, state.preview.width, state.preview.height);
            ImGui::Dummy(ImVec2(size, size));
            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::BeginDisabled(state.readOnly);
            const assets::PbrMaterial& m = state.material;
            std::string name = m.name;
            if (ImGui::InputText("Name", &name))
            {
                result.edited |= Edited(state, "name", [&](assets::PbrMaterial& copy) { copy.name = name; });
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                state.undo.Seal();
            }
            float color[4] = {m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w};
            if (ImGui::ColorEdit4("Base color", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            {
                result.edited |= Edited(state, "baseColor", [&](assets::PbrMaterial& copy)
                                        { copy.baseColor = Vec4(color[0], color[1], color[2], color[3]); });
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                state.undo.Seal();
            }
            result.edited |= Slider(state, "Metallic", "metallic", m.metallic, 0.0f, 1.0f,
                                    [](assets::PbrMaterial& copy, float v) { copy.metallic = v; });
            result.edited |= Slider(state, "Roughness", "roughness", m.roughness, 0.0f, 1.0f,
                                    [](assets::PbrMaterial& copy, float v) { copy.roughness = v; });
            float emissive[3] = {m.emissive.x, m.emissive.y, m.emissive.z};
            if (ImGui::ColorEdit3("Emissive", emissive, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR))
            {
                result.edited |= Edited(state, "emissive", [&](assets::PbrMaterial& copy)
                                        { copy.emissive = Vec3(emissive[0], emissive[1], emissive[2]); });
            }
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                state.undo.Seal();
            }
            result.edited |= Slider(state, "Normal scale", "normalScale", m.normalScale, 0.0f, 4.0f,
                                    [](assets::PbrMaterial& copy, float v) { copy.normalScale = v; });
            result.edited |= Slider(state, "Occlusion", "occlusionStrength", m.occlusionStrength, 0.0f, 1.0f,
                                    [](assets::PbrMaterial& copy, float v) { copy.occlusionStrength = v; });
            assets::AlphaMode mode = m.alphaMode;
            if (EnumCombo("Alpha mode", kAlphaNames, mode))
            {
                result.edited |= Edited(state, "alphaMode", [&](assets::PbrMaterial& copy) { copy.alphaMode = mode; });
                state.undo.Seal();
            }
            if (m.alphaMode == assets::AlphaMode::Mask)
            {
                result.edited |= Slider(state, "Alpha cutoff", "alphaCutoff", m.alphaCutoff, 0.0f, 1.0f,
                                        [](assets::PbrMaterial& copy, float v) { copy.alphaCutoff = v; });
            }
            bool doubleSided = m.doubleSided;
            if (ImGui::Checkbox("Double sided", &doubleSided))
            {
                result.edited |=
                    Edited(state, "doubleSided", [&](assets::PbrMaterial& copy) { copy.doubleSided = doubleSided; });
                state.undo.Seal();
            }
            ImGui::EndGroup();
            ImGui::Separator();
            ImGui::TextUnformatted("Textures (drag from the Assets panel)");
            result.edited |=
                TextureSlot(state, "Base color (sRGB)", "baseColorTexture", &assets::PbrMaterial::baseColorTexture);
            result.edited |= TextureSlot(state, "Metallic (B) / roughness (G)", "metallicRoughnessTexture",
                                         &assets::PbrMaterial::metallicRoughnessTexture);
            result.edited |= TextureSlot(state, "Normal", "normalTexture", &assets::PbrMaterial::normalTexture);
            result.edited |=
                TextureSlot(state, "Occlusion (R)", "occlusionTexture", &assets::PbrMaterial::occlusionTexture);
            result.edited |=
                TextureSlot(state, "Emissive (sRGB)", "emissiveTexture", &assets::PbrMaterial::emissiveTexture);
            ImGui::EndDisabled();
            if (!state.message.empty())
            {
                ImGui::TextWrapped("%s", state.message.c_str());
            }
        }
    }
    ImGui::End();
    return result;
}

ImportPanelResult DrawImportPanel(asset_import::AssetDatabase* database, ImportPanelState& state,
                                  std::string_view selectedAsset)
{
    ImportPanelResult result;
    if (ImGui::Begin(kImportWindow))
    {
        const asset_import::AssetRecord* record = nullptr;
        if (database != nullptr && !selectedAsset.empty())
        {
            record = database->FindByPath(selectedAsset);
            if (record == nullptr)
            {
                record = database->FindByProduct(selectedAsset);
            }
        }
        if (record == nullptr)
        {
            ImGui::TextUnformatted(database == nullptr ? "No asset database for this project."
                                                       : "Select an imported asset in the Assets panel.");
        }
        else
        {
            if (state.source != record->path)
            {
                state.source = record->path;
                state.model = record->model;
                state.texture = record->texture;
                state.message.clear();
            }
            ImGui::Text("%s (%s)", record->path.c_str(), std::string(ToString(record->kind)).c_str());
            ImGui::TextDisabled("guid %s", assets::ToString(record->guid).c_str());
            if (record->kind == asset_import::SourceKind::Model)
            {
                ModelImportSettings& s = state.model;
                ImGui::DragFloat("Scale", &s.scale, 0.01f, 0.0001f, 10000.0f, "%.4f");
                EnumCombo("Up axis", kUpNames, s.up);
                EnumCombo("Handedness", kHandedNames, s.handedness);
                EnumCombo("Normals", kNormalNames, s.normals);
                ImGui::BeginDisabled(s.normals != asset_import::NormalMode::Smooth);
                ImGui::SliderFloat("Smoothing angle", &s.smoothingAngleDegrees, 0.0f, 180.0f);
                ImGui::EndDisabled();
                ImGui::Checkbox("Generate tangents", &s.generateTangents);
                ImGui::Checkbox("Optimize (meshoptimizer)", &s.optimize);
                ImGui::Checkbox("Materials", &s.importMaterials);
                ImGui::Checkbox("Skeletons", &s.importSkeletons);
                ImGui::Checkbox("Animations", &s.importAnimations);
            }
            else if (record->kind == asset_import::SourceKind::Texture)
            {
                asset_import::TextureImportSettings& s = state.texture;
                EnumCombo("Color space", kSpaceNames, s.colorSpace);
                ImGui::Checkbox("Generate mips", &s.generateMips);
                EnumCombo("Compression", kCompressionNames, s.compression);
                ImGui::Checkbox("Flip vertically", &s.flipY);
            }
            if (record->kind != asset_import::SourceKind::Material && ImGui::Button("Re-import"))
            {
                auto applied = record->kind == asset_import::SourceKind::Model
                                   ? database->SetModelSettings(record->path, state.model)
                                   : database->SetTextureSettings(record->path, state.texture);
                if (applied)
                {
                    auto cooked = database->Cook(record->path);
                    state.message = cooked
                                        ? std::format("{} {}", cooked.value() ? "imported" : "up to date", record->path)
                                        : cooked.error().message;
                    result.reimported = cooked.has_value();
                }
                else
                {
                    state.message = applied.error().message;
                }
            }
            if (!state.message.empty())
            {
                ImGui::TextWrapped("%s", state.message.c_str());
            }
            if (ImGui::TreeNode("Products"))
            {
                for (const asset_import::CookedProduct& product : record->products)
                {
                    ImGui::BulletText("%s: %s", product.kind.c_str(), product.path.c_str());
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Dependencies"))
            {
                for (const std::string& dependency : database->Dependencies(record->path))
                {
                    ImGui::BulletText("%s", dependency.c_str());
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Used by"))
            {
                for (const std::string& dependent : database->Dependents(record->path, true))
                {
                    ImGui::BulletText("%s", dependent.c_str());
                }
                ImGui::TreePop();
            }
        }
    }
    ImGui::End();
    return result;
}

} // namespace interi::editor
