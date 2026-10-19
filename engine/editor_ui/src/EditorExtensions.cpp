// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/editor_ui/EditorExtensions.h>
#include <interi/editor_ui/ViewportOverlay.h>
#include <interi/scene/Mesh.h>

#include <imgui.h>

#include <algorithm>
#include <format>

namespace interi::editor
{

struct EditorExtensions::Taken
{
    std::vector<Entry<PanelDef>> panels;
    std::vector<Entry<OverlayDef>> overlays;
    std::vector<Entry<ToolDef>> tools;
    std::vector<Entry<RenderFeatureDef>> features;
    std::vector<Entry<UpdateDef>> updates;
    bool activeToolLost = false;
    std::string activeTool;
};

namespace
{

template <typename Entries>
void TakeProvider(Entries& entries, Entries& taken, std::string_view provider)
{
    Entries kept;
    for (auto& entry : entries)
    {
        (entry.provider == provider ? taken : kept).push_back(std::move(entry));
    }
    entries = std::move(kept);
}

} // namespace

void DocumentFacade::FocusCamera(const Vec3& target, float distance)
{
    if (m_host.camera != nullptr)
    {
        m_host.camera->target = target;
        m_host.camera->distance = distance;
    }
}

std::uint64_t DocumentFacade::AddMeshObject(std::string name, const Transform& transform, std::string meshAsset)
{
    return m_document->Add(
        [&](EditorScene& scene)
        {
            const EditorEntityId id = scene.CreateObject(name, transform);
            (void)scene.AddComponent(id, scene::MeshComponent::kComponentName);
            const auto entity = scene.Find(id);
            if (auto* mesh = entity ? scene.GetWorld().GetMut<scene::MeshComponent>(*entity) : nullptr)
            {
                mesh->asset.path = std::move(meshAsset);
            }
            return id;
        });
}

std::vector<std::uint64_t> DocumentFacade::SelectedIds() const
{
    return m_document->SelectedIds();
}

std::string DocumentFacade::ObjectName(std::uint64_t id) const
{
    return std::string(m_document->Scene().NameOf(id));
}

Transform DocumentFacade::ObjectTransform(std::uint64_t id) const
{
    return m_document->Scene().Find(id) ? m_document->Scene().WorldTransform(id) : Transform{};
}

bool ViewportCanvas::Project(const Vec3& world, float& x, float& y) const
{
    const auto point = m_camera->Project(world, m_viewport.width, m_viewport.height);
    if (!point)
    {
        return false;
    }
    x = m_viewport.x + point->x;
    y = m_viewport.y + point->y;
    return true;
}

Ray ViewportCanvas::ScreenRay(float x, float y) const
{
    return m_camera->ScreenRay({.x = x - m_viewport.x, .y = y - m_viewport.y}, m_viewport.width, m_viewport.height);
}

void ViewportCanvas::Line(float x0, float y0, float x1, float y1, std::uint32_t abgr, float thickness)
{
    m_list->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), abgr, thickness);
}

void ViewportCanvas::WorldLine(const Vec3& from, const Vec3& to, std::uint32_t abgr, float thickness)
{
    DrawWorldLine(*m_list, *m_camera, m_viewport, from, to, abgr, thickness);
}

void ViewportCanvas::Text(float x, float y, std::uint32_t abgr, const char* text)
{
    m_list->AddText(ImVec2(x, y), abgr, text);
}

PanelContext CurrentPanelContext()
{
    PanelContext context;
    context.imgui = ImGui::GetCurrentContext();
    ImGuiMemAllocFunc alloc = nullptr;
    ImGuiMemFreeFunc free = nullptr;
    void* user = nullptr;
    ImGui::GetAllocatorFunctions(&alloc, &free, &user);
    context.allocAlloc = reinterpret_cast<void*>(alloc);
    context.allocFree = reinterpret_cast<void*>(free);
    context.allocUser = user;
    return context;
}

Result<void> EditorExtensions::Check(std::string_view provider, const std::string& id, bool hasFunction)
{
    if (!hasFunction || id.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "an editor extension needs an id and a function");
    }
    const std::string prefix = std::string(provider) + ":";
    if (!id.starts_with(prefix) || id.size() == prefix.size())
    {
        return MakeError(ErrorCode::InvalidArgument,
                         std::format("editor extension '{}' must be named '{}<name>'", id, prefix));
    }
    return {};
}

bool EditorExtensions::IdTaken(const std::string& id) const
{
    const auto same = [&](const auto& entry) { return entry.def.id == id; };
    return std::ranges::any_of(m_panels, same) || std::ranges::any_of(m_overlays, same) ||
           std::ranges::any_of(m_tools, same) || std::ranges::any_of(m_features, same) ||
           std::ranges::any_of(m_updates, same);
}

