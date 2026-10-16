// SPDX-License-Identifier: MIT
#include "EditorApp.h"

#include <interi/core/Log.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/editor/Inspector.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/ViewportOverlay.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform_sdl/SdlPlatform.h>

#include <imgui.h>

#include <algorithm>
#include <variant>

namespace interi::editor
{

namespace
{

/// The sandbox's generated wall (game/sandbox BuildWall): a 4 x 3 x 0.3 m concrete wall on y = 0, fractured into 4
/// regions of up to 10 chunks, so the sandbox level shows as it plays.
Result<std::vector<std::byte>> BuildSandboxWall(JobSystem& jobs)
{
    using namespace interi::destruction;
    HierarchySettings hierarchy;
    for (const auto& [count, seed] : {std::pair{4u, 11u}, std::pair{10u, 12u}})
    {
        SeedSettings level;
        level.count = count;
        level.randomSeed = seed;
        hierarchy.levels.push_back(level);
    }
    const ChunkHierarchy chunks =
        BuildHierarchy(MakeBoxMesh({-2.0f, 0.0f, -0.15f}, {2.0f, 3.0f, 0.15f}), hierarchy, jobs);
    std::vector<DestructionMaterial> materials(1);
    materials[0].name = "concrete";
    BondSettings bonds;
    bonds.groundHeight = 0.0f;
    BondGraph graph = BuildBondGraph(chunks.chunks, materials, bonds);
    INTERI_TRY_ASSIGN(asset, AssembleAsset(chunks.chunks, std::move(graph), materials, {}, jobs));
    return WriteDestructibleAsset(asset, 1);
}

/// Acknowledges ImGui's texture requests when nothing draws (no Vulkan).
void AcknowledgeTextures()
{
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
}

constexpr Vec3 kSelectedColor{1.0f, 0.62f, 0.15f};

} // namespace

EditorApp::EditorApp(SdlPlatform& platform, IWindow& window, JobSystem& jobs, const EditorAppDesc& desc)
    : m_platform(platform)
    , m_window(window)
    , m_dataDir(desc.dataDir)
    , m_dataFiles(desc.dataDir)
    , m_iniPath((std::filesystem::temp_directory_path() / "interi_editor_layout.ini").string())
    , m_previous(std::chrono::steady_clock::now())
{
    (void)m_files.Mount("", m_dataFiles);
    (void)m_files.Mount("generated", m_generatedFiles);
    if (auto wall = BuildSandboxWall(jobs))
    {
        (void)m_generatedFiles.WriteFile("wall.idest", *wall);
    }

    m_context = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = m_iniPath.c_str();
    m_buildLayout = !std::filesystem::exists(m_iniPath);
    ImGui::StyleColorsDark();

    if (auto renderer = EditorRenderer::Create(platform, window, jobs, desc.validation))
    {
        m_renderer = std::move(*renderer);
    }
    else
    {
        INTERI_LOG_WARN("Editor", "no renderer ({}): running the UI without drawing", renderer.error().message);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    }
    m_platform.SetTextInput(window, true);
    m_assetList = ListAssets(m_dataDir);
}

EditorApp::~EditorApp()
{
    m_renderer.reset(); // releases ImGui's textures while the context is alive
    ImGui::DestroyContext(m_context);
}

Result<void> EditorApp::OpenLevel(std::string_view path)
{
    INTERI_TRY(m_document.Open(m_files, path));
    m_window.SetTitle(std::string("Interi Editor - ") + std::string(path));
    return {};
}

std::shared_ptr<const destruction::DestructibleAsset> EditorApp::Asset(const std::string& path)
{
    if (const auto found = m_assets.find(path); found != m_assets.end())
    {
        return found->second;
    }
    if (m_missingAssets.contains(path))
    {
        return nullptr;
    }
    auto bytes = m_files.ReadFile(path);
    auto asset = bytes ? destruction::ReadDestructibleAsset(*bytes) : std::unexpected(bytes.error());
    if (!asset)
    {
        INTERI_LOG_WARN("Editor", "asset {}: {} (drawn as a placeholder)", path, asset.error().message);
        m_missingAssets.insert(path);
        return nullptr;
    }
    auto shared = std::make_shared<const destruction::DestructibleAsset>(std::move(*asset));
    if (m_renderer != nullptr)
    {
        if (auto added = m_renderer->AddAsset(shared); !added)
        {
            INTERI_LOG_WARN("Editor", "asset {}: {}", path, added.error().message);
        }
    }
    m_assets.emplace(path, shared);
    return shared;
}

AssetBoundsFn EditorApp::Bounds()
{
    return [this](std::string_view path) -> std::optional<AABB>
    {
        const auto asset = Asset(std::string(path));
        if (asset == nullptr)
        {
            return std::nullopt;
        }
        return ChunkBounds(*asset, 0);
    };
}

Vec3 EditorApp::PlacementPoint(const Ray& ray) const
{
    std::optional<float> distance = RayPlaneY(ray, 0.0f);
    if (const auto hit = PickEntity(m_document.Scene(), ray, {}); hit && (!distance || hit->distance < *distance))
    {
        distance = hit->distance;
    }
    return ray.origin + ray.direction * distance.value_or(m_camera.distance);
}

void EditorApp::DropAsset(const std::string& path, const Ray& ray)
{
    if (!path.ends_with(".idest"))
    {
        INTERI_LOG_WARN("Editor", "{}: only cooked .idest destructibles can be placed", path);
        return;
    }
    Transform transform;
    transform.position = Snap(PlacementPoint(ray), m_tools.snap ? m_tools.moveSnap : 0.0f);
    (void)m_document.Add([&](EditorScene& scene) { return scene.CreateDestructible(transform, path); });
}

void EditorApp::HandleViewportInput(const ScreenRect& viewport)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (!ImGui::IsMousePosValid())
    {
        return; // no mouse yet (or it left the window)
    }
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetMousePos();
    const Ray ray =
        m_camera.ScreenRay({.x = mouse.x - viewport.x, .y = mouse.y - viewport.y}, viewport.width, viewport.height);
    if (hovered)
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f))
        {
            m_camera.Orbit(io.MouseDelta.x, io.MouseDelta.y);
        }
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))
        {
            m_camera.Pan(io.MouseDelta.x, io.MouseDelta.y, viewport.height);
        }
        if (io.MouseWheel != 0.0f)
        {
            m_camera.Zoom(io.MouseWheel);
        }
        if (!io.WantTextInput)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            {
                m_tools.mode = GizmoMode::Translate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            {
                m_tools.mode = GizmoMode::Rotate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            {
                m_tools.mode = GizmoMode::Scale;
            }
        }
    }

    const EditorEntityId selection = m_document.Selection();
    const auto entity = m_document.Scene().Find(selection);
    std::optional<Gizmo> gizmo;
    if (entity)
    {
        const Vec3 origin = m_document.Scene().GetWorld().Get<Transform>(*entity)->position;
        gizmo = Gizmo{.mode = m_tools.mode, .origin = origin, .size = Gizmo::SizeFor(origin, m_camera.Eye())};
    }
    m_hoveredAxis = (hovered && gizmo && !m_drag) ? GizmoRayPick(*gizmo, ray) : GizmoAxis::None;

    if (m_drag)
    {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && m_document.Scene().Contains(m_dragTarget))
        {
            ApplyDrag(ray);
        }
        else
        {
            m_drag.reset();
            m_document.Undo().Seal();
        }
        return;
    }
    if (!hovered || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        return;
    }
    // Scaling only resizes static boxes: the other kinds have no size in the level file.
    const bool canDrag =
        gizmo && (m_tools.mode != GizmoMode::Scale || m_document.Scene().KindOf(selection) == EntityKind::StaticBox);
    if (canDrag && m_hoveredAxis != GizmoAxis::None)
    {
        m_drag = GizmoDrag::Begin(*gizmo, m_hoveredAxis, ray);
        if (m_drag)
        {
            const World& world = m_document.Scene().GetWorld();
            m_dragTarget = selection;
            m_dragStart = *world.Get<Transform>(*entity);
            const auto* box = world.Get<StaticBoxComponent>(*entity);
            m_dragStartHalfExtents = box != nullptr ? box->halfExtents : Vec3(1.0f);
            m_dragKey = (m_dragKey + 1) | 0x8000000000000000ull;
            m_document.Undo().Seal();
            return;
        }
    }
    const auto hit = PickEntity(m_document.Scene(), ray, Bounds());
    m_document.Select(hit ? hit->id : 0);
}

