// SPDX-License-Identifier: MIT
#include <interi/editor/Inspector.h>
#include <interi/editor/Prefabs.h>
#include <interi/editor_ui/DataPanels.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/ServicePanels.h>
#include <interi/editor_ui/ValueWidgets.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <string>
#include <utility>

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
    const values::ValueSchema schema{
        .type = field.type,
        .element = TypeOf(value) == FieldType::List ? std::get<values::ValueList>(value).element : FieldType::Float,
        .options = field.options,
        .meta = field.meta,
        .defaultValue = value};
    return ValueWidget(field.name.c_str(), schema, value);
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
        ImGui::DockBuilderDockWindow(kFractureWindow, rightBottom);
        ImGui::DockBuilderDockWindow(kStructureWindow, rightBottom);
        ImGui::DockBuilderDockWindow(kValuesWindow, rightBottom);
        ImGui::DockBuilderDockWindow(kCharactersWindow, rightBottom);
        ImGui::DockBuilderDockWindow(kDataTablesWindow, bottom);
        ImGui::DockBuilderDockWindow(kAssetsWindow, bottom);
        ImGui::DockBuilderDockWindow(kConsoleWindow, bottom);
        ImGui::DockBuilderDockWindow(kPreferencesWindow, rightBottom);
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
        requests.exportLevel = ImGui::MenuItem("Export level", nullptr, false, !document.ExportPath().empty());
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
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_D))
    {
        (void)DuplicateSelection(document);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_G))
    {
        requests.group = true;
    }
}

