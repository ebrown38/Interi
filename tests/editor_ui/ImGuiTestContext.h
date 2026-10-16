// SPDX-License-Identifier: MIT
#pragma once

#include <imgui.h>

namespace interi::editor::testing
{

/// A headless ImGui context: no renderer (texture requests are acknowledged without uploading), no ini file.
class ImGuiTestContext
{
public:
    ImGuiTestContext()
        : m_context(ImGui::CreateContext())
    {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.DeltaTime = 1.0f / 60.0f;
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    }
    ~ImGuiTestContext()
    {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        {
            texture->SetTexID(ImTextureID_Invalid);
            texture->SetStatus(ImTextureStatus_Destroyed);
        }
        ImGui::DestroyContext(m_context);
    }
    ImGuiTestContext(const ImGuiTestContext&) = delete;
    ImGuiTestContext& operator=(const ImGuiTestContext&) = delete;
    ImGuiTestContext(ImGuiTestContext&&) = delete;
    ImGuiTestContext& operator=(ImGuiTestContext&&) = delete;

    /// One frame: NewFrame, `draw`, Render. Returns the draw data.
    template <typename Draw>
    ImDrawData& Frame(Draw&& draw)
    {
        ImGui::NewFrame();
        draw();
        ImGui::Render();
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        {
            if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
            {
                texture->SetTexID(1);
                texture->SetStatus(ImTextureStatus_OK);
            }
            else if (texture->Status == ImTextureStatus_WantDestroy)
            {
                texture->SetTexID(ImTextureID_Invalid);
                texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
        return *ImGui::GetDrawData();
    }

private:
    ImGuiContext* m_context;
};

} // namespace interi::editor::testing