void EditorApp::ApplyDrag(const Ray& ray)
{
    const GizmoAxis axis = m_drag->Axis();
    const Vec3 direction = AxisVector(axis);
    Transform transform = m_dragStart;
    switch (m_drag->Mode())
    {
    case GizmoMode::Translate:
    {
        const float distance = Snap(m_drag->Distance(ray), m_tools.snap ? m_tools.moveSnap : 0.0f);
        transform.position = m_dragStart.position + direction * distance;
        (void)EditTransform(m_document.Scene(), m_document.Undo(), m_dragTarget, transform, m_dragKey);
        break;
    }
    case GizmoMode::Rotate:
    {
        constexpr float kDegrees = 180.0f / 3.14159265f;
        const float degrees = Snap(m_drag->Angle(ray) * kDegrees, m_tools.snap ? m_tools.rotateSnapDegrees : 0.0f);
        transform.rotation = Normalize(Quat::FromAxisAngle(direction, degrees / kDegrees) * m_dragStart.rotation);
        (void)EditTransform(m_document.Scene(), m_document.Undo(), m_dragTarget, transform, m_dragKey);
        break;
    }
    case GizmoMode::Scale:
    {
        Vec3 halfExtents = m_dragStartHalfExtents;
        const int index = axis == GizmoAxis::X ? 0 : (axis == GizmoAxis::Y ? 1 : 2);
        const float scaled =
            std::max(Snap(halfExtents[index] * m_drag->Factor(ray), m_tools.snap ? m_tools.sizeSnap : 0.0f), 0.01f);
        halfExtents = {index == 0 ? scaled : halfExtents.x, index == 1 ? scaled : halfExtents.y,
                       index == 2 ? scaled : halfExtents.z};
        (void)EditField(m_document.Scene(), m_document.Undo(), m_dragTarget, StaticBoxComponent::kComponentName,
                        "halfExtents", halfExtents, m_dragKey);
        break;
    }
    }
}

