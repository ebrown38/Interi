// SPDX-License-Identifier: MIT
#include "EditorApp.h"

#include <interi/anim/AnimEvents.h>
#include <interi/chain/CoreLibrary.h>
#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/core/Profiling.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/Modeling.h>
#include <interi/editor/Prefabs.h>
#include <interi/editor_ui/AssetPanels.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/ViewportOverlay.h>
#include <interi/hotreload/ModuleDocs.h>
#include <interi/jobs/JobSystem.h>
#include <interi/nav/NavEvents.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Primitives.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/values/ProjectData.h>

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <iterator>
#include <variant>

namespace interi::editor
{

namespace
{

/// Whether a Tracy profiler is connected (profile builds only).
[[nodiscard]] bool ProfilerConnected()
{
#if INTERI_PROFILING_ACTIVE
    return TracyIsConnected;
#else
    return false;
#endif
}

} // namespace

namespace
{

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
    , m_jobs(jobs)
    , m_dataDir(desc.dataDir)
    , m_dataFiles(desc.dataDir)
    // Versioned: a layout saved before a window existed would leave that window floating.
    , m_iniPath((std::filesystem::temp_directory_path() / "interi_editor_layout_v10.ini").string())
    , m_previous(std::chrono::steady_clock::now())
{
    (void)m_files.Mount("", m_dataFiles);
    for (std::string& id : chain::CoreContracts())
    {
        m_chainModules.DeclareContract(std::move(id));
    }
    for (std::string& id : anim::AnimationContracts())
    {
        m_chainModules.DeclareContract(std::move(id));
    }
    for (std::string& id : nav::NavigationContracts())
    {
        m_chainModules.DeclareContract(std::move(id));
    }
    m_chains = std::make_unique<chain::ChainRegistry>(m_chainModules);
    m_moduleLibraries = desc.moduleLibraries;
    if (Logger* logger = GetGlobalLogger())
    {
        logger->AddSink(std::make_unique<LogBufferSink>(m_log));
    }
    m_preferencesPath = desc.preferencesPath.empty() ? DefaultPreferencesPath() : desc.preferencesPath;
    if (auto preferences = LoadPreferences(m_preferencesPath))
    {
        m_preferences = std::move(*preferences);
    }
    else
    {
        INTERI_LOG_WARN("Editor", "{}: {} (using defaults)", m_preferencesPath.string(), preferences.error().message);
    }
    m_trustPath = TrustStorePath(m_preferencesPath);
    if (auto trust = TrustStore::Load(m_trustPath))
    {
        m_trust = std::move(*trust);
    }
    else
    {
        INTERI_LOG_WARN("Editor", "{}: {} (no module is trusted)", m_trustPath.string(), trust.error().message);
    }
    m_tools.snap = m_preferences.snap;
    m_tools.moveSnap = m_preferences.moveSnap;
    m_tools.rotateSnapDegrees = m_preferences.rotateSnapDegrees;
    m_tools.sizeSnap = m_preferences.sizeSnap;
    m_keepPlayValues = m_preferences.keepPlayValues;
    m_autosave.SetInterval(std::chrono::seconds(m_preferences.autosaveSeconds));
    if (auto registered = chain::RegisterCoreLibrary(*m_chains); !registered)
    {
        INTERI_LOG_ERROR("Editor", "chain library: {}", registered.error().message);
    }
    if (auto registered = anim::RegisterAnimationLibrary(*m_chains); !registered)
    {
        INTERI_LOG_ERROR("Editor", "animation library: {}", registered.error().message);
    }
    if (auto registered = nav::RegisterNavigationLibrary(*m_chains, std::make_shared<nav::NavSource>()); !registered)
    {
        INTERI_LOG_ERROR("Editor", "navigation library: {}", registered.error().message);
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
    OpenAssetDatabase();
    OpenProject(desc);
}

void EditorApp::OpenProject(const EditorAppDesc& desc)
{
    if (desc.modulesDir.empty())
    {
        return;
    }
    std::vector<sdk::ModuleManifest> manifests;
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(desc.modulesDir, error))
    {
        if (!std::filesystem::exists(entry.path() / "module.json"))
        {
            continue;
        }
        auto manifest = sdk::LoadManifest(entry.path());
        if (!manifest)
        {
            INTERI_LOG_ERROR("Editor", "{}", manifest.error().message);
            return;
        }
        manifests.push_back(std::move(*manifest));
    }
    std::vector<std::string> order = desc.project;
    if (order.empty())
    {
        for (const sdk::ModuleManifest& manifest : manifests)
        {
            if (manifest.kind != sdk::ModuleKind::PhysicsEngine) // engines are reached through physicsEngines
            {
                order.push_back(manifest.id);
            }
        }
        std::ranges::sort(order);
    }
    auto graph = sdk::ResolveModuleGraph(order, manifests, sdk::CurrentHost());
    if (!graph)
    {
        INTERI_LOG_ERROR("Editor", "project: {}", graph.error().message);
        return;
    }
    auto project = ProjectData::Create(*graph);
    if (!project)
    {
        INTERI_LOG_ERROR("Editor", "project: {}", project.error().message);
        return;
    }
    m_project = std::move(*project);
    m_projectDataPath = desc.projectDataPath;
    if (!m_projectDataPath.empty() && std::filesystem::exists(m_projectDataPath))
    {
        std::ifstream file(m_projectDataPath, std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        auto loaded = values::LoadProjectData(text, m_project->Data());
        if (!loaded)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", m_projectDataPath.string(), loaded.error().message);
        }
        for (const std::string& warning : loaded.value_or(std::vector<std::string>{}))
        {
            INTERI_LOG_WARN("Editor", "{}: {}", m_projectDataPath.string(), warning);
        }
    }
    INTERI_LOG_INFO("Editor", "project of {} modules ({} values, {} archetypes, {} tables)", graph->modules.size(),
                    m_project->Data().Values().Ids().size(), m_project->Data().Characters().ArchetypeIds().size(),
                    m_project->Data().Tables().TableIds().size());
}

values::DataRegistry* EditorApp::ActiveData() noexcept
{
    if (m_modules != nullptr)
    {
        return &m_modules->Data();
    }
    return m_project != nullptr ? &m_project->Data() : nullptr;
}

chain::ChainRegistry& EditorApp::ActiveChains() noexcept
{
    return m_modules != nullptr ? m_modules->Chains() : *m_chains;
}

void EditorApp::LoadProjectModulesHost(const sdk::Project& project)
{
    ++m_chain.revision; // the chain registry is replaced: the chain editor's palette rebuilds
    m_moduleViewDirty = true;
    m_libraryWatcher = {};
    m_modules.reset(); // unloads the old modules (their extensions leave first)
    if (project.modules.empty() || !hotreload::ModuleHost::Supported())
    {
        return;
    }
    game::ProjectModulesDesc desc;
    desc.librarySearchPaths = m_moduleLibraries;
    desc.services[kEditorExtensionsService] = &m_extensions;
    auto modules = EditorModules::Open(project, desc);
    if (!modules)
    {
        INTERI_LOG_WARN("Editor", "modules not loaded ({}): play uses the core chain library", modules.error().message);
        return;
    }
    m_modules = std::move(*modules);
    INTERI_LOG_INFO("Editor", "loaded {} modules", m_modules->Host().LoadedModules().size());
    m_libraryWatcher = {};
    const std::vector<std::filesystem::path> libraries = FindModuleLibraries(m_projectManifests, m_moduleLibraries);
    m_libraryWatcher.Watch(libraries);
}

void EditorApp::SaveProjectData()
{
    if (ActiveData() == nullptr || m_projectDataPath.empty())
    {
        return;
    }
    std::ofstream file(m_projectDataPath, std::ios::binary | std::ios::trunc);
    file << values::WriteProjectData(*ActiveData());
    if (!file.flush())
    {
        INTERI_LOG_ERROR("Editor", "can't write {}", m_projectDataPath.string());
        return;
    }
    INTERI_LOG_INFO("Editor", "saved {}", m_projectDataPath.string());
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

AssetBoundsFn EditorApp::Bounds()
{
    return [this](std::string_view path) -> std::optional<AABB>
    {
        if (path.ends_with(".imesh"))
        {
            const std::string key = "mesh:" + std::string(path);
            auto found = m_meshCache.find(key);
            if (found == m_meshCache.end())
            {
                auto mesh = scene::LoadMeshAsset(m_files, path);
                found =
                    m_meshCache
                        .emplace(key, mesh ? std::make_shared<const scene::TriangleMesh>(std::move(*mesh)) : nullptr)
                        .first;
            }
            if (found->second == nullptr)
            {
                return std::nullopt;
            }
            const scene::MeshBounds bounds = scene::BoundsOf(*found->second);
            return AABB{.min = bounds.min, .max = bounds.max};
        }
        return std::nullopt; // modules draw (and bound) their own asset kinds
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
    Transform placement;
    placement.position = Snap(PlacementPoint(ray), m_tools.snap ? m_tools.moveSnap : 0.0f);
    if (path.ends_with(".prefab.json"))
    {
        if (auto instance = InstantiatePrefab(m_document, m_files, path, placement); !instance)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", path, instance.error().message);
        }
        return;
    }
    if (path.ends_with(".imodel.json"))
    {
        if (auto instance = InstantiateModel(m_document, m_files, path, placement); !instance)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", path, instance.error().message);
        }
        return;
    }
    if (path.ends_with(".imesh"))
    {
        (void)m_document.Add(
            [&](EditorScene& scene)
            {
                const EditorEntityId id = scene.CreateObject(std::filesystem::path(path).stem().string(), placement);
                (void)scene.AddComponent(id, scene::MeshComponent::kComponentName);
                const auto entity = scene.Find(id);
                if (auto* mesh = entity ? scene.GetWorld().GetMut<scene::MeshComponent>(*entity) : nullptr)
                {
                    mesh->asset.path = path;
                }
                return id;
            });
        return;
    }
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

    if (m_scenePlay != nullptr)
    {
        return; // the viewport shows the running scene: the camera moves, nothing is edited
    }
    {
        DocumentFacade document(m_document, FacadeHost());
        if (m_extensions.ScenePreviewActive(document))
        {
            return; // a module's preview replaces the scene: nothing in it can be picked
        }
    }
    const EditorEntityId selection = m_document.Selection();
    const auto entity = m_document.Scene().Find(selection);
    std::optional<Gizmo> gizmo;
    if (entity)
    {
        const Vec3 origin = m_document.Scene().WorldTransform(selection).position;
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
    if (gizmo && m_hoveredAxis != GizmoAxis::None)
    {
        m_drag = GizmoDrag::Begin(*gizmo, m_hoveredAxis, ray);
        if (m_drag)
        {
            m_dragTarget = selection;
            m_dragStart = m_document.Scene().WorldTransform(selection);
            m_dragKey = (m_dragKey + 1) | 0x8000000000000000ull;
            m_document.Undo().Seal();
            return;
        }
    }
    const auto hit = PickEntity(m_document.Scene(), ray, Bounds());
    if (ImGui::GetIO().KeyCtrl)
    {
        if (hit)
        {
            m_document.ToggleSelected(hit->id);
        }
    }
    else
    {
        m_document.Select(hit ? hit->id : 0);
    }
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
        Vec3 scale = m_dragStart.scale;
        const int index = axis == GizmoAxis::X ? 0 : (axis == GizmoAxis::Y ? 1 : 2);
        const float scaled =
            std::max(Snap(scale[index] * m_drag->Factor(ray), m_tools.snap ? m_tools.sizeSnap : 0.0f), 0.01f);
        transform.scale = {index == 0 ? scaled : scale.x, index == 1 ? scaled : scale.y, index == 2 ? scaled : scale.z};
        (void)EditTransform(m_document.Scene(), m_document.Undo(), m_dragTarget, transform, m_dragKey);
        break;
    }
    }
}

void EditorApp::UpdateServices(float seconds)
{
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(seconds));
    if (m_preferences.autosave && m_autosave.Tick(elapsed, m_document.IsDirty()))
    {
        if (auto path = m_document.Autosave(m_files); !path)
        {
            INTERI_LOG_WARN("Editor", "autosave: {}", path.error().message);
        }
        else
        {
            INTERI_LOG_INFO("Editor", "autosaved {}", *path);
        }
    }
    if (m_preferences.profilerFrameMarks)
    {
        INTERI_FRAME_MARK();
    }
    // Sources that changed on disk are imported again twice a second.
    m_assetPollSeconds += seconds;
    if (m_assetWatcher != nullptr && m_assetPollSeconds >= 0.5f)
    {
        m_assetPollSeconds = 0.0f;
        const auto outcome = m_assetWatcher->Update();
        if (outcome.changed)
        {
            for (const std::string& path : outcome.report.cooked)
            {
                INTERI_LOG_INFO("Editor", "imported {}", path);
            }
            for (const std::string& failure : outcome.report.failed)
            {
                INTERI_LOG_ERROR("Editor", "import {}", failure);
            }
            RefreshAssets();
        }
    }
}

