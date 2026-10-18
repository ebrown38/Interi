// SPDX-License-Identifier: MIT
#pragma once

// The editor's side of the extension API (EditorExtensionsApi.h): the registry modules add panels, overlays, tools
// and render features to, and the code that draws and runs them. Modules see only the abstract API; this class is
// linked into the editor application.

#include <interi/editor/EditorCamera.h>
#include <interi/editor/EditorDocument.h>
#include <interi/editor_ui/EditorExtensionsApi.h>
#include <interi/editor_ui/EditorPanels.h>

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct ImDrawList;

namespace interi::editor
{

/// An EditorDocument as modules see it.
class DocumentFacade final : public IEditorDocument
{
public:
    explicit DocumentFacade(EditorDocument& document) noexcept
        : m_document(&document)
    {
    }
    [[nodiscard]] World& GetWorld() override { return m_document->Scene().GetWorld(); }
    [[nodiscard]] std::vector<std::uint64_t> SelectedIds() const override;
    [[nodiscard]] std::uint64_t Selection() const override { return m_document->Selection(); }
    [[nodiscard]] std::string ObjectName(std::uint64_t id) const override;
    [[nodiscard]] Transform ObjectTransform(std::uint64_t id) const override;

private:
    EditorDocument* m_document;
};

/// The viewport image drawn over with ImGui's draw list, projected with the editor camera.
class ViewportCanvas final : public IViewportCanvas
{
public:
    ViewportCanvas(ImDrawList& list, const OrbitCamera& camera, const ScreenRect& viewport) noexcept
        : m_list(&list)
        , m_camera(&camera)
        , m_viewport(viewport)
    {
    }
    [[nodiscard]] ViewportRect Rect() const override
    {
        return {.x = m_viewport.x, .y = m_viewport.y, .width = m_viewport.width, .height = m_viewport.height};
    }
    [[nodiscard]] bool Project(const Vec3& world, float& x, float& y) const override;
    [[nodiscard]] Ray ScreenRay(float x, float y) const override;
    void Line(float x0, float y0, float x1, float y1, std::uint32_t abgr, float thickness) override;
    void WorldLine(const Vec3& from, const Vec3& to, std::uint32_t abgr, float thickness) override;
    void Text(float x, float y, std::uint32_t abgr, const char* text) override;

private:
    ImDrawList* m_list;
    const OrbitCamera* m_camera;
    ScreenRect m_viewport;
};

/// The ImGui context and allocator the panels draw with.
[[nodiscard]] PanelContext CurrentPanelContext();

class EditorExtensions final : public IEditorExtensions
{
public:
    Result<void> AddPanel(std::string_view provider, PanelDef def) override;
    Result<void> AddOverlay(std::string_view provider, OverlayDef def) override;
    Result<void> AddTool(std::string_view provider, ToolDef def) override;
    Result<void> AddRenderFeature(std::string_view provider, RenderFeatureDef def) override;

    [[nodiscard]] std::shared_ptr<void> Extract(std::string_view provider) override;
    void Restore(std::shared_ptr<void> token) override;

    [[nodiscard]] std::size_t PanelCount() const noexcept { return m_panels.size(); }
    [[nodiscard]] std::size_t OverlayCount() const noexcept { return m_overlays.size(); }
    [[nodiscard]] std::size_t ToolCount() const noexcept { return m_tools.size(); }
    [[nodiscard]] std::size_t RenderFeatureCount() const noexcept { return m_features.size(); }

    /// An "Extensions" menu in the main menu bar: panels to show or hide, tools to activate (none active: the
    /// editor's own selection tool). Call inside the menu bar.
    void DrawMenu();
    /// Each shown panel as a window.
    void DrawPanels(IEditorDocument& document);
    /// Every overlay over the viewport.
    void DrawOverlays(IViewportCanvas& canvas, IEditorDocument& document);
    /// The active tool's frame; true when a tool is active (the editor then leaves the viewport mouse to it).
    bool RunActiveTool(IViewportCanvas& canvas, const ToolInput& input, IEditorDocument& document);
    /// What the render features contribute now.
    void CollectRenderFeatures(DebugDraw& draw, IEditorDocument& document) const;
    /// Draws collected debug geometry over the viewport.
    static void DrawDebug(const DebugDraw& draw, IViewportCanvas& canvas);

    /// Activates the tool `id` (empty: none). False for an unknown id.
    bool ActivateTool(std::string_view id);
    [[nodiscard]] std::string_view ActiveTool() const noexcept { return m_activeTool; }
    void SetPanelVisible(std::string_view id, bool visible);
    [[nodiscard]] bool PanelVisible(std::string_view id) const;

private:
    template <typename Def>
    struct Entry
    {
        std::string provider;
        Def def;
        bool visible = true;
    };
    struct Taken;

    [[nodiscard]] static Result<void> Check(std::string_view provider, const std::string& id, bool hasFunction);
    [[nodiscard]] bool IdTaken(const std::string& id) const;

    std::vector<Entry<PanelDef>> m_panels;
    std::vector<Entry<OverlayDef>> m_overlays;
    std::vector<Entry<ToolDef>> m_tools;
    std::vector<Entry<RenderFeatureDef>> m_features;
    std::string m_activeTool;
};

} // namespace interi::editor