void EditorApp::BuildScene()
{
    m_boxes.clear();
    m_chunks.clear();
    const World& world = m_document.Scene().GetWorld();
    const EditorEntityId selection = m_document.Selection();
    for (const EditorEntityId id : m_document.Scene().Ids())
    {
        const Entity entity = *m_document.Scene().Find(id);
        const Transform& transform = *world.Get<Transform>(entity);
        const bool selected = id == selection;
        if (const auto* box = world.Get<StaticBoxComponent>(entity))
        {
            m_boxes.push_back(
                {.transform = Mat4::FromTRS(transform.position, transform.rotation, box->halfExtents * 2.0f),
                 .color = selected ? kSelectedColor : Vec3(0.5f, 0.52f, 0.48f),
                 .occluder = true});
        }
        else if (const auto* placement = world.Get<DestructiblePlacementComponent>(entity))
        {
            const auto asset = Asset(placement->asset);
            if (asset == nullptr)
            {
                m_boxes.push_back({.transform = Mat4::FromTRS(transform.position + Vec3(0.0f, 0.5f, 0.0f),
                                                              transform.rotation, Vec3(1.0f)),
                                   .color = {0.9f, 0.1f, 0.8f}});
                continue;
            }
            m_chunks.push_back({.asset = asset.get(),
                                .chunk = 0,
                                .transform = Mat4::FromTRS(transform.position, transform.rotation, Vec3(1.0f)),
                                .color = selected ? std::optional(kSelectedColor) : std::nullopt});
        }
        else if (world.Has<SpawnPointComponent>(entity))
        {
            m_boxes.push_back({.transform = Mat4::FromTRS(transform.position + Vec3(0.0f, 0.9f, 0.0f),
                                                          transform.rotation, {0.6f, 1.8f, 0.3f}),
                               .color = selected ? kSelectedColor : Vec3(0.2f, 0.45f, 0.9f)});
        }
    }
}