void EditorApp::RefreshAssets()
{
    m_assetList = ListAssets(m_dataDir);
    m_thumbnails.Clear();
    m_meshCache.clear();
}

void EditorApp::OpenAssetDatabase()
{
    m_assetWatcher.reset();
    m_assetDb = std::make_unique<asset_import::AssetDatabase>(
        asset_import::AssetDatabase::Options{.root = m_dataDir, .importedFolder = "imported", .guids = {}});
    if (auto scanned = m_assetDb->Scan(); !scanned)
    {
        INTERI_LOG_ERROR("Editor", "assets: {}", scanned.error().message);
        m_assetDb.reset();
        return;
    }
    for (const std::string& warning : m_assetDb->ScanWarnings())
    {
        INTERI_LOG_WARN("Editor", "assets: {}", warning);
    }
    const asset_import::CookReport report = m_assetDb->CookAll();
    if (!report.cooked.empty())
    {
        INTERI_LOG_INFO("Editor", "imported {} asset(s)", report.cooked.size());
    }
    for (const std::string& warning : report.warnings)
    {
        INTERI_LOG_WARN("Editor", "import: {}", warning);
    }
    for (const std::string& failure : report.failed)
    {
        INTERI_LOG_ERROR("Editor", "import {}", failure);
    }
    m_assetWatcher = std::make_unique<asset_import::AssetWatcher>(*m_assetDb);
    (void)m_assetWatcher->Update(); // records the state
    RefreshAssets();
}

