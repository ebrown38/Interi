// SPDX-License-Identifier: MIT
#include <interi/editor/StructureDebug.h>
#include <interi/editor_ui/StructurePanel.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <format>

namespace interi::editor
{

StructureRequests DrawStructurePanel(StructureTool& tool, const PlayStatus& status)
{
    StructureRequests requests;
    if (!ImGui::Begin(kStructureWindow))
    {
        ImGui::End();
        return requests;
    }
    if (!status.playing)
    {
        requests.play = ImGui::Button("Play level");
    }
    else
    {
        requests.stop = ImGui::Button("Stop");
        ImGui::SameLine();
        ImGui::Checkbox("pause", &tool.paused);
        ImGui::SameLine();
        ImGui::BeginDisabled(!tool.paused);
        requests.step = ImGui::Button("step");
        ImGui::EndDisabled();
        const std::string line = status.watching ? std::format("replay step {} / {}, {} pieces, {} bonds", status.step,
                                                               status.replaySteps, status.pieces, status.intactBonds)
                                                 : std::format("step {}, {} pieces, {} bonds", status.step,
                                                               status.pieces, status.intactBonds);
        ImGui::TextUnformatted(line.c_str());
    }

    ImGui::SeparatorText("Overlays");
    ImGui::Checkbox("stress heatmap", &tool.stressOverlay);
    ImGui::SameLine();
    ImGui::Checkbox("anchors", &tool.anchors);
    // The colour scale.
    ImDrawList& list = *ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr int kSteps = 32;
    for (int i = 0; i < kSteps; ++i)
    {
        const Vec3 color = StressOverlayColor(static_cast<float>(i) / (kSteps - 1));
        const float x0 = origin.x + width * static_cast<float>(i) / kSteps;
        list.AddRectFilled(ImVec2(x0, origin.y), ImVec2(x0 + width / kSteps + 1.0f, origin.y + 8.0f),
                           ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f)));
    }
    ImGui::Dummy(ImVec2(width, 10.0f));
    ImGui::TextUnformatted("0% utilization              100%");

    ImGui::SeparatorText("Detonate here");
    ImGui::BeginDisabled(!status.playing);
    ImGui::Checkbox("left mouse detonates", &tool.detonate);
    ImGui::EndDisabled();
    ImGui::DragFloat("energy", &tool.energy, 50.0f, 10.0f, 1.0e6f, "%.0f J");
    ImGui::DragFloat("inner radius", &tool.innerRadius, 0.05f, 0.05f, 20.0f, "%.2f m");
    ImGui::DragFloat("radius", &tool.radius, 0.05f, 0.1f, 50.0f, "%.2f m");

    ImGui::SeparatorText("Replay");
    ImGui::InputText("file", &tool.replayPath);
    ImGui::BeginDisabled(!status.playing || status.watching);
    requests.saveReplay = ImGui::Button("Save session");
    ImGui::EndDisabled();
    ImGui::SameLine();
    requests.verifyReplay = ImGui::Button("Verify");
    ImGui::SameLine();
    requests.watchReplay = ImGui::Button("Watch");
    if (!status.message.empty())
    {
        ImGui::TextWrapped("%s", status.message.c_str());
    }
    ImGui::End();
    return requests;
}

} // namespace interi::editor
