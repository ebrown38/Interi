// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ModuleManagerPanel.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <map>

namespace interi::editor
{

namespace
{

constexpr ImVec4 kGreen(0.45f, 0.85f, 0.5f, 1.0f);
constexpr ImVec4 kAmber(0.95f, 0.75f, 0.3f, 1.0f);
constexpr ImVec4 kRed(0.95f, 0.45f, 0.4f, 1.0f);
constexpr ImVec4 kBlue(0.55f, 0.75f, 1.0f, 1.0f);

constexpr const char* kTrustWarning =
    "Native code runs inside the editor and the game with full trust: it can read and write any file and do anything "
    "you can. There is no sandbox. Only enable modules from authors you trust.";

ImVec4 StatusColor(ModuleStatus status)
{
    switch (status)
    {
    case ModuleStatus::Enabled:
        return kGreen;
    case ModuleStatus::Implicit:
    case ModuleStatus::Engine:
        return kBlue;
    case ModuleStatus::Available:
        return ImGui::GetStyleColorVec4(ImGuiCol_Text);
    case ModuleStatus::Incompatible:
        return kRed;
    }
    return kGreen;
}

std::string Join(const std::vector<std::string>& items)
{
    std::string text;
    for (const std::string& item : items)
    {
        text += text.empty() ? item : ", " + item;
    }
    return text;
}

std::string Describe(const ModuleManagerModel* model, const std::string& id)
{
    const ModuleEntry* entry = model != nullptr ? model->Find(id) : nullptr;
    return entry != nullptr ? std::format("{} ({})", entry->name.empty() ? id : entry->name, id) : id;
}

void Resolve(ModuleManagerState& state, const ModuleManagerView& view, const ModuleAction& action,
             ModuleManagerRequests& requests)
{
    if (!view.plan)
    {
        return;
    }
    Result<ModuleChange> change = view.plan(action);
    if (!change)
    {
        state.refusal = change.error().message;
        return;
    }
    state.refusal.clear();
    if (NeedsConfirmation(*change))
    {
        state.pending = ModuleManagerState::Pending{.action = action, .change = std::move(*change)};
    }
    else
    {
        requests.apply = std::move(*change);
    }
}

void DrawConfirmation(ModuleManagerState& state, const ModuleManagerView& view, ModuleManagerRequests& requests)
{
    if (state.confirm && state.pending)
    {
        requests.trust = state.pending->change.needsTrust;
        requests.apply = std::move(state.pending->change);
        state.pending.reset();
    }
    if (state.cancel)
    {
        state.pending.reset();
    }
    state.confirm = false;
    state.cancel = false;
    if (!state.pending)
    {
        return;
    }
    const char* title = "Apply module change?";
    if (!ImGui::IsPopupOpen(title))
    {
        ImGui::OpenPopup(title);
    }
    if (!ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        return;
    }
    const ModuleChange& change = state.pending->change;
    for (const std::string& id : change.added)
    {
        ImGui::TextColored(kGreen, "+ %s", Describe(view.model, id).c_str());
    }
    for (const std::string& id : change.removed)
    {
        ImGui::TextColored(kRed, "- %s", Describe(view.model, id).c_str());
    }
    if (change.PhysicsEngineChanged())
    {
        ImGui::TextColored(kAmber, "Physics Engine: %s -> %s",
                           change.physicsEngineBefore.empty() ? "(none)" : change.physicsEngineBefore.c_str(),
                           change.physicsEngineAfter.empty() ? "(none)" : change.physicsEngineAfter.c_str());
    }
    if (!change.needsTrust.empty())
    {
        ImGui::Separator();
        ImGui::TextColored(kAmber, "Native code from:");
        for (const std::string& id : change.needsTrust)
        {
            const ModuleEntry* entry = view.model != nullptr ? view.model->Find(id) : nullptr;
            ImGui::BulletText("%s  by %s, license %s", id.c_str(), entry != nullptr ? entry->author.c_str() : "?",
                              entry != nullptr ? entry->license.c_str() : "?");
        }
        ImGui::PushTextWrapPos(480.0f);
        ImGui::TextUnformatted(kTrustWarning);
        ImGui::PopTextWrapPos();
    }
    ImGui::Separator();
    if (ImGui::Button(change.needsTrust.empty() ? "Apply" : "Trust and apply"))
    {
        state.confirm = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel"))
    {
        state.cancel = true;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void DrawModuleDetails(const ModuleEntry& entry)
{
    ImGui::SeparatorText(entry.name.empty() ? entry.id.c_str() : entry.name.c_str());
    ImGui::TextDisabled("%s  version %s  %s", entry.id.c_str(), entry.version.ToString().c_str(),
                        std::string(sdk::ToString(entry.kind)).c_str());
    ImGui::Text("Author: %s", entry.author.c_str());
    ImGui::Text("License: %s", entry.license.c_str());
    if (!entry.description.empty())
    {
        ImGui::TextWrapped("%s", entry.description.c_str());
    }
    if (entry.nativeCode)
    {
        ImGui::TextColored(entry.trusted ? kGreen : kAmber, "%s",
                           entry.trusted ? "Native code: trusted" : "Native code: not confirmed yet");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", kTrustWarning);
        }
    }
    else
    {
        ImGui::TextDisabled("Content only: no native code.");
    }
    if (!entry.requiredBy.empty())
    {
        ImGui::Text("Required by: %s", Join(entry.requiredBy).c_str());
    }
    if (!entry.reason.empty())
    {
        ImGui::TextColored(entry.status == ModuleStatus::Incompatible ? kRed : kBlue, "%s", entry.reason.c_str());
    }
}

void DrawModulesTab(ModuleManagerState& state, const ModuleManagerView& view)
{
    if (view.model == nullptr)
    {
        return;
    }
    if (!view.model->graphError.empty())
    {
        ImGui::TextColored(kRed, "The project doesn't resolve: %s", view.model->graphError.c_str());
    }
    if (!state.refusal.empty())
    {
        ImGui::TextColored(kRed, "Refused: %s", state.refusal.c_str());
    }
    ImGui::TextDisabled("Drag enabled modules to reorder: the top one wins where two provide the same item.");

    std::size_t enabledCount = 0;
    for (const ModuleEntry& entry : view.model->entries)
    {
        enabledCount += entry.status == ModuleStatus::Enabled ? 1 : 0;
    }
    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("modules", 6, flags, ImVec2(0.0f, 240.0f)))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableSetupColumn("Module");
        ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableHeadersRow();
        for (const ModuleEntry& entry : view.model->entries)
        {
            ImGui::PushID(entry.id.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            if (entry.position >= 0)
            {
                ImGui::Text("%d", entry.position + 1);
            }
            ImGui::TableNextColumn();
            ImGui::PushStyleColor(ImGuiCol_Text, StatusColor(entry.status));
            if (ImGui::Selectable(entry.name.empty() ? entry.id.c_str() : entry.name.c_str(),
                                  state.selected == entry.id,
                                  ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
            {
                state.selected = entry.id;
            }
            ImGui::PopStyleColor();
            if (entry.status == ModuleStatus::Enabled)
            {
                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload("MODULE_ROW", entry.id.c_str(), entry.id.size() + 1);
                    ImGui::TextUnformatted(entry.id.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("MODULE_ROW"))
                    {
                        state.Queue({.kind = ModuleAction::Kind::Move,
                                     .id = static_cast<const char*>(payload->Data),
                                     .position = static_cast<std::size_t>(entry.position)});
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            if (ImGui::IsItemHovered() && !entry.description.empty())
            {
                ImGui::SetTooltip("%s", entry.description.c_str());
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.version.ToString().c_str());
            ImGui::TableNextColumn();
            ImGui::TextColored(StatusColor(entry.status), "%s", std::string(ToString(entry.status)).c_str());
            if (ImGui::IsItemHovered() && !entry.reason.empty())
            {
                ImGui::SetTooltip("%s", entry.reason.c_str());
            }
            ImGui::TableNextColumn();
            if (!entry.nativeCode)
            {
                ImGui::TextDisabled("content");
            }
            else
            {
                ImGui::TextColored(entry.trusted ? kGreen : kAmber, "%s", entry.trusted ? "trusted" : "native");
            }
            ImGui::TableNextColumn();
            if (entry.status == ModuleStatus::Enabled)
            {
                const auto position = static_cast<std::size_t>(entry.position);
                ImGui::BeginDisabled(position == 0);
                if (ImGui::SmallButton("Up"))
                {
                    state.Queue({.kind = ModuleAction::Kind::Move, .id = entry.id, .position = position - 1});
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(position + 1 >= enabledCount);
                if (ImGui::SmallButton("Down"))
                {
                    state.Queue({.kind = ModuleAction::Kind::Move, .id = entry.id, .position = position + 1});
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::SmallButton("Disable"))
                {
                    state.Queue({.kind = ModuleAction::Kind::Disable, .id = entry.id, .position = 0});
                }
            }
            else if (entry.status == ModuleStatus::Available)
            {
                if (ImGui::SmallButton("Enable"))
                {
                    state.Queue({.kind = ModuleAction::Kind::Enable, .id = entry.id, .position = 0});
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (const ModuleEntry* selected = view.model->Find(state.selected))
    {
        DrawModuleDetails(*selected);
    }
}

void DrawGraphTab(ModuleManagerState& state, const ModuleManagerView& view)
{
    if (view.graph == nullptr || view.graph->nodes.empty())
    {
        ImGui::TextDisabled("No modules are loaded in the project.");
        return;
    }
    constexpr float kNodeWidth = 190.0f;
    constexpr float kNodeHeight = 34.0f;
    constexpr float kColumn = 240.0f;
    constexpr float kRow = 52.0f;
    int rows = 1;
    for (const GraphNodeView& node : view.graph->nodes)
    {
        rows = std::max(rows, node.row + 1);
    }
    ImGui::TextColored(kGreen, "required");
    ImGui::SameLine();
    ImGui::TextColored(kBlue, "optional");
    ImGui::SameLine();
    ImGui::TextColored(kAmber, "Physics Engine");
    ImGui::SameLine();
    ImGui::TextDisabled("Arrows point from a module to what must load before it.");
    if (!ImGui::BeginChild("graph", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImGui::EndChild();
        return;
    }
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    const auto topLeft = [&](const GraphNodeView& node)
    {
        return ImVec2(origin.x + 10.0f + kColumn * static_cast<float>(node.layer),
                      origin.y + 10.0f + kRow * static_cast<float>(node.row));
    };
    std::map<std::string, const GraphNodeView*> byId;
    for (const GraphNodeView& node : view.graph->nodes)
    {
        byId[node.id] = &node;
    }
    for (const GraphEdgeView& edge : view.graph->edges)
    {
        const auto from = byId.find(edge.from);
        const auto to = byId.find(edge.to);
        if (from == byId.end() || to == byId.end())
        {
            continue;
        }
        const ImVec2 a = topLeft(*to->second);
        const ImVec2 b = topLeft(*from->second);
        const ImVec2 start(a.x + kNodeWidth, a.y + kNodeHeight * 0.5f);
        const ImVec2 end(b.x, b.y + kNodeHeight * 0.5f);
        const ImVec4 color = edge.kind == GraphEdgeKind::Requires   ? kGreen
                             : edge.kind == GraphEdgeKind::Optional ? kBlue
                                                                    : kAmber;
        draw->AddBezierCubic(start, ImVec2(start.x + 40.0f, start.y), ImVec2(end.x - 40.0f, end.y), end,
                             ImGui::ColorConvertFloat4ToU32(color), 2.0f);
        draw->AddCircleFilled(end, 4.0f, ImGui::ColorConvertFloat4ToU32(color));
    }
    for (const GraphNodeView& node : view.graph->nodes)
    {
        const ImVec2 position = topLeft(node);
        ImGui::SetCursorScreenPos(position);
        ImGui::PushID(node.id.c_str());
        if (ImGui::InvisibleButton("node", ImVec2(kNodeWidth, kNodeHeight)))
        {
            state.selected = node.id;
        }
        ImGui::PopID();
        const bool selected = state.selected == node.id;
        const ImVec4 fill = node.kind == sdk::ModuleKind::PhysicsEngine ? ImVec4(0.30f, 0.25f, 0.10f, 1.0f)
                            : node.implicit                             ? ImVec4(0.12f, 0.20f, 0.30f, 1.0f)
                                                                        : ImVec4(0.12f, 0.26f, 0.16f, 1.0f);
        draw->AddRectFilled(position, ImVec2(position.x + kNodeWidth, position.y + kNodeHeight),
                            ImGui::ColorConvertFloat4ToU32(fill), 4.0f);
        draw->AddRect(position, ImVec2(position.x + kNodeWidth, position.y + kNodeHeight),
                      selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(120, 120, 120, 255), 4.0f);
        draw->AddText(ImVec2(position.x + 8.0f, position.y + 4.0f), IM_COL32(235, 235, 235, 255),
                      (node.name.empty() ? node.id : node.name).c_str());
        draw->AddText(ImVec2(position.x + 8.0f, position.y + 18.0f), IM_COL32(150, 150, 150, 255),
                      std::format("{}  {}", sdk::ToString(node.kind),
                                  node.implicit ? "implicit" : "priority " + std::to_string(node.priority))
                          .c_str());
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("%s", node.id.c_str());
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + kRow * static_cast<float>(rows) + 20.0f));
    ImGui::Dummy(ImVec2(kColumn * static_cast<float>(view.graph->layerCount), 1.0f));
    ImGui::EndChild();
}

void DrawConflictsTab(ModuleManagerState& state, const ModuleManagerView& view)
{
    if (view.conflicts == nullptr)
    {
        return;
    }
    ImGui::SeparatorText("Items more than one module provides");
    if (view.conflicts->conflicts.empty())
    {
        ImGui::TextDisabled("None. Events and hooks are listed once the project's libraries are loaded.");
    }
    else if (ImGui::BeginTable("conflicts", 5,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable))
    {
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
        ImGui::TableSetupColumn("Id");
        ImGui::TableSetupColumn("Winner");
        ImGui::TableSetupColumn("Shadowed");
        ImGui::TableSetupColumn("##plugin", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();
        int row = 0;
        for (const ItemConflict& conflict : view.conflicts->conflicts)
        {
            ImGui::PushID(row++);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(std::string(ToString(conflict.kind)).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(conflict.id.c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", conflict.shared ? "Shared id: the highest provider wins outright."
                                                        : "Local id: replaced only by an explicit override.");
            }
            ImGui::TableNextColumn();
            ImGui::TextColored(kGreen, "%s%s", conflict.winner.c_str(), conflict.overridden ? " (override)" : "");
            ImGui::TableNextColumn();
            std::string shadowed;
            for (const ItemProvider& provider : conflict.shadowed)
            {
                shadowed += (shadowed.empty() ? "" : ", ") + provider.provider;
            }
            ImGui::TextDisabled("%s", shadowed.c_str());
            ImGui::TableNextColumn();
            const bool pluginable = conflict.kind == ItemKind::Hook || conflict.kind == ItemKind::Event;
            ImGui::BeginDisabled(!pluginable);
            if (ImGui::SmallButton("Plugin..."))
            {
                state.SetPluginTarget(
                    conflict.kind == ItemKind::Hook ? PluginTargetKind::Hook : PluginTargetKind::Event, conflict.id);
            }
            ImGui::EndDisabled();
            if (!pluginable && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            {
                ImGui::SetTooltip("Plugins override hooks and Events.");
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::SeparatorText("Declared overrides");
    if (view.conflicts->overrides.empty())
    {
        ImGui::TextDisabled("No module declares an override.");
        return;
    }
    if (ImGui::BeginTable("overrides", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Module");
        ImGui::TableSetupColumn("Target");
        ImGui::TableSetupColumn("State");
        ImGui::TableHeadersRow();
        for (const DeclaredOverrideStatus& status : view.conflicts->overrides)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(status.owner.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(status.target.c_str());
            ImGui::TableNextColumn();
            if (status.applied)
            {
                ImGui::TextColored(kGreen, "applied");
            }
            else if (status.targetMissing)
            {
                ImGui::TextColored(kAmber, "target module not in the project");
            }
            else
            {
                ImGui::TextColored(kAmber, "declared, nothing provided yet");
            }
        }
        ImGui::EndTable();
    }
}

void DrawPluginTab(ModuleManagerState& state, const ModuleManagerView& view, ModuleManagerRequests& requests)
{
    ImGui::SeparatorText("New Plugin");
    static constexpr const char* kKinds[] = {"Hook", "Event"};
    ImGui::SetNextItemWidth(120.0f);
    ImGui::Combo("Overrides", &state.pluginTargetKind, kKinds, 2);
    ImGui::InputText("Target id", &state.pluginTarget);
    ImGui::InputText("Module id", &state.pluginId);
    ImGui::InputText("Name", &state.pluginName);
    ImGui::InputText("Author", &state.pluginAuthor);
    ImGui::InputText("License", &state.pluginLicense);
    ImGui::InputText("Folder", &state.pluginFolder);
    ImGui::BeginDisabled(state.pluginTarget.empty() || state.pluginFolder.empty());
    if (ImGui::Button("Generate stub"))
    {
        PluginRequest request;
        request.desc.id = state.pluginId;
        request.desc.name = state.pluginName;
        request.desc.author = state.pluginAuthor;
        request.desc.license = state.pluginLicense;
        request.desc.target.kind = static_cast<PluginTargetKind>(state.pluginTargetKind);
        request.desc.target.id = state.pluginTarget;
        request.folder = state.pluginFolder;
        requests.createPlugin = std::move(request);
    }
    ImGui::EndDisabled();
    if (!state.pluginStatus.empty())
    {
        ImGui::TextWrapped("%s", state.pluginStatus.c_str());
    }
    ImGui::TextDisabled("Pick the target from the Conflicts tab or the Chain Editor palette (right-click an event).");

    ImGui::SeparatorText("Hot reload loop");
    ImGui::InputText("Build command", &state.buildCommand);
    ImGui::Checkbox("Reload and replay when a module library is rebuilt", &state.autoReload);
    ImGui::BeginDisabled(!view.canBuild && !state.buildCommand.empty());
    if (ImGui::Button(state.buildCommand.empty() ? "Reload and replay" : "Build, reload and replay"))
    {
        requests.runLoop = true;
    }
    ImGui::EndDisabled();
    if (!state.loopOutput.empty())
    {
        ImGui::TextColored(state.loopFailed ? kRed : kGreen, "%s", state.loopFailed ? "Failed" : "Reloaded");
        ImGui::InputTextMultiline("##loop", &state.loopOutput, ImVec2(-1.0f, 140.0f), ImGuiInputTextFlags_ReadOnly);
    }
}

void DrawDocsTab(ModuleManagerState& state, const ModuleManagerView& view, ModuleManagerRequests& requests)
{
    if (view.model == nullptr)
    {
        return;
    }
    if (ImGui::BeginCombo("Module", state.docsModule.empty() ? "(choose)" : state.docsModule.c_str()))
    {
        for (const ModuleEntry& entry : view.model->entries)
        {
            if (ImGui::Selectable(entry.id.c_str(), entry.id == state.docsModule))
            {
                state.docsModule = entry.id;
                requests.generateDocs = entry.id;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(state.docsText.empty());
    if (ImGui::Button("Save to project"))
    {
        requests.saveDocs = state.docsModule;
    }
    ImGui::EndDisabled();
    if (!state.docsText.empty())
    {
        ImGui::InputTextMultiline("##docs", &state.docsText, ImVec2(-1.0f, -1.0f), ImGuiInputTextFlags_ReadOnly);
    }
}

} // namespace

bool NeedsConfirmation(const ModuleChange& change) noexcept
{
    return !change.needsTrust.empty() || change.PhysicsEngineChanged();
}

ModuleManagerRequests DrawModuleManagerPanel(ModuleManagerState& state, const ModuleManagerView& view)
{
    ModuleManagerRequests requests;
    // Confirmation first: a queued action may then open the next one.
    if (state.queued)
    {
        const ModuleAction action = std::move(*state.queued);
        state.queued.reset();
        Resolve(state, view, action, requests);
    }
    if (!state.show)
    {
        DrawConfirmation(state, view, requests);
        return requests;
    }
    ImGui::SetNextWindowSize(ImVec2(760.0f, 520.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kModuleManagerWindow, &state.show))
    {
        if (ImGui::BeginTabBar("module_manager_tabs"))
        {
            const auto flags = [&](ModuleManagerTab tab)
            {
                const bool requested = state.requestedTab && *state.requestedTab == tab;
                return requested ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem("Modules", nullptr, flags(ModuleManagerTab::Modules)))
            {
                DrawModulesTab(state, view);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Dependency graph", nullptr, flags(ModuleManagerTab::Graph)))
            {
                DrawGraphTab(state, view);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Conflicts", nullptr, flags(ModuleManagerTab::Conflicts)))
            {
                DrawConflictsTab(state, view);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Plugin", nullptr, flags(ModuleManagerTab::Plugin)))
            {
                DrawPluginTab(state, view, requests);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Docs", nullptr, flags(ModuleManagerTab::Docs)))
            {
                DrawDocsTab(state, view, requests);
                ImGui::EndTabItem();
            }
            state.requestedTab.reset();
            ImGui::EndTabBar();
        }
        DrawConfirmation(state, view, requests);
    }
    ImGui::End();
    return requests;
}

} // namespace interi::editor