void EditorApp::OpenProjectFolder(const std::filesystem::path& folder)
{
    // A running scene holds the current project's Value registry: stop it (keeping nothing) first.
    m_scenePlay.reset();
    auto project = sdk::LoadProject(folder);
    if (!project)
    {
        INTERI_LOG_ERROR("Editor", "project: {}", project.error().message);
        return;
    }
    for (const std::string& warning : project->warnings)
    {
        INTERI_LOG_WARN("Editor", "{}: {}", folder.string(), warning);
    }
    auto manifests = LoadProjectModules(*project);
    if (!manifests)
    {
        INTERI_LOG_ERROR("Editor", "project modules: {}", manifests.error().message);
        return;
    }
    auto graph = sdk::ResolveModuleGraph(project->modules, *manifests, sdk::CurrentHost(), project->physicsEngine);
    if (!graph)
    {
        INTERI_LOG_ERROR("Editor", "project: {}", graph.error().message);
        return;
    }
    auto data = ProjectData::Create(*graph);
    if (!data)
    {
        INTERI_LOG_ERROR("Editor", "project data: {}", data.error().message);
        return;
    }
    m_project = std::move(*data);
    m_physicsEngine = DescribePhysicsEngine(*graph, *project, *manifests, sdk::CurrentHost());
    m_addableModules.clear();
    for (const sdk::ModuleManifest& manifest : *manifests)
    {
        if (manifest.kind != sdk::ModuleKind::PhysicsEngine && graph->Find(manifest.id) == sdk::ModuleGraph::npos)
        {
            m_addableModules.push_back(manifest.id);
        }
    }
    m_projectMenu.refusal.clear();
    m_projectDef = *project;
    m_projectManifests = *manifests;
    m_projectDataPath = project->data.empty() ? std::filesystem::path() : folder / project->data;
    if (!m_projectDataPath.empty() && std::filesystem::exists(m_projectDataPath))
    {
        std::ifstream file(m_projectDataPath, std::ios::binary);
        std::ostringstream text;
        text << file.rdbuf();
        if (auto loaded = values::LoadProjectData(text.str(), m_project->Data()); !loaded)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", m_projectDataPath.string(), loaded.error().message);
        }
    }
    m_templates = ListTemplates(*manifests);
    LoadProjectModulesHost(*project);
    if (m_projectFiles != nullptr)
    {
        (void)m_files.Unmount(*m_projectFiles); // before the old project's file system goes away
    }
    m_projectFiles = std::make_unique<NativeFileSystem>(folder);
    (void)m_files.Mount("", *m_projectFiles); // newest: the project's files shadow the data tree, writes go there
    m_dataDir = folder;
    m_assetList = ListAssets(m_dataDir);
    OpenAssetDatabase();
    m_meshCache.clear();
    AddRecentProject(m_preferences, folder.string());
    (void)SavePreferences(m_preferencesPath, m_preferences);
    if (!project->startupScene.empty())
    {
        if (auto opened = OpenLevel(project->startupScene); !opened)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", project->startupScene, opened.error().message);
        }
    }
    m_window.SetTitle(std::format("Interi Editor - {}", project->name));
    m_moduleViewDirty = true;
    INTERI_LOG_INFO("Editor", "opened project {} ({} modules)", project->name, graph->loadOrder.size());
}

void EditorApp::HandleProjectRequests(const ProjectRequests& requests)
{
    if (requests.createFolder)
    {
        const ProjectTemplate* from =
            requests.createTemplate >= 0 && static_cast<std::size_t>(requests.createTemplate) < m_templates.size()
                ? &m_templates[static_cast<std::size_t>(requests.createTemplate)]
                : nullptr;
        if (auto created = CreateProject(*requests.createFolder, requests.createName, from); !created)
        {
            INTERI_LOG_ERROR("Editor", "new project: {}", created.error().message);
        }
        else
        {
            OpenProjectFolder(*requests.createFolder);
        }
    }
    if (requests.open)
    {
        OpenProjectFolder(*requests.open);
    }
    if (requests.addModule && m_projectDef)
    {
        // The Module Manager checks the module graph and asks before native code is enabled.
        m_moduleManager.Queue({.kind = ModuleAction::Kind::Enable, .id = *requests.addModule, .position = 0});
    }
}

