// SPDX-License-Identifier: MIT
#pragma once

// What modules add to the editor: panels, viewport overlays, tools and render features. This header is all a module
// needs: it is abstract and header-only (no editor symbol is linked into a module library; everything crosses the
// boundary through virtual calls), and it includes neither ImGui nor editor headers.
//
//     auto* editor = interi::hotreload::ServiceAs<interi::editor::IEditorExtensions>(registrar, "editor.extensions");
//     if (editor != nullptr)   // null in a game: the module ships its editor parts in the same library
//         (void)editor->AddPanel(scope.Provider(), {.id = scope.Local("Stats"), .title = "Stats", .draw = ...});
//
// Ids are "<module id>:<name>" like every module item. The host takes a module's entries back before its library
// unloads (IModuleService), so callbacks never outlive their code; a hot-reloaded module simply registers again.
//
//   - Panels draw with ImGui. The module links its own copy of ImGui, so it must adopt the editor's context first:
//     `PanelContext::imgui` is the `ImGuiContext*` (call `ImGui::SetCurrentContext` with it, and
//     `ImGui::SetAllocatorFunctions` with the pair in `PanelContext::allocAlloc/allocFree/allocUser`).
//   - Overlays and tools draw through the viewport's `IViewportCanvas` (world points projected by the editor, lines
//     and text), needing no ImGui. A tool owns the viewport's mouse while it is the active one.
//   - A render feature contributes world-space debug geometry (`DebugDraw` lines and boxes) drawn each frame.
//   - The document is seen through `IEditorDocument`: its World (shared engine types), selection and names.

#include <interi/core/Result.h>
#include <interi/core/math/Geometry.h>
#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>
#include <interi/ecs/World.h>
#include <interi/hotreload/ModuleService.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::editor
{

inline constexpr const char* kEditorExtensionsService = "editor.extensions";

/// The scene being edited, as modules see it (an object is an entity with a stable id).
class IEditorDocument
{
public:
    IEditorDocument() = default;
    IEditorDocument(const IEditorDocument&) = delete;
    IEditorDocument& operator=(const IEditorDocument&) = delete;
    IEditorDocument(IEditorDocument&&) = delete;
    IEditorDocument& operator=(IEditorDocument&&) = delete;
    virtual ~IEditorDocument() = default;

    [[nodiscard]] virtual World& GetWorld() = 0;
    /// Editor ids of the selected objects (empty: none) and of the primary selection (0: none).
    [[nodiscard]] virtual std::vector<std::uint64_t> SelectedIds() const = 0;
    [[nodiscard]] virtual std::uint64_t Selection() const = 0;
    [[nodiscard]] virtual std::string ObjectName(std::uint64_t id) const = 0;
    /// World transform of an object (identity for an unknown id).
    [[nodiscard]] virtual Transform ObjectTransform(std::uint64_t id) const = 0;
};

struct ViewportRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

/// Drawing over the viewport, in window pixels.
class IViewportCanvas
{
public:
    IViewportCanvas() = default;
    IViewportCanvas(const IViewportCanvas&) = delete;
    IViewportCanvas& operator=(const IViewportCanvas&) = delete;
    IViewportCanvas(IViewportCanvas&&) = delete;
    IViewportCanvas& operator=(IViewportCanvas&&) = delete;
    virtual ~IViewportCanvas() = default;

    [[nodiscard]] virtual ViewportRect Rect() const = 0;
    /// Where a world point shows in window pixels; false when it is behind the eye.
    [[nodiscard]] virtual bool Project(const Vec3& world, float& x, float& y) const = 0;
    /// The ray from the eye through a window pixel.
    [[nodiscard]] virtual Ray ScreenRay(float x, float y) const = 0;
    virtual void Line(float x0, float y0, float x1, float y1, std::uint32_t abgr, float thickness) = 0;
    /// A world-space line, dropped when an end is behind the eye.
    virtual void WorldLine(const Vec3& from, const Vec3& to, std::uint32_t abgr, float thickness) = 0;
    virtual void Text(float x, float y, std::uint32_t abgr, const char* text) = 0;
};

/// Mouse state over the viewport for the active tool (window pixels).
struct ToolInput
{
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    bool hovered = false;
    bool pressed = false; ///< Left button went down this frame (over the viewport).
    bool down = false;
    bool released = false;
};

/// The ImGui context a panel draws in (see the top of this header). Opaque pointers: no ImGui include needed.
struct PanelContext
{
    void* imgui = nullptr;      ///< ImGuiContext*.
    void* allocAlloc = nullptr; ///< ImGuiMemAllocFunc.
    void* allocFree = nullptr;  ///< ImGuiMemFreeFunc.
    void* allocUser = nullptr;
};

/// World-space geometry for the viewport.
class DebugDraw
{
public:
    struct Line
    {
        Vec3 from;
        Vec3 to;
        std::uint32_t abgr;
    };
    void AddLine(const Vec3& from, const Vec3& to, std::uint32_t abgr) { m_lines.push_back({from, to, abgr}); }
    /// The twelve edges of an axis-aligned box.
    void AddBox(const Vec3& center, const Vec3& half, std::uint32_t abgr)
    {
        const auto corner = [&](int i)
        {
            return center + Vec3((i & 1) != 0 ? half.x : -half.x, (i & 2) != 0 ? half.y : -half.y,
                                 (i & 4) != 0 ? half.z : -half.z);
        };
        for (int i = 0; i < 8; ++i)
        {
            for (const int bit : {1, 2, 4})
            {
                if ((i & bit) == 0)
                {
                    AddLine(corner(i), corner(i | bit), abgr);
                }
            }
        }
    }
    [[nodiscard]] const std::vector<Line>& Lines() const noexcept { return m_lines; }
    void Clear() noexcept { m_lines.clear(); }

private:
    std::vector<Line> m_lines;
};

struct PanelDef
{
    std::string id;
    std::string title;
    /// Called between Begin and End of the panel's window, with the editor's ImGui context in `context`.
    std::function<void(const PanelContext& context, IEditorDocument& document)> draw;
};

struct OverlayDef
{
    std::string id;
    std::function<void(IViewportCanvas& canvas, IEditorDocument& document)> draw;
};

struct ToolDef
{
    std::string id;
    std::string name;
    std::function<void()> activated;   ///< Optional.
    std::function<void()> deactivated; ///< Optional.
    std::function<void(IViewportCanvas& canvas, const ToolInput& input, IEditorDocument& document)> frame;
};

struct RenderFeatureDef
{
    std::string id;
    std::function<void(DebugDraw& draw, IEditorDocument& document)> contribute;
};

/// The `editor.extensions` host service.
class IEditorExtensions : public hotreload::IModuleService
{
public:
    static constexpr std::string_view kServiceType = "interi.editor.IEditorExtensions";
    [[nodiscard]] std::string_view ServiceType() const noexcept override { return kServiceType; }

    /// InvalidArgument for an empty id or function, an id that isn't "<provider>:<name>", or an id already taken.
    virtual Result<void> AddPanel(std::string_view provider, PanelDef def) = 0;
    virtual Result<void> AddOverlay(std::string_view provider, OverlayDef def) = 0;
    virtual Result<void> AddTool(std::string_view provider, ToolDef def) = 0;
    virtual Result<void> AddRenderFeature(std::string_view provider, RenderFeatureDef def) = 0;
};

} // namespace interi::editor
