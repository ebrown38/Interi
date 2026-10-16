// SPDX-License-Identifier: MIT
#include <interi/editor/EditorMath.h>
#include <interi/editor/Inspector.h>
#include <interi/editor_ui/EditorPanels.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <cstdint>
#include <string>

namespace interi::editor
{

namespace
{

/// Undo merge key for the widget just drawn: its ImGui id combined with the entity.
[[nodiscard]] std::uint64_t MergeKey(EditorEntityId id) noexcept
{
    return (std::uint64_t{ImGui::GetItemID()} << 32) ^ id ^ 0x9E3779B97F4A7C15ull;
}

/// Draws one field's widget; returns the edited value when it changed this frame.
[[nodiscard]] std::optional<FieldValue> FieldWidget(const FieldInfo& field, const FieldValue& value)
{
    const char* label = field.name.c_str();
    switch (field.type)
    {
    case FieldType::Bool:
    {
        bool edited = std::get<bool>(value);
        if (ImGui::Checkbox(label, &edited))
        {
            return edited;
        }
        break;
    }
    case FieldType::U32:
    {
        std::uint32_t edited = std::get<std::uint32_t>(value);
        if (ImGui::DragScalar(label, ImGuiDataType_U32, &edited, 1.0f))
        {
            return edited;
        }
        break;
    }
    case FieldType::Float:
    {
        float edited = std::get<float>(value);
        if (ImGui::DragFloat(label, &edited, 0.05f))
        {
            return edited;
        }
        break;
    }
    case FieldType::Vec3:
    {
        Vec3 edited = std::get<Vec3>(value);
        if (ImGui::DragFloat3(label, &edited.x, 0.05f))
        {
            return edited;
        }
        break;
    }
    case FieldType::Quat:
    {
        Vec3 degrees = EulerDegrees(std::get<Quat>(value));
        if (ImGui::DragFloat3(label, &degrees.x, 0.5f))
        {
            return FromEulerDegrees(degrees);
        }
        break;
    }
    case FieldType::String:
    {
        // ImGui keeps its own buffer while the field is active; the edit is recorded once, when it is left.
        std::string edited = std::get<std::string>(value);
        (void)ImGui::InputText(label, &edited);
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            return edited;
        }
        break;
    }
    }
    return std::nullopt;
}

} // namespace

ScreenRect DrawDockSpace(bool buildDefaultLayout)
{
    const ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
    if (buildDefaultLayout)
    {
        ImGui::DockBuilderRemoveNodeChildNodes(dockspace);
        ImGuiID centre = dockspace;
        const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.2f, nullptr, &centre);
        const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.28f, nullptr, &centre);
        const ImGuiID bottom = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Down, 0.25f, nullptr, &centre);
        ImGuiID rightTop = right;
        const ImGuiID rightBottom = ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.45f, nullptr, &rightTop);
        ImGui::DockBuilderDockWindow(kOutlinerWindow, left);
        ImGui::DockBuilderDockWindow(kInspectorWindow, rightTop);
        ImGui::DockBuilderDockWindow(kToolsWindow, rightBottom);
        ImGui::DockBuilderDockWindow(kAssetsWindow, bottom);
        ImGui::DockBuilderDockWindow(kViewportWindow, centre);
        ImGui::DockBuilderFinish(dockspace);
    }
    ScreenRect rect;
    if (const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(dockspace))
    {
        rect = {.x = central->Pos.x, .y = central->Pos.y, .width = central->Size.x, .height = central->Size.y};
    }
    else
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        rect = {.x = viewport->WorkPos.x,
                .y = viewport->WorkPos.y,
                .width = viewport->WorkSize.x,
                .height = viewport->WorkSize.y};
    }
    return rect;
}

MenuRequests DrawMainMenu(EditorDocument& document)
{
    MenuRequests requests;
    if (!ImGui::BeginMainMenuBar())
    {
        return requests;
    }
    if (ImGui::BeginMenu("File"))
    {
        requests.newLevel = ImGui::MenuItem("New");
        requests.save = ImGui::MenuItem("Save", "Ctrl+S", false, !document.Path().empty());
        ImGui::Separator();
        requests.quit = ImGui::MenuItem("Quit");
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        UndoStack& undo = document.Undo();
        const std::string undoLabel = "Undo " + std::string(undo.UndoLabel());
        const std::string redoLabel = "Redo " + std::string(undo.RedoLabel());
        if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, undo.CanUndo()))
        {
            (void)undo.Undo();
        }
        if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, undo.CanRedo()))
        {
            (void)undo.Redo();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete", "Del", false, document.Selection() != 0))
        {
            (void)document.DeleteSelection();
        }
        ImGui::EndMenu();
    }
    const std::string title =
        (document.Path().empty() ? std::string("(untitled)") : document.Path()) + (document.IsDirty() ? " *" : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(title.c_str()).x - 16.0f);
    ImGui::TextUnformatted(title.c_str());
    ImGui::EndMainMenuBar();
    return requests;
}