void DrawOutliner(EditorDocument& document)
{
    constexpr const char* kRowPayload = "INTERI_OBJECT";
    if (ImGui::Begin(kOutlinerWindow))
    {
        std::optional<std::pair<EditorEntityId, EditorEntityId>> reparent; // (object, new parent)
        for (const OutlinerRow& row : OutlinerRows(document.Scene()))
        {
            ImGui::PushID(static_cast<int>(row.id));
            const float indent = static_cast<float>(row.depth) * 14.0f; // Indent(0) would use the default width
            if (indent > 0.0f)
            {
                ImGui::Indent(indent);
            }
            const std::string label = (row.hasChildren ? "> " : "  ") + row.label;
            if (ImGui::Selectable(label.c_str(), document.IsSelected(row.id)))
            {
                if (ImGui::GetIO().KeyCtrl)
                {
                    document.ToggleSelected(row.id);
                }
                else
                {
                    document.Select(row.id);
                }
            }
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload(kRowPayload, &row.id, sizeof(row.id));
                ImGui::TextUnformatted(row.label.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRowPayload))
                {
                    EditorEntityId dragged = 0;
                    std::memcpy(&dragged, payload->Data, sizeof(dragged));
                    reparent.emplace(dragged, row.id);
                }
                ImGui::EndDragDropTarget();
            }
            if (indent > 0.0f)
            {
                ImGui::Unindent(indent);
            }
            ImGui::PopID();
        }
        // The space below the rows takes a dropped row as a root.
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 20.0f)));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kRowPayload))
            {
                EditorEntityId dragged = 0;
                std::memcpy(&dragged, payload->Data, sizeof(dragged));
                reparent.emplace(dragged, 0);
            }
            ImGui::EndDragDropTarget();
        }
        if (reparent)
        {
            (void)Reparent(document.Scene(), document.Undo(), reparent->first, reparent->second);
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
        ImGui::TextUnformatted("Add");
        for (const std::string& name : scene::PrimitiveShapeNames())
        {
            if (name != "Box")
            {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(name.c_str()).x + 16.0f)
                {
                    ImGui::NewLine();
                }
            }
            else
            {
                ImGui::SameLine();
            }
            if (ImGui::SmallButton(name.c_str()))
            {
                requests.addPrimitive = scene::ParsePrimitiveShape(name);
            }
        }
        requests.addSpawnPoint = ImGui::Button("Add spawn point");
        const std::size_t selected = document.SelectedRoots().size();
        ImGui::BeginDisabled(selected == 0);
        requests.focusSelection = ImGui::Button("Focus selection (F)");
        ImGui::Separator();
        ImGui::TextUnformatted("Selection");
        requests.group = ImGui::Button("Group");
        ImGui::SameLine();
        requests.ungroup = ImGui::Button("Ungroup");
        ImGui::SameLine();
        requests.duplicate = ImGui::Button("Duplicate");
        ImGui::SameLine();
        requests.snapSelection = ImGui::Button("Snap to grid");
        ImGui::SetNextItemWidth(80.0f);
        ImGui::InputInt("##count", &settings.arrayCount);
        settings.arrayCount = std::clamp(settings.arrayCount, 1, 1000);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        ImGui::DragFloat3("##offset", &settings.arrayOffset.x, 0.05f);
        ImGui::SameLine();
        requests.array = ImGui::Button("Array");
        ImGui::TextUnformatted("Axis");
        ImGui::SameLine();
        ImGui::RadioButton("X", &settings.axis, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Y", &settings.axis, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Z", &settings.axis, 2);
        ImGui::BeginDisabled(selected < 2);
        if (ImGui::Button("Align min"))
        {
            requests.align = AlignMode::Min;
        }
        ImGui::SameLine();
        if (ImGui::Button("Align centre"))
        {
            requests.align = AlignMode::Centre;
        }
        ImGui::SameLine();
        if (ImGui::Button("Align max"))
        {
            requests.align = AlignMode::Max;
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(selected < 3);
        requests.distribute = ImGui::Button("Distribute");
        ImGui::EndDisabled();
        ImGui::SameLine();
        requests.mirror = ImGui::Button("Mirror");
        if (ImGui::Button("Pivot to centre"))
        {
            requests.pivot = PivotPlacement::BoundsCentre;
        }
        ImGui::SameLine();
        if (ImGui::Button("Pivot to bottom"))
        {
            requests.pivot = PivotPlacement::BoundsBottom;
        }
        ImGui::BeginDisabled(selected < 2);
        ImGui::TextUnformatted("CSG (first selected is the base)");
        for (const modeling::CsgOperation operation :
             {modeling::CsgOperation::Union, modeling::CsgOperation::Subtract, modeling::CsgOperation::Intersect})
        {
            ImGui::SameLine();
            if (ImGui::SmallButton(std::string(modeling::ToString(operation)).c_str()))
            {
                requests.csg = operation;
            }
        }
        ImGui::EndDisabled();
        ImGui::TextUnformatted("Prefab");
        ImGui::SameLine();
        ImGui::BeginDisabled(selected != 1);
        requests.createPrefab = ImGui::SmallButton("Create from selection");
        ImGui::EndDisabled();
        ImGui::BeginDisabled(InstanceRootOf(document.Scene(), document.Selection()) == 0);
        ImGui::SameLine();
        requests.revertPrefab = ImGui::SmallButton("Revert");
        ImGui::SameLine();
        requests.applyPrefab = ImGui::SmallButton("Apply to prefab");
        ImGui::EndDisabled();
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
        ImGui::TextUnformatted(
            "Drag destructibles and meshes into the viewport; double-click a scene or level to open it.");
        for (const AssetEntry& asset : assets)
        {
            const bool openable = asset.kind == AssetKind::Level || asset.kind == AssetKind::Scene;
            const std::string label = std::format("[{}] {}", ToString(asset.kind), asset.path);
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick);
            if (openable && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                requests.openLevel = asset.path;
            }
            if (!openable && ImGui::BeginDragDropSource())
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

ScenePlayRequests DrawPlayControls(const ScenePlayStatus& status)
{
    ScenePlayRequests requests;
    if (!status.playing)
    {
        requests.play = ImGui::Button("Play (F5)");
    }
    else
    {
        requests.stop = ImGui::Button("Stop (F5)");
        ImGui::SameLine();
        requests.pause = ImGui::Button(status.paused ? "Resume (F6)" : "Pause (F6)");
        ImGui::SameLine();
        requests.step = ImGui::Button("Step (F10)");
        ImGui::SameLine();
        ImGui::Text("step %llu", static_cast<unsigned long long>(status.step));
    }
    if (!ImGui::GetIO().WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
        {
            (status.playing ? requests.stop : requests.play) = true;
        }
        if (status.playing && ImGui::IsKeyPressed(ImGuiKey_F6, false))
        {
            requests.pause = true;
        }
        if (status.playing && ImGui::IsKeyPressed(ImGuiKey_F10, false))
        {
            requests.step = true;
        }
    }
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
                if (component.component != scene::NameComponent::kComponentName &&
                    component.component != scene::ParentComponent::kComponentName &&
                    component.component != scene::LocalTransformComponent::kComponentName &&
                    ImGui::SmallButton("Remove component"))
                {
                    ImGui::PopID();
                    (void)RemoveComponent(document.Scene(), document.Undo(), id, component.component);
                    break; // the snapshot is stale now
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            if (ImGui::BeginCombo("##add", "Add component"))
            {
                for (const TypeReflection* type : document.Reflection().Types())
                {
                    const bool present =
                        std::ranges::contains(snapshot->components, type->name, &ComponentSnapshot::component);
                    if (!present && ImGui::Selectable(type->name.c_str()))
                    {
                        (void)AddComponent(document.Scene(), document.Undo(), id, type->name);
                    }
                }
                ImGui::EndCombo();
            }
            for (const scene::SceneComponent& kept : snapshot->kept)
            {
                ImGui::TextDisabled("%s (no loaded module reflects it; kept as written)", kept.name.c_str());
            }
        }
    }
    ImGui::End();
}

} // namespace interi::editor