void EditorApp::DrawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(kViewportWindow))
    {
        const ImVec2 size = ImGui::GetContentRegionAvail();
        m_viewportWidth = std::max(size.x, 0.0f);
        m_viewportHeight = std::max(size.y, 0.0f);
        const std::uint64_t texture = m_renderer == nullptr
                                          ? 0
                                          : m_renderer->ViewportTexture(static_cast<std::uint32_t>(m_viewportWidth),
                                                                        static_cast<std::uint32_t>(m_viewportHeight));
        if (texture != 0)
        {
            ImGui::Image(ImTextureRef(texture), size);
        }
        else
        {
            ImGui::InvisibleButton("viewport", ImVec2(std::max(size.x, 1.0f), std::max(size.y, 1.0f)));
        }
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 extent = ImGui::GetItemRectSize();
        const ScreenRect viewport{.x = min.x, .y = min.y, .width = extent.x, .height = extent.y};
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(kAssetPayload))
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                DropAsset(static_cast<const char*>(payload->Data),
                          m_camera.ScreenRay({.x = mouse.x - viewport.x, .y = mouse.y - viewport.y}, viewport.width,
                                             viewport.height));
            }
            ImGui::EndDragDropTarget();
        }
        HandleViewportInput(viewport);
        const EditorEntityId selection = m_document.Selection();
        if (const auto box = selection == 0 ? std::nullopt : PickBoxOf(m_document.Scene(), selection, Bounds()))
        {
            const Vec3 origin =
                m_document.Scene().GetWorld().Get<Transform>(*m_document.Scene().Find(selection))->position;
            const Gizmo gizmo{.mode = m_tools.mode, .origin = origin, .size = Gizmo::SizeFor(origin, m_camera.Eye())};
            DrawGizmo(*ImGui::GetWindowDrawList(), gizmo, m_camera, viewport, m_drag ? m_drag->Axis() : m_hoveredAxis);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

FrameResult EditorApp::Frame()
{
    FrameResult result;
    std::vector<InputEvent> events;
    m_platform.PollEvents(events);
    ImGuiIO& io = ImGui::GetIO();
    for (const InputEvent& event : events)
    {
        if (std::holds_alternative<QuitEvent>(event))
        {
            result.quit = true;
        }
        m_input.Feed(io, event);
    }
    const WindowExtent size = m_window.Size();
    io.DisplaySize = ImVec2(static_cast<float>(size.width), static_cast<float>(size.height));
    const auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(std::chrono::duration<float>(now - m_previous).count(), 1e-4f, 0.25f);
    m_previous = now;

    ImGui::NewFrame();
    MenuRequests requests = DrawMainMenu(m_document);
    (void)DrawDockSpace(m_buildLayout);
    m_buildLayout = false;
    HandleShortcuts(m_document, requests);
    DrawOutliner(m_document);
    DrawInspector(m_document);
    const ToolRequests tools = DrawToolsPanel(m_tools, m_document);
    const AssetRequests assets = DrawAssetsPanel(m_assetList);
    DrawViewport();
    ImGui::Render();

    if (tools.addStaticBox || tools.addSpawnPoint)
    {
        Transform transform;
        transform.position = Snap(m_camera.target, m_tools.snap ? m_tools.moveSnap : 0.0f);
        if (tools.addStaticBox)
        {
            (void)m_document.Add([&](EditorScene& scene) { return scene.CreateStaticBox(transform, Vec3(1.0f)); });
        }
        else
        {
            transform.position.y = 0.0f;
            (void)m_document.Add([&](EditorScene& scene) { return scene.CreateSpawnPoint(transform, "spawn"); });
        }
    }
    if (const auto entity = m_document.Scene().Find(m_document.Selection());
        entity && (tools.focusSelection || (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))))
    {
        m_camera.target = m_document.Scene().GetWorld().Get<Transform>(*entity)->position;
    }
    if (assets.refresh)
    {
        m_assetList = ListAssets(m_dataDir);
    }
    if (assets.openLevel)
    {
        if (auto opened = OpenLevel(*assets.openLevel); !opened)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", *assets.openLevel, opened.error().message);
        }
    }

    if (requests.newLevel)
    {
        m_document.New();
    }
    if (requests.save)
    {
        if (auto saved = m_document.Save(m_dataFiles); !saved)
        {
            INTERI_LOG_ERROR("Editor", "save: {}", saved.error().message);
        }
        else
        {
            INTERI_LOG_INFO("Editor", "saved {}", m_document.Path());
        }
    }
    result.quit = result.quit || requests.quit;

    if (m_renderer == nullptr)
    {
        AcknowledgeTextures();
        return result;
    }
    BuildScene();
    render::SceneView view;
    view.cameraPosition = m_camera.Eye();
    view.view = m_camera.View();
    view.verticalFov = m_camera.verticalFov;
    view.nearPlane = m_camera.nearPlane;
    view.sun = {.direction = {-0.5f, -1.0f, -0.3f}, .color = {1.0f, 0.95f, 0.85f}, .intensity = 3.0f};
    view.ambient = {0.12f, 0.12f, 0.13f};
    result.presented = m_renderer->Render({.boxes = m_boxes,
                                           .chunks = m_chunks,
                                           .world = nullptr,
                                           .destruction = nullptr,
                                           .view = view,
                                           .ui = ImGui::GetDrawData()});
    return result;
}

} // namespace interi::editor