Result<void> EditorExtensions::AddPanel(std::string_view provider, PanelDef def)
{
    INTERI_TRY(Check(provider, def.id, static_cast<bool>(def.draw)));
    if (IdTaken(def.id))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("editor extension '{}' already exists", def.id));
    }
    if (def.title.empty())
    {
        def.title = def.id;
    }
    m_panels.push_back({.provider = std::string(provider), .def = std::move(def), .visible = true});
    return {};
}

Result<void> EditorExtensions::AddOverlay(std::string_view provider, OverlayDef def)
{
    INTERI_TRY(Check(provider, def.id, static_cast<bool>(def.draw)));
    if (IdTaken(def.id))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("editor extension '{}' already exists", def.id));
    }
    m_overlays.push_back({.provider = std::string(provider), .def = std::move(def), .visible = true});
    return {};
}

Result<void> EditorExtensions::AddTool(std::string_view provider, ToolDef def)
{
    INTERI_TRY(Check(provider, def.id, static_cast<bool>(def.frame)));
    if (IdTaken(def.id))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("editor extension '{}' already exists", def.id));
    }
    if (def.name.empty())
    {
        def.name = def.id;
    }
    m_tools.push_back({.provider = std::string(provider), .def = std::move(def), .visible = true});
    return {};
}

Result<void> EditorExtensions::AddRenderFeature(std::string_view provider, RenderFeatureDef def)
{
    INTERI_TRY(Check(provider, def.id,
                     static_cast<bool>(def.contribute) || static_cast<bool>(def.render) ||
                         static_cast<bool>(def.replacesScene)));
    if (IdTaken(def.id))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("editor extension '{}' already exists", def.id));
    }
    m_features.push_back({.provider = std::string(provider), .def = std::move(def), .visible = true});
    return {};
}

Result<void> EditorExtensions::AddUpdate(std::string_view provider, UpdateDef def)
{
    INTERI_TRY(Check(provider, def.id, static_cast<bool>(def.update)));
    if (IdTaken(def.id))
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("editor extension '{}' already exists", def.id));
    }
    m_updates.push_back({.provider = std::string(provider), .def = std::move(def), .visible = true});
    return {};
}

std::shared_ptr<void> EditorExtensions::Extract(std::string_view provider)
{
    auto taken = std::make_shared<Taken>();
    // The active tool's own `deactivated` callback is module code too: call it before it goes.
    const auto active =
        std::ranges::find(m_tools, m_activeTool, [](const auto& e) -> const std::string& { return e.def.id; });
    if (active != m_tools.end() && active->provider == provider)
    {
        taken->activeToolLost = true;
        taken->activeTool = m_activeTool;
        if (active->def.deactivated)
        {
            active->def.deactivated();
        }
        m_activeTool.clear();
    }
    TakeProvider(m_panels, taken->panels, provider);
    TakeProvider(m_overlays, taken->overlays, provider);
    TakeProvider(m_tools, taken->tools, provider);
    TakeProvider(m_features, taken->features, provider);
    TakeProvider(m_updates, taken->updates, provider);
    return taken;
}

void EditorExtensions::Restore(std::shared_ptr<void> token)
{
    auto taken = std::static_pointer_cast<Taken>(token);
    for (auto& entry : taken->panels)
    {
        m_panels.push_back(std::move(entry));
    }
    for (auto& entry : taken->overlays)
    {
        m_overlays.push_back(std::move(entry));
    }
    for (auto& entry : taken->tools)
    {
        m_tools.push_back(std::move(entry));
    }
    for (auto& entry : taken->updates)
    {
        m_updates.push_back(std::move(entry));
    }
    for (auto& entry : taken->features)
    {
        m_features.push_back(std::move(entry));
    }
    if (taken->activeToolLost)
    {
        (void)ActivateTool(taken->activeTool);
    }
}

bool EditorExtensions::ActivateTool(std::string_view id)
{
    if (id == m_activeTool)
    {
        return true;
    }
    const auto find = [&](std::string_view wanted)
    {
        return std::ranges::find(m_tools, wanted, [](const auto& e) -> const std::string& { return e.def.id; });
    };
    const auto next = find(id);
    if (!id.empty() && next == m_tools.end())
    {
        return false;
    }
    if (const auto current = find(m_activeTool); current != m_tools.end() && current->def.deactivated)
    {
        current->def.deactivated();
    }
    m_activeTool = std::string(id);
    if (next != m_tools.end() && next->def.activated)
    {
        next->def.activated();
    }
    return true;
}

