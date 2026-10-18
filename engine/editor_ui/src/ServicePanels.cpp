// SPDX-License-Identifier: MIT
#include <interi/editor_ui/ServicePanels.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <string>
#include <utility>

namespace interi::editor
{

void DrawConsolePanel(LogBuffer& log, ConsoleState& state)
{
    if (ImGui::Begin(kConsoleWindow))
    {
        static constexpr const char* kLevels[] = {"Trace", "Debug", "Info", "Warning", "Error", "Fatal"};
        ImGui::SetNextItemWidth(100.0f);
        ImGui::Combo("##level", &state.minimumLevel, kLevels, static_cast<int>(std::size(kLevels)));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputTextWithHint("##filter", "filter", &state.filter);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            log.Clear();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &state.autoScroll);
        ImGui::Separator();
        if (ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const LogBuffer::Record& record : log.Records())
            {
                if (static_cast<int>(record.level) < state.minimumLevel ||
                    (!state.filter.empty() && record.category.find(state.filter) == std::string::npos &&
                     record.message.find(state.filter) == std::string::npos))
                {
                    continue;
                }
                const ImVec4 color = record.level >= LogLevel::Error     ? ImVec4(1.0f, 0.4f, 0.4f, 1.0f)
                                     : record.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f)
                                                                         : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                const std::string line =
                    std::format("[{:10.3f}] [{}] {}", static_cast<double>(record.timestampNs) * 1e-9, record.category,
                                record.message);
                ImGui::TextColored(color, "%s", line.c_str());
            }
            if (state.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            {
                ImGui::SetScrollHereY(1.0f);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

bool DrawPreferencesPanel(Preferences& preferences, bool profilerBuild, bool profilerConnected)
{
    bool changed = false;
    if (ImGui::Begin(kPreferencesWindow))
    {
        ImGui::SeparatorText("Autosave");
        changed |= ImGui::Checkbox("Autosave", &preferences.autosave);
        int seconds = static_cast<int>(preferences.autosaveSeconds);
        if (ImGui::DragInt("Every (s)", &seconds, 1.0f, 10, 3600))
        {
            preferences.autosaveSeconds = static_cast<std::uint32_t>(std::clamp(seconds, 10, 3600));
            changed = true;
        }
        ImGui::SeparatorText("Play");
        changed |= ImGui::Checkbox("Keep changes made during play", &preferences.keepPlayValues);
        ImGui::SetItemTooltip("On: Values chains set and your edits stay when play stops. Off: both are undone.");
        ImGui::SeparatorText("Snapping defaults");
        changed |= ImGui::Checkbox("Snap", &preferences.snap);
        changed |= ImGui::DragFloat("Move step", &preferences.moveSnap, 0.01f, 0.01f, 10.0f, "%.2f m");
        changed |= ImGui::DragFloat("Angle step", &preferences.rotateSnapDegrees, 0.5f, 1.0f, 90.0f, "%.0f deg");
        changed |= ImGui::DragFloat("Size step", &preferences.sizeSnap, 0.01f, 0.01f, 10.0f, "%.2f");
        ImGui::SeparatorText("Profiler (Tracy)");
        ImGui::BeginDisabled(!profilerBuild);
        changed |= ImGui::Checkbox("Frame marks", &preferences.profilerFrameMarks);
        ImGui::EndDisabled();
        if (!profilerBuild)
        {
            ImGui::TextDisabled("Not in this build: use a *-profile preset (INTERI_PROFILING).");
        }
        else
        {
            ImGui::Text("Tracy: %s", profilerConnected ? "connected" : "waiting for the profiler to connect");
        }
    }
    ImGui::End();
    return changed;
}

ProjectSettingsRequests DrawProjectSettingsPanel(const PhysicsEngineStatus& status)
{
    ProjectSettingsRequests requests;
    if (ImGui::Begin(kProjectSettingsWindow))
    {
        ImGui::SeparatorText("Physics Engine");
        if (!status.selected)
        {
            ImGui::TextDisabled("None: no module declares a Physics Engine.");
        }
        else
        {
            ImGui::Text("%s %s", status.id.c_str(), status.version.c_str());
            ImGui::TextDisabled("%s", status.reason.c_str());
        }
        const char* current = status.pinned ? status.id.c_str() : "(automatic)";
        if (ImGui::BeginCombo("Pin", current))
        {
            if (ImGui::Selectable("(automatic)", !status.pinned))
            {
                requests.clearPin = status.pinned;
            }
            for (const std::string& id : status.pinnable)
            {
                if (ImGui::Selectable(id.c_str(), status.pinned && id == status.id) &&
                    !(status.pinned && id == status.id))
                {
                    requests.pin = id;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Only engines every module of the project supports are listed.");
        }
    }
    ImGui::End();
    return requests;
}

ProjectRequests DrawProjectMenu(ProjectMenuState& state, std::span<const std::string> recent,
                                std::span<const ProjectTemplate> templates, std::span<const std::string> addable)
{
    ProjectRequests requests;
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Project"))
        {
            state.showNew = ImGui::MenuItem("New project...") || state.showNew;
            state.showOpen = ImGui::MenuItem("Open project...") || state.showOpen;
            if (ImGui::BeginMenu("Recent", !recent.empty()))
            {
                for (const std::string& folder : recent)
                {
                    if (ImGui::MenuItem(folder.c_str()))
                    {
                        requests.open = folder;
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Add module", !addable.empty()))
            {
                for (const std::string& id : addable)
                {
                    if (ImGui::MenuItem(id.c_str()))
                    {
                        requests.addModule = id;
                    }
                }
                ImGui::EndMenu();
            }
            if (!state.refusal.empty())
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Refused: %s", state.refusal.c_str());
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    if (state.showNew)
    {
        ImGui::OpenPopup("New project");
        state.showNew = false;
    }
    if (state.showOpen)
    {
        ImGui::OpenPopup("Open project");
        state.showOpen = false;
    }
    if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Name", &state.newName);
        ImGui::InputText("Folder", &state.newFolder);
        const std::string current =
            state.templateIndex < 0 || static_cast<std::size_t>(state.templateIndex) >= templates.size()
                ? std::string("(empty project)")
                : std::format("{} ({})", templates[static_cast<std::size_t>(state.templateIndex)].name,
                              templates[static_cast<std::size_t>(state.templateIndex)].module);
        if (ImGui::BeginCombo("Template", current.c_str()))
        {
            if (ImGui::Selectable("(empty project)", state.templateIndex < 0))
            {
                state.templateIndex = -1;
            }
            for (std::size_t i = 0; i < templates.size(); ++i)
            {
                const std::string label = std::format("{} ({})", templates[i].name, templates[i].module);
                if (ImGui::Selectable(label.c_str(), std::cmp_equal(i, state.templateIndex)))
                {
                    state.templateIndex = static_cast<int>(i);
                }
                if (ImGui::IsItemHovered() && !templates[i].description.empty())
                {
                    ImGui::SetTooltip("%s", templates[i].description.c_str());
                }
            }
            ImGui::EndCombo();
        }
        ImGui::BeginDisabled(state.newFolder.empty() || state.newName.empty());
        if (ImGui::Button("Create"))
        {
            requests.createFolder = state.newFolder;
            requests.createName = state.newName;
            requests.createTemplate = state.templateIndex;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Open project", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::InputText("Folder", &state.openFolder);
        ImGui::BeginDisabled(state.openFolder.empty());
        if (ImGui::Button("Open"))
        {
            requests.open = state.openFolder;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return requests;
}

} // namespace interi::editor