void EditorApp::HandleProjectSettings(const ProjectSettingsRequests& requests)
{
    if (!m_projectDef || (!requests.pin && !requests.clearPin))
    {
        return;
    }
    std::optional<sdk::PhysicsEngineRef> pin;
    if (requests.pin)
    {
        pin = sdk::PhysicsEngineRef{.id = *requests.pin, .version = {}};
    }
    ApplyProjectChange(PinProjectPhysicsEngine(*m_projectDef, m_projectManifests, pin, sdk::CurrentHost()),
                       requests.pin ? std::format("pin Physics Engine {}", *requests.pin)
                                    : "clear the Physics Engine pin");
}

void EditorApp::ApplyProjectChange(Result<sdk::Project> changed, const std::string& what)
{
    if (!changed)
    {
        // The project stays as it was; the reason goes to the console and the Project menu.
        INTERI_LOG_ERROR("Editor", "can't {}: {}", what, changed.error().message);
        m_projectMenu.refusal = changed.error().message;
        return;
    }
    if (auto saved = sdk::SaveProject(*changed); !saved)
    {
        INTERI_LOG_ERROR("Editor", "can't {}: {}", what, saved.error().message);
        m_projectMenu.refusal = saved.error().message;
        return;
    }
    // Changing the module set or the engine re-resolves the project (and stops the play first).
    OpenProjectFolder(changed->directory);
}

void EditorApp::HandlePlay(const ScenePlayRequests& requests, float seconds)
{
    values::DataRegistry* data = ActiveData();
    values::ValueRegistry* values = data != nullptr ? &data->Values() : nullptr;
    if (requests.play && m_scenePlay == nullptr)
    {
        auto session = ScenePlaySession::Start(m_document.Scene(), ActiveChains(), values, m_files, m_jobs, true);
        if (!session)
        {
            INTERI_LOG_ERROR("Editor", "play: {}", session.error().message);
            return;
        }
        m_scenePlay = std::move(*session);
        m_chain.debugger.Reset();
        m_chain.debugger.SetTarget(m_chain.path);
        m_scenePlay->SetStepObserver([this](const ScenePlaySession& play)
                                     { return m_chain.debugger.Observe(play.Runtime()).has_value(); });
        m_playUndoMark = m_document.Undo().Applied();
        for (const std::string& warning : m_scenePlay->Runtime().Warnings())
        {
            INTERI_LOG_WARN("Editor", "play: {}", warning);
        }
        return;
    }
    if (m_scenePlay == nullptr)
    {
        return;
    }
    if (requests.stop)
    {
        const std::size_t kept =
            EndPlay(*m_scenePlay, m_keepPlayValues, ActiveData(), m_document.Undo(), m_playUndoMark);
        if (kept > 0)
        {
            INTERI_LOG_INFO("Editor", "kept {} Value(s) chains set during play", kept);
        }
        m_scenePlay.reset();
        return;
    }
    if (requests.pause)
    {
        m_scenePlay->SetPaused(!m_scenePlay->Paused());
    }
    if (requests.step)
    {
        m_scenePlay->StepOnce();
    }
    m_scenePlay->Advance(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<float>(seconds)));
}

void EditorApp::HandleModeling(const ToolRequests& tools, bool groupShortcut)
{
    const AssetBoundsFn bounds = Bounds();
    if (tools.group || groupShortcut)
    {
        (void)GroupSelection(m_document, bounds);
    }
    if (tools.ungroup)
    {
        (void)UngroupSelection(m_document);
    }
    if (tools.duplicate)
    {
        (void)DuplicateSelection(m_document);
    }
    if (tools.array)
    {
        (void)DuplicateSelection(m_document, static_cast<std::uint32_t>(m_tools.arrayCount), m_tools.arrayOffset);
    }
    if (tools.align)
    {
        (void)AlignSelection(m_document, m_tools.axis, *tools.align, bounds);
    }
    if (tools.distribute)
    {
        (void)DistributeSelection(m_document, m_tools.axis, bounds);
    }
    if (tools.mirror)
    {
        (void)MirrorSelection(m_document, m_tools.axis, bounds);
    }
    if (tools.snapSelection)
    {
        (void)SnapSelection(m_document, m_tools.moveSnap);
    }
    if (tools.pivot)
    {
        for (const EditorEntityId id : m_document.SelectedRoots())
        {
            (void)SetPivot(m_document, id, *tools.pivot, bounds);
        }
    }
    if (tools.createPrefab)
    {
        const EditorEntityId root = m_document.Selection();
        std::string name(m_document.Scene().NameOf(root));
        std::ranges::replace_if(name, [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }, '_');
        const std::string path = std::format("prefabs/{}.prefab.json", name.empty() ? "prefab" : name);
        if (auto created = CreatePrefabFromSelection(m_document, m_files, path); !created)
        {
            INTERI_LOG_ERROR("Editor", "create prefab: {}", created.error().message);
        }
        else
        {
            INTERI_LOG_INFO("Editor", "wrote {}", path);
            m_assetList = ListAssets(m_dataDir);
        }
    }
    if (tools.revertPrefab || tools.applyPrefab)
    {
        const EditorEntityId root = InstanceRootOf(m_document.Scene(), m_document.Selection());
        auto done =
            tools.revertPrefab ? RevertInstance(m_document, m_files, root) : ApplyInstance(m_document, m_files, root);
        if (!done)
        {
            INTERI_LOG_ERROR("Editor", "prefab: {}", done.error().message);
        }
        m_meshCache.clear();
    }
    if (tools.csg)
    {
        // A fresh asset per operation: earlier results stay valid for undo.
        std::string path;
        for (std::uint32_t n = 1; path.empty() || m_files.Exists(path); ++n)
        {
            path = std::format("meshes/csg_{}.imesh", n);
        }
        if (auto created = CsgSelection(m_document, m_files, *tools.csg, path); !created)
        {
            INTERI_LOG_ERROR("Editor", "csg: {}", created.error().message);
        }
        else
        {
            INTERI_LOG_INFO("Editor", "csg: wrote {}", path);
            m_assetList = ListAssets(m_dataDir);
        }
    }
}