void EditorExtensions::SetPanelVisible(std::string_view id, bool visible)
{
    for (auto& panel : m_panels)
    {
        if (panel.def.id == id)
        {
            panel.visible = visible;
        }
    }
}

bool EditorExtensions::PanelVisible(std::string_view id) const
{
    return std::ranges::any_of(m_panels, [&](const auto& panel) { return panel.def.id == id && panel.visible; });
}

void EditorExtensions::DrawMenu()
{
    if (m_panels.empty() && m_tools.empty())
    {
        return;
    }
    if (!ImGui::BeginMenu("Extensions"))
    {
        return;
    }
    for (auto& panel : m_panels)
    {
        ImGui::MenuItem(panel.def.title.c_str(), nullptr, &panel.visible);
    }
    if (!m_tools.empty())
    {
        ImGui::Separator();
        if (ImGui::MenuItem("No extension tool", nullptr, m_activeTool.empty()))
        {
            (void)ActivateTool({});
        }
        // Copy the ids: activating can call into module code.
        std::vector<std::pair<std::string, std::string>> tools;
        tools.reserve(m_tools.size());
        for (const auto& tool : m_tools)
        {
            tools.emplace_back(tool.def.id, tool.def.name);
        }
        for (const auto& [id, name] : tools)
        {
            if (ImGui::MenuItem(name.c_str(), nullptr, m_activeTool == id))
            {
                (void)ActivateTool(id);
            }
        }
    }
    ImGui::EndMenu();
}

void EditorExtensions::DrawPanels(IEditorDocument& document)
{
    const PanelContext context = CurrentPanelContext();
    for (auto& panel : m_panels)
    {
        if (!panel.visible)
        {
            continue;
        }
        if (ImGui::Begin(panel.def.title.c_str(), &panel.visible))
        {
            panel.def.draw(context, document);
        }
        ImGui::End();
    }
}

void EditorExtensions::DrawOverlays(IViewportCanvas& canvas, IEditorDocument& document)
{
    for (auto& overlay : m_overlays)
    {
        overlay.def.draw(canvas, document);
    }
}

bool EditorExtensions::RunActiveTool(IViewportCanvas& canvas, const ToolInput& input, IEditorDocument& document)
{
    for (auto& tool : m_tools)
    {
        if (tool.def.id == m_activeTool)
        {
            tool.def.frame(canvas, input, document);
            return true;
        }
    }
    return false;
}

void EditorExtensions::CollectRenderFeatures(DebugDraw& draw, IEditorDocument& document) const
{
    for (const auto& feature : m_features)
    {
        if (feature.def.contribute)
        {
            feature.def.contribute(draw, document);
        }
    }
}

Result<void> EditorExtensions::GatherRenderObjects(RenderFeatureContext& context, RenderFeatureOutput& output,
                                                   IEditorDocument& document,
                                                   const std::function<Result<void>()>& resetPool) const
{
    const std::size_t objectsBefore = output.objects != nullptr ? output.objects->size() : 0;
    const std::size_t decalsBefore = output.decals != nullptr ? output.decals->size() : 0;
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        bool full = false;
        for (const auto& feature : m_features)
        {
            if (!feature.def.render)
            {
                continue;
            }
            const Result<void> done = feature.def.render(context, output, document);
            if (done)
            {
                continue;
            }
            if (done.error().code == ErrorCode::OutOfMemory && attempt == 0)
            {
                full = true;
                break;
            }
            INTERI_LOG_ERROR("Editor", "render feature '{}': {}", feature.def.id, done.error().message);
        }
        if (!full)
        {
            return {};
        }
        if (output.objects != nullptr)
        {
            output.objects->resize(objectsBefore);
        }
        if (output.decals != nullptr)
        {
            output.decals->resize(decalsBefore);
        }
        INTERI_TRY(resetPool());
        ++context.generation;
    }
    return {};
}

void EditorExtensions::RunUpdates(float seconds, IEditorDocument& document) const
{
    for (const auto& update : m_updates)
    {
        update.def.update(seconds, document);
    }
}

bool EditorExtensions::ScenePreviewActive(IEditorDocument& document) const
{
    return std::ranges::any_of(m_features, [&](const auto& feature)
                               { return feature.def.replacesScene && feature.def.replacesScene(document); });
}

void EditorExtensions::DrawDebug(const DebugDraw& draw, IViewportCanvas& canvas)
{
    for (const DebugDraw::Line& line : draw.Lines())
    {
        canvas.WorldLine(line.from, line.to, line.abgr, 1.0f);
    }
}

} // namespace interi::editor