void HandleShortcuts(EditorDocument& document, MenuRequests& requests)
{
    if (ImGui::GetIO().WantTextInput)
    {
        return;
    }
    UndoStack& undo = document.Undo();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
    {
        (void)undo.Undo();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) ||
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
    {
        (void)undo.Redo();
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
    {
        requests.save = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
    {
        (void)document.DeleteSelection();
    }
}

void DrawOutliner(EditorDocument& document)
{
    if (ImGui::Begin(kOutlinerWindow))
    {
        const EditorEntityId selection = document.Selection();
        for (const OutlinerRow& row : OutlinerRows(document.Scene()))
        {
            ImGui::PushID(static_cast<int>(row.id));
            if (ImGui::Selectable(row.label.c_str(), row.id == selection))
            {
                document.Select(row.id);
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

ToolRequests DrawToolsPanel(ToolSettings& settings, const EditorDocument& document)
{
    ToolRequests requests;
    if (ImGui::Begin(kToolsWindow))
    {
        int mode = static_cast<int>(settings.mode);
        ImGui::RadioButton("Move (W)", &mode, static_cast<int>(GizmoMode::Translate));
        ImGui::SameLine();
        ImGui::RadioButton("Rotate (E)", &mode, static_cast<int>(GizmoMode::Rotate));
        ImGui::SameLine();
        ImGui::RadioButton("Size (R)", &mode, static_cast<int>(GizmoMode::Scale));
        settings.mode = static_cast<GizmoMode>(mode);
        ImGui::Checkbox("Snap", &settings.snap);
        ImGui::BeginDisabled(!settings.snap);
        ImGui::DragFloat("Move step", &settings.moveSnap, 0.01f, 0.01f, 10.0f, "%.2f m");
        ImGui::DragFloat("Angle step", &settings.rotateSnapDegrees, 0.5f, 1.0f, 90.0f, "%.0f deg");
        ImGui::DragFloat("Size step", &settings.sizeSnap, 0.01f, 0.01f, 10.0f, "%.2f m");
        ImGui::EndDisabled();
        ImGui::Separator();
        requests.addStaticBox = ImGui::Button("Add static box");
        ImGui::SameLine();
        requests.addSpawnPoint = ImGui::Button("Add spawn point");
        ImGui::BeginDisabled(document.Selection() == 0);
        requests.focusSelection = ImGui::Button("Focus selection (F)");
        ImGui::EndDisabled();
    }
    ImGui::End();
    return requests;
}

AssetRequests DrawAssetsPanel(std::span<const AssetEntry> assets)
{
    AssetRequests requests;
    if (ImGui::Begin(kAssetsWindow))
    {
        requests.refresh = ImGui::Button("Refresh");
        ImGui::SameLine();
        ImGui::TextUnformatted("Drag destructibles into the viewport; double-click a level to open it.");
        for (const AssetEntry& asset : assets)
        {
            const char* kind = asset.kind == AssetKind::Destructible ? "[destructible] "
                               : asset.kind == AssetKind::Source     ? "[gltf]         "
                                                                     : "[level]        ";
            const std::string label = kind + asset.path;
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
            if (asset.kind == AssetKind::Level && ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                requests.openLevel = asset.path;
            }
            if (asset.kind != AssetKind::Level && ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kAssetPayload, asset.path.c_str(), asset.path.size() + 1);
                ImGui::TextUnformatted(asset.path.c_str());
                ImGui::EndDragDropSource();
            }
        }
    }
    ImGui::End();
    return requests;
}

void DrawInspector(EditorDocument& document)
{
    if (ImGui::Begin(kInspectorWindow))
    {
        const EditorEntityId id = document.Selection();
        const auto snapshot = id == 0 ? std::nullopt : document.Scene().Snapshot(id);
        if (!snapshot)
        {
            ImGui::TextUnformatted("Nothing selected.");
        }
        else
        {
            for (const ComponentSnapshot& component : snapshot->components)
            {
                const TypeReflection* reflection = document.Reflection().Find(component.component);
                if (reflection == nullptr ||
                    !ImGui::CollapsingHeader(component.component.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    continue;
                }
                ImGui::PushID(component.component.c_str());
                for (std::size_t i = 0; i < reflection->fields.size(); ++i)
                {
                    const FieldInfo& field = reflection->fields[i];
                    if (const auto edited = FieldWidget(field, component.values[i]))
                    {
                        (void)EditField(document.Scene(), document.Undo(), id, component.component, field.name, *edited,
                                        MergeKey(id));
                    }
                    if (ImGui::IsItemDeactivated())
                    {
                        document.Undo().Seal();
                    }
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

} // namespace interi::editor