std::shared_ptr<const scene::TriangleMesh> EditorApp::ObjectMesh(const World& world, Entity entity)
{
    std::string key;
    Result<scene::TriangleMesh> mesh = MakeError(ErrorCode::NotFound, "no mesh");
    if (const auto* primitive = world.Get<scene::PrimitiveComponent>(entity))
    {
        key = std::format("primitive:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}", primitive->shape.name, primitive->size.x,
                          primitive->size.y, primitive->size.z, primitive->radius, primitive->height, primitive->tube,
                          primitive->segments, primitive->rings, primitive->steps);
        if (const auto found = m_meshCache.find(key); found != m_meshCache.end())
        {
            return found->second;
        }
        auto params = scene::ParamsOf(*primitive);
        mesh = params ? scene::GeneratePrimitive(*params) : std::unexpected(params.error());
    }
    else if (const auto* asset = world.Get<scene::MeshComponent>(entity))
    {
        if (!asset->asset.path.ends_with(".imesh"))
        {
            return nullptr; // another kind of asset: the module that owns it draws the object
        }
        key = "mesh:" + asset->asset.path;
        if (const auto found = m_meshCache.find(key); found != m_meshCache.end())
        {
            return found->second;
        }
        mesh = scene::LoadMeshAsset(m_files, asset->asset.path);
    }
    else
    {
        return nullptr;
    }
    if (!mesh)
    {
        INTERI_LOG_WARN("Editor", "{}: {}", key, mesh.error().message);
        m_meshCache.emplace(key, nullptr); // don't retry every frame
        return nullptr;
    }
    if (m_meshCache.size() > 256)
    {
        m_meshCache.clear(); // parameter drags make many variants; the renderer drops what it no longer sees
    }
    auto shared = std::make_shared<const scene::TriangleMesh>(std::move(*mesh));
    m_meshCache.emplace(key, shared);
    return shared;
}

void EditorApp::BuildScene()
{
    m_boxes.clear();
    m_meshes.clear();
    m_meshColors.clear();
    // The edited scene, or the running one while playing.
    World& world = m_scenePlay != nullptr ? m_scenePlay->Runtime().GetWorld() : m_document.Scene().GetWorld();
    const scene::HierarchyIndex objects = scene::BuildHierarchy(world);
    m_meshColors.reserve(objects.nodes.size()); // m_meshes keeps spans into it
    for (const auto& [id, node] : objects.nodes)
    {
        const Entity entity = node.entity;
        const auto* worldTransform = world.Get<Transform>(entity);
        if (worldTransform == nullptr || world.Has<chain::HiddenComponent>(entity))
        {
            continue;
        }
        const Transform transform = *worldTransform;
        const bool selected = m_scenePlay == nullptr && m_document.IsSelected(id);
        if (const auto mesh = ObjectMesh(world, entity))
        {
            if (m_renderer != nullptr)
            {
                if (auto added = m_renderer->AddMesh(mesh); !added)
                {
                    INTERI_LOG_WARN("Editor", "mesh of {}: {}", id, added.error().message);
                }
            }
            const auto* primitive = world.Get<scene::PrimitiveComponent>(entity);
            const auto* asset = world.Get<scene::MeshComponent>(entity);
            const values::ValueList& colors = primitive != nullptr ? primitive->colors : asset->colors;
            const Vec3 offset = primitive != nullptr ? primitive->offset : asset->offset;
            std::vector<Vec3>& slotColors = m_meshColors.emplace_back();
            for (std::uint32_t slot = 0; slot < std::max<std::uint32_t>(mesh->SlotCount(), 1); ++slot)
            {
                const values::Color color = scene::SlotColor(colors, slot);
                slotColors.emplace_back(color.r, color.g, color.b);
            }
            m_meshes.push_back({.mesh = mesh.get(),
                                .transform = transform.ToMat4() * Mat4::Translation(offset),
                                .slotColors = slotColors,
                                .color = selected ? std::optional(kSelectedColor) : std::nullopt,
                                .occluder = true});
        }
    }
}

DocumentHost EditorApp::FacadeHost()
{
    return {.dataDirectory = m_dataDir, .files = &m_files, .jobs = &m_jobs, .camera = &m_camera};
}

void EditorApp::DrawViewport()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(kViewportWindow))
    {
        ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + 6.0f, ImGui::GetCursorPosY() + 4.0f));
        m_playRequests = DrawPlayControls({.playing = m_scenePlay != nullptr,
                                           .paused = m_scenePlay != nullptr && m_scenePlay->Paused(),
                                           .step = m_scenePlay != nullptr ? m_scenePlay->StepIndex() : 0});
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
        {
            // An active extension tool owns the viewport's mouse; the editor's own picking and gizmo stand by.
            const ImVec2 mouse = ImGui::GetMousePos();
            const bool hovered = ImGui::IsItemHovered();
            const ToolInput input{.mouseX = mouse.x,
                                  .mouseY = mouse.y,
                                  .hovered = hovered,
                                  .pressed = hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left),
                                  .down = ImGui::IsMouseDown(ImGuiMouseButton_Left),
                                  .released = ImGui::IsMouseReleased(ImGuiMouseButton_Left)};
            ViewportCanvas canvas(*ImGui::GetWindowDrawList(), m_camera, viewport);
            DocumentFacade document(m_document, FacadeHost());
            if (!m_extensions.RunActiveTool(canvas, input, document))
            {
                HandleViewportInput(viewport);
            }
        }
        {
            ViewportCanvas canvas(*ImGui::GetWindowDrawList(), m_camera, viewport);
            DocumentFacade document(m_document, FacadeHost());
            m_extensions.DrawOverlays(canvas, document);
            DebugDraw debug;
            m_extensions.CollectRenderFeatures(debug, document);
            EditorExtensions::DrawDebug(debug, canvas);
        }
        DocumentFacade selectionDocument(m_document, FacadeHost());
        const EditorEntityId selection =
            (m_scenePlay != nullptr || m_extensions.ScenePreviewActive(selectionDocument)) ? 0 : m_document.Selection();
        if (const auto box = selection == 0 ? std::nullopt : PickBoxOf(m_document.Scene(), selection, Bounds()))
        {
            const Vec3 origin = m_document.Scene().WorldTransform(selection).position;
            const Gizmo gizmo{.mode = m_tools.mode, .origin = origin, .size = Gizmo::SizeFor(origin, m_camera.Eye())};
            DrawGizmo(*ImGui::GetWindowDrawList(), gizmo, m_camera, viewport, m_drag ? m_drag->Axis() : m_hoveredAxis);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

namespace
{

/// Reads Values from the project's registry for the HUD designer's preview.
class RegistryValues final : public chain::IValueReader
{
public:
    explicit RegistryValues(const values::ValueRegistry* registry) noexcept
        : m_registry(registry)
    {
    }
    [[nodiscard]] const values::Value* Read(std::string_view id) const override
    {
        return m_registry == nullptr ? nullptr : m_registry->Get(id);
    }

private:
    const values::ValueRegistry* m_registry;
};

} // namespace

void EditorApp::OpenChain(const std::string& path)
{
    m_chain.show = true;
    m_chain.path = path;
    if (auto loaded = chain::LoadChain(m_files, path))
    {
        m_chain.asset = std::move(*loaded);
        if (NeedsChainLayout(m_chain.asset))
        {
            AutoLayoutChain(m_chain.asset, {.registry = &ActiveChains(), .values = nullptr});
        }
        m_chain.undo.Clear();
        m_chain.ui = {};
        m_chain.message = std::format("loaded {} nodes", m_chain.asset.nodes.size());
        for (const std::string& warning : m_chain.asset.warnings)
        {
            INTERI_LOG_WARN("Editor", "{}: {}", path, warning);
        }
    }
    else
    {
        m_chain.message = loaded.error().message;
    }
}

void EditorApp::DrawChainEditorWindow()
{
    if (!m_chain.show)
    {
        return;
    }
    ImGui::SetNextWindowPos(ImVec2(60.0f, 710.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520.0f, 90.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Chain File", &m_chain.show))
    {
        ImGui::InputText("path", &m_chain.path);
        if (ImGui::Button("Load"))
        {
            OpenChain(m_chain.path);
        }
        ImGui::SameLine();
        if (ImGui::Button("New"))
        {
            m_chain.asset = {};
            m_chain.undo.Clear();
            m_chain.ui = {};
            m_chain.message = "new chain";
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            const std::string text = chain::WriteChain(m_chain.asset);
            const auto saved = m_files.WriteFile(m_chain.path, std::as_bytes(std::span(text)));
            m_chain.message = saved ? std::format("saved {}", m_chain.path) : saved.error().message;
            if (saved)
            {
                m_chain.undo.MarkClean();
            }
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(m_chain.undo.IsDirty() ? "*" : "");
        ImGui::TextUnformatted(m_chain.message.c_str());
    }
    ImGui::End();
    const values::DataRegistry* data = ActiveData();
    const RegistryValues values(data != nullptr ? &data->Values() : nullptr);
    ChainEditorHost host;
    host.context = {.registry = &ActiveChains(), .values = data != nullptr ? &data->Values() : nullptr};
    host.revision = m_chain.revision;
    host.debugger = &m_chain.debugger;
    host.playing = m_scenePlay != nullptr;
    host.paused = m_scenePlay != nullptr && m_scenePlay->Paused();
    host.playStep = m_scenePlay != nullptr ? m_scenePlay->StepIndex() : 0;
    host.values = m_scenePlay != nullptr
                      ? static_cast<const chain::IValueReader*>(&m_scenePlay->Runtime().Executor().Values())
                      : static_cast<const chain::IValueReader*>(&values);
    const ChainEditorResult result = DrawChainEditor(m_chain.asset, m_chain.undo, m_chain.ui, host);
    if (result.overrideEvent)
    {
        m_moduleManager.SetPluginTarget(PluginTargetKind::Event, *result.overrideEvent);
    }
    if (m_scenePlay != nullptr)
    {
        if (result.pauseRequested)
        {
            m_scenePlay->SetPaused(true);
        }
        if (result.resumeRequested)
        {
            m_scenePlay->SetPaused(false);
        }
        if (result.stepRequested)
        {
            m_scenePlay->StepOnce();
        }
    }
}

namespace
{

/// Runs `command` through the shell and returns its combined output; an error (with the output) for a non-zero exit.
Result<std::string> RunShell(const std::string& command)
{
#if defined(_WIN32)
    std::FILE* pipe = _popen((command + " 2>&1").c_str(), "r");
#else
    std::FILE* pipe = popen((command + " 2>&1").c_str(), "r");
#endif
    if (pipe == nullptr)
    {
        return MakeError(ErrorCode::IoError, "can't run: " + command);
    }
    std::string output;
    std::array<char, 4096> buffer{};
    constexpr std::size_t kMaxOutput = 256 * 1024;
    while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
    {
        if (output.size() < kMaxOutput)
        {
            output += buffer.data();
        }
    }
#if defined(_WIN32)
    const int status = _pclose(pipe);
#else
    const int status = pclose(pipe);
#endif
    if (status != 0)
    {
        return MakeError(ErrorCode::Unsupported, std::format("{}\nbuild failed (exit status {})", output, status));
    }
    return output;
}

} // namespace

void EditorApp::RefreshModuleManager()
{
    m_moduleViewDirty = false;
    m_moduleModel = {};
    m_moduleGraph = {};
    m_moduleConflicts = {};
    m_resolvedGraph = {};
    if (!m_projectDef)
    {
        return;
    }
    const sdk::HostInfo host = sdk::CurrentHost();
    m_moduleModel = BuildModuleManagerModel(*m_projectDef, m_projectManifests, host, m_trust);
    auto graph = sdk::ResolveModuleGraph(m_projectDef->modules, m_projectManifests, host, m_projectDef->physicsEngine);
    if (!graph)
    {
        return;
    }
    m_resolvedGraph = std::move(*graph);
    m_moduleGraph = BuildDependencyGraph(m_resolvedGraph);
    m_moduleConflicts = CollectConflicts(m_resolvedGraph, m_modules != nullptr ? &m_modules->Chains() : nullptr,
                                         ActiveData(), m_modules != nullptr ? &m_modules->Host().Hooks() : nullptr);
}

void EditorApp::RunModuleLoop(bool build)
{
    const bool wasPlaying = m_scenePlay != nullptr;
    DevLoopHooks hooks;
    hooks.stopPlay = [this]
    {
        if (m_scenePlay != nullptr)
        {
            (void)EndPlay(*m_scenePlay, false, ActiveData(), m_document.Undo(), m_playUndoMark);
            m_scenePlay.reset();
        }
    };
    if (build && !m_moduleManager.buildCommand.empty())
    {
        hooks.build = [this] { return RunShell(m_moduleManager.buildCommand); };
    }
    hooks.reload = [this]() -> Result<std::string>
    {
        if (m_modules == nullptr)
        {
            return MakeError(ErrorCode::NotFound, "no modules are loaded");
        }
        std::string text;
        for (const sdk::ModuleManifest& manifest : m_projectManifests)
        {
            const auto& loaded = m_modules->Host().LoadedModules();
            if (manifest.library.empty() || std::ranges::find(loaded, manifest.id) == loaded.end())
            {
                continue;
            }
            auto stats = m_modules->Reload(manifest.id);
            if (!stats)
            {
                return MakeError(stats.error().code, std::format("{}: {}", manifest.id, stats.error().message));
            }
            text += std::format("reloaded {}\n", manifest.id);
        }
        return text;
    };
    if (wasPlaying)
    {
        hooks.replay = [this]() -> Result<void>
        {
            HandlePlay({.play = true, .pause = false, .step = false, .stop = false}, 0.0f);
            return m_scenePlay != nullptr ? Result<void>{} : MakeError(ErrorCode::Unsupported, "play didn't start");
        };
    }
    const DevLoopReport report = RunDevLoop(hooks);
    m_moduleManager.loopOutput = report.success ? report.output : report.output + "\n" + report.message;
    m_moduleManager.loopFailed = !report.success;
    if (report.success)
    {
        INTERI_LOG_INFO("Editor", "modules reloaded");
    }
    else
    {
        INTERI_LOG_ERROR("Editor", "module loop failed: {}", report.message);
    }
    ++m_chain.revision; // events and functions came from new instances: the palette rebuilds
    m_moduleViewDirty = true;
}

void EditorApp::DrawModuleManager()
{
    if (m_moduleViewDirty)
    {
        RefreshModuleManager();
    }
    const sdk::HostInfo host = sdk::CurrentHost();
    ModuleManagerView view;
    view.model = &m_moduleModel;
    view.graph = &m_moduleGraph;
    view.conflicts = &m_moduleConflicts;
    view.canBuild = true;
    view.plan = [this, host](const ModuleAction& action) -> Result<ModuleChange>
    {
        if (!m_projectDef)
        {
            return MakeError(ErrorCode::NotFound, "no project is open");
        }
        switch (action.kind)
        {
        case ModuleAction::Kind::Enable:
            return EnableModule(*m_projectDef, m_projectManifests, action.id, host, m_trust, action.position);
        case ModuleAction::Kind::Disable:
            return DisableModule(*m_projectDef, m_projectManifests, action.id, host, m_trust);
        case ModuleAction::Kind::Move:
            return MoveModule(*m_projectDef, m_projectManifests, action.id, action.position, host, m_trust);
        }
        return MakeError(ErrorCode::InvalidArgument, "unknown action");
    };
    const ModuleManagerRequests requests = DrawModuleManagerPanel(m_moduleManager, view);
    if (!m_moduleManager.refusal.empty())
    {
        m_projectMenu.refusal = m_moduleManager.refusal;
    }
    m_moduleRequests = requests;
}

void EditorApp::HandleModuleManager()
{
    const ModuleManagerRequests requests = std::move(m_moduleRequests);
    m_moduleRequests = {};
    // A library rebuilt outside the editor: reload and play again.
    if (m_modules != nullptr && m_moduleManager.autoReload && m_libraryWatcher.Poll())
    {
        INTERI_LOG_INFO("Editor", "a module library changed: reloading");
        RunModuleLoop(false);
    }
    for (const std::string& id : requests.trust)
    {
        if (const auto manifest = std::ranges::find(m_projectManifests, id, &sdk::ModuleManifest::id);
            manifest != m_projectManifests.end())
        {
            m_trust.Trust(*manifest);
        }
    }
    if (!requests.trust.empty())
    {
        if (auto saved = m_trust.Save(m_trustPath); !saved)
        {
            INTERI_LOG_WARN("Editor", "{}: {}", m_trustPath.string(), saved.error().message);
        }
    }
    if (requests.apply)
    {
        ApplyProjectChange(requests.apply->project, "change the project's modules");
    }
    if (requests.createPlugin)
    {
        PluginStubDesc desc = requests.createPlugin->desc;
        std::filesystem::path folder = requests.createPlugin->folder;
        if (folder.is_relative() && m_projectDef)
        {
            folder = m_projectDef->directory / folder;
        }
        auto target = DescribePluginTarget(desc.target.kind, desc.target.id, m_resolvedGraph,
                                           m_modules != nullptr ? &m_modules->Chains() : nullptr,
                                           m_modules != nullptr ? &m_modules->Host().Hooks() : nullptr);
        if (!target)
        {
            m_moduleManager.pluginStatus = target.error().message;
        }
        else
        {
            desc.target = std::move(*target);
            auto written = WritePluginStub(folder, desc);
            m_moduleManager.pluginStatus =
                written ? std::format("Wrote {} files to {}. Build it against the engine (see the generated "
                                      "CMakeLists.txt), put it above the module it overrides and enable it.",
                                      written->size(), folder.string())
                        : written.error().message;
        }
    }
    if (requests.generateDocs || requests.saveDocs)
    {
        const std::string& id = requests.generateDocs ? *requests.generateDocs : *requests.saveDocs;
        const auto manifest = std::ranges::find(m_projectManifests, id, &sdk::ModuleManifest::id);
        if (manifest != m_projectManifests.end())
        {
            auto page = hotreload::GenerateModuleDocs(*manifest, m_modules != nullptr ? &m_modules->Chains() : nullptr,
                                                      m_modules != nullptr ? &m_modules->Host().Hooks() : nullptr);
            if (!page)
            {
                m_moduleManager.docsText = page.error().message;
            }
            else if (requests.generateDocs)
            {
                m_moduleManager.docsText = std::move(*page);
            }
            else if (m_projectDef)
            {
                const std::filesystem::path path = m_projectDef->directory / "docs" / "modules" / (id + ".md");
                std::error_code error;
                std::filesystem::create_directories(path.parent_path(), error);
                std::ofstream(path, std::ios::binary | std::ios::trunc) << *page;
                INTERI_LOG_INFO("Editor", "wrote {}", path.string());
            }
        }
    }
    if (requests.runLoop)
    {
        RunModuleLoop(true);
    }
}

void EditorApp::DrawHud()
{
    if (!m_hud.show)
    {
        return;
    }
    // Load/save bar above the designer window.
    if (ImGui::Begin("HUD File", &m_hud.show))
    {
        ImGui::InputText("path", &m_hud.path);
        if (ImGui::Button("Load"))
        {
            if (auto loaded = hud::LoadHud(m_files, m_hud.path))
            {
                m_hud.document = std::move(loaded->document);
                m_hud.undo.Clear();
                m_hud.ui = {};
                m_hud.message = std::format("loaded {} widgets", m_hud.document.widgets.size());
            }
            else
            {
                m_hud.message = loaded.error().message;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save"))
        {
            const std::string text = hud::WriteHud(m_hud.document);
            const auto saved = m_files.WriteFile(m_hud.path, std::as_bytes(std::span(text)));
            m_hud.message = saved ? std::format("saved {}", m_hud.path) : saved.error().message;
            if (saved)
            {
                m_hud.undo.MarkClean();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Undo") && m_hud.undo.CanUndo())
        {
            m_hud.undo.Undo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Redo") && m_hud.undo.CanRedo())
        {
            m_hud.undo.Redo();
        }
        ImGui::TextUnformatted(m_hud.message.c_str());
    }
    ImGui::End();
    const values::DataRegistry* data = ActiveData();
    const RegistryValues values(data != nullptr ? &data->Values() : nullptr);
    (void)DrawHudDesignerPanel(m_hud.document, m_hud.undo, m_hud.ui, values,
                               data != nullptr ? data->Values().Ids() : std::vector<std::string>{});
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
    // The Project menu first: File's right-aligned title would push later menus off the bar.
    const ProjectRequests projectRequests =
        DrawProjectMenu(m_projectMenu, m_preferences.recentProjects, m_templates, m_addableModules);
    MenuRequests requests = DrawMainMenu(m_document);
    (void)DrawDockSpace(m_buildLayout);
    m_buildLayout = false;
    HandleShortcuts(m_document, requests);
    DrawOutliner(m_document);
    DrawInspector(m_document);
    const ToolRequests tools = DrawToolsPanel(m_tools, m_document);
    const AssetRequests assets =
        DrawAssetsPanel(m_assetList, m_assetBrowser, {.files = &m_files, .thumbnails = &m_thumbnails});
    if (DrawMaterialEditorPanel(m_materialEditor, m_files).saved)
    {
        m_thumbnails.Invalidate(m_materialEditor.path);
    }
    if (DrawImportPanel(m_assetDb.get(), m_importPanel, m_assetBrowser.selected).reimported)
    {
        RefreshAssets();
    }
    if (values::DataRegistry* data = ActiveData())
    {
        DrawValuesPanel(*data, m_document.Undo(), m_dataPanels);
        DrawCharactersPanel(*data, m_document.Undo(), m_dataPanels);
        DrawDataTablesPanel(*data, m_document.Undo(), m_dataPanels);
    }
    if (ImGui::BeginMainMenuBar())
    {
        m_extensions.DrawMenu();
        ImGui::MenuItem("HUD Designer", nullptr, &m_hud.show);
        ImGui::MenuItem("Chain Editor", nullptr, &m_chain.show);
        ImGui::MenuItem("Module Manager", nullptr, &m_moduleManager.show);
        ImGui::EndMainMenuBar();
    }
    {
        DocumentFacade document(m_document, FacadeHost());
        m_extensions.DrawPanels(document);
    }
    DrawHud();
    DrawChainEditorWindow();
    DrawModuleManager();
    DrawConsolePanel(*m_log, m_console);
    const ProjectSettingsRequests settings = DrawProjectSettingsPanel(m_physicsEngine);
    if (DrawPreferencesPanel(m_preferences, INTERI_PROFILING_ACTIVE != 0, ProfilerConnected()))
    {
        m_tools.snap = m_preferences.snap;
        m_tools.moveSnap = m_preferences.moveSnap;
        m_tools.rotateSnapDegrees = m_preferences.rotateSnapDegrees;
        m_tools.sizeSnap = m_preferences.sizeSnap;
        m_keepPlayValues = m_preferences.keepPlayValues;
        m_autosave.SetInterval(std::chrono::seconds(m_preferences.autosaveSeconds));
        if (auto saved = SavePreferences(m_preferencesPath, m_preferences); !saved)
        {
            INTERI_LOG_WARN("Editor", "preferences: {}", saved.error().message);
        }
    }
    DrawViewport();
    HandleProjectRequests(projectRequests);
    HandleModuleManager();
    HandleProjectSettings(settings);
    ImGui::Render();

    HandlePlay(m_playRequests, io.DeltaTime);
    UpdateServices(io.DeltaTime);
    {
        DocumentFacade facade(m_document, FacadeHost());
        m_extensions.RunUpdates(io.DeltaTime, facade);
    }
    if (tools.addPrimitive)
    {
        Transform transform;
        transform.position = Snap(m_camera.target, m_tools.snap ? m_tools.moveSnap : 0.0f);
        scene::PrimitiveParams params;
        params.shape = *tools.addPrimitive;
        params.height = params.shape == scene::PrimitiveShape::Capsule ? 2.0f : 1.0f;
        (void)m_document.Add([&](EditorScene& scene) { return scene.CreatePrimitive(params, transform); });
    }
    HandleModeling(tools, requests.group);
    if (const EditorEntityId focused = m_document.Selection();
        focused != 0 && (tools.focusSelection || (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))))
    {
        m_camera.target = m_document.Scene().WorldTransform(focused).position;
    }
    if (assets.refresh)
    {
        RefreshAssets();
    }
    if (assets.openMaterial)
    {
        if (auto opened = OpenMaterialInEditor(m_materialEditor, m_files, *assets.openMaterial); !opened)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", *assets.openMaterial, opened.error().message);
        }
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
        if (auto saved = m_document.Save(m_files); !saved)
        {
            INTERI_LOG_ERROR("Editor", "save: {}", saved.error().message);
        }
        else
        {
            INTERI_LOG_INFO("Editor", "saved {}", m_document.Path());
            m_autosave.Reset();
        }
        SaveProjectData();
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
    DocumentFacade facade(m_document, FacadeHost());
    result.presented = m_renderer->Render({.boxes = m_boxes,
                                           .meshes = m_meshes,
                                           .view = view,
                                           .extensions = &m_extensions,
                                           .document = &facade,
                                           .replaceScene = m_extensions.ScenePreviewActive(facade),
                                           .ui = ImGui::GetDrawData()});
    return result;
}

} // namespace interi::editor
