// SPDX-License-Identifier: MIT
#include "EditorApp.h"

#include <interi/chain/CoreLibrary.h>
#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/core/Profiling.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction_cook/Cooker.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/LevelBridge.h>
#include <interi/editor/Modeling.h>
#include <interi/editor/Prefabs.h>
#include <interi/editor/Replay.h>
#include <interi/editor/StructureDebug.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/ViewportOverlay.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Queries.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/scene/Mesh.h>
#include <interi/scene/Primitives.h>
#include <interi/sdk/Manifest.h>
#include <interi/sdk/ModuleGraph.h>
#include <interi/values/ProjectData.h>

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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
    , m_jobs(jobs)
    , m_dataDir(desc.dataDir)
    , m_dataFiles(desc.dataDir)
    // Versioned: a layout saved before a window existed would leave that window floating.
    , m_iniPath((std::filesystem::temp_directory_path() / "interi_editor_layout_v5.ini").string())
    , m_previous(std::chrono::steady_clock::now())
{
    (void)m_files.Mount("", m_dataFiles);
    (void)m_files.Mount("generated", m_generatedFiles);
    for (std::string& id : chain::CoreContracts())
    {
        m_chainModules.DeclareContract(std::move(id));
    }
    m_chains = std::make_unique<chain::ChainRegistry>(m_chainModules);
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
    m_structure.replayPath = (std::filesystem::temp_directory_path() / "interi_editor_session.irpl").string();
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
            order.push_back(manifest.id);
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

void EditorApp::SaveProjectData()
{
    if (m_project == nullptr || m_projectDataPath.empty())
    {
        return;
    }
    std::ofstream file(m_projectDataPath, std::ios::binary | std::ios::trunc);
    file << values::WriteProjectData(m_project->Data());
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
    m_assets.emplace(path, shared);
    return shared;
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
    std::string asset = path;
    if (!path.ends_with(".idest"))
    {
        auto cooked = CookSource(path); // a glTF: place its cooked destructible
        if (!cooked)
        {
            INTERI_LOG_ERROR("Editor", "{}: {}", path, cooked.error().message);
            return;
        }
        asset = std::move(*cooked);
    }
    Transform transform;
    transform.position = Snap(PlacementPoint(ray), m_tools.snap ? m_tools.moveSnap : 0.0f);
    (void)m_document.Add([&](EditorScene& scene) { return CreateDestructible(scene, transform, asset); });
}

Result<std::string> EditorApp::CookSource(const std::string& path)
{
    destruction::CookRequest request;
    request.input = m_dataDir / path;
    INTERI_TRY_ASSIGN(report, destruction::Cook(request, m_jobs));
    const std::string asset = std::filesystem::relative(report.output, m_dataDir).generic_string();
    if (!report.skipped)
    {
        INTERI_LOG_INFO("Editor", "cooked {}: {} chunks, {} bonds", asset, report.chunkCount, report.bondCount);
        m_assets.erase(asset);
        m_missingAssets.erase(asset);
        m_assetList = ListAssets(m_dataDir);
    }
    return asset;
}

bool EditorApp::PreviewActive() const
{
    return m_play == nullptr && m_fracture.showPreview && !m_fracture.active.empty();
}

void EditorApp::StartPlay()
{
    auto session = PlaySession::Start(ExportLevel(m_document.Scene()), m_files, m_jobs);
    if (!session)
    {
        INTERI_LOG_ERROR("Editor", "play: {}", session.error().message);
        return;
    }
    m_play = std::move(*session);
    m_watching.reset();
    m_playTime = 0.0f;
}

void EditorApp::StepPlay(float seconds)
{
    if (m_play == nullptr)
    {
        return;
    }
    const auto step = [&]
    {
        if (m_watching)
        {
            if (m_play->StepIndex() >= m_watching->steps)
            {
                return false; // the replay is over: stay on its last state
            }
            while (m_nextReplayEvent < m_watching->events.size() &&
                   m_watching->events[m_nextReplayEvent].step == m_play->StepIndex())
            {
                m_play->Detonate(m_watching->events[m_nextReplayEvent++].damage);
            }
        }
        m_play->Step();
        if (m_watching && m_play->StepIndex() == m_watching->steps)
        {
            m_replayMessage = m_play->StateHash() == m_watching->finalHash
                                  ? "replay finished: the state hash matches the recording"
                                  : "replay finished: the state hash DIFFERS from the recording";
        }
        return true;
    };
    m_playTime = std::min(m_playTime + seconds, 4.0f * PlaySession::kStep);
    while (m_playTime >= PlaySession::kStep)
    {
        m_playTime -= PlaySession::kStep;
        if (!step())
        {
            break;
        }
    }
}

PlayStatus EditorApp::Status() const
{
    PlayStatus status;
    status.message = m_replayMessage;
    if (m_play == nullptr)
    {
        return status;
    }
    status.playing = true;
    status.watching = m_watching.has_value();
    status.step = m_play->StepIndex();
    status.replaySteps = m_watching ? m_watching->steps : 0;
    destruction::DestructionWorld& destruction = m_play->Destruction();
    for (const destruction::DestructibleId id : destruction.Instances())
    {
        status.pieces += destruction.PieceCount(id);
        for (std::uint32_t bond = 0; bond < destruction.Asset(id).bonds.size(); ++bond)
        {
            status.intactBonds += destruction.IsBondBroken(id, bond) ? 0 : 1;
        }
    }
    return status;
}

void EditorApp::HandleStructureRequests(const StructureRequests& requests)
{
    if (requests.play)
    {
        StartPlay();
    }
    if (requests.stop)
    {
        m_play.reset();
        m_watching.reset();
    }
    if (requests.step && m_play != nullptr)
    {
        m_playTime = PlaySession::kStep;
        const bool paused = m_structure.paused;
        m_structure.paused = false;
        StepPlay(0.0f);
        m_structure.paused = paused;
    }
    if (requests.saveReplay && m_play != nullptr)
    {
        const std::vector<std::byte> bytes = WriteReplay(m_play->Recording(m_document.Path()));
        std::ofstream file(m_structure.replayPath, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        m_replayMessage = file ? std::format("saved {} steps, {} explosions to {}", m_play->StepIndex(),
                                             m_play->Recording({}).events.size(), m_structure.replayPath)
                               : std::format("cannot write {}", m_structure.replayPath);
    }
    if (requests.verifyReplay || requests.watchReplay)
    {
        std::ifstream file(m_structure.replayPath, std::ios::binary);
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto replay = ReadReplay(std::as_bytes(std::span(bytes)));
        if (!replay)
        {
            m_replayMessage = std::format("{}: {}", m_structure.replayPath, replay.error().message);
            return;
        }
        if (requests.verifyReplay)
        {
            const auto hash = RunReplay(*replay, m_files, m_jobs);
            m_replayMessage = !hash ? std::format("replay failed: {}", hash.error().message)
                              : *hash == replay->finalHash
                                  ? std::format("verified: {} steps reproduce hash {:016x}", replay->steps, *hash)
                                  : std::format("MISMATCH: {:016x}, recorded {:016x}", *hash, replay->finalHash);
            return;
        }
        auto level = gameplay::ParseLevel(replay->levelJson);
        auto session = level ? PlaySession::Start(*level, m_files, m_jobs) : std::unexpected(level.error());
        if (!session)
        {
            m_replayMessage = std::format("replay failed: {}", session.error().message);
            return;
        }
        m_play = std::move(*session);
        m_watching = std::move(*replay);
        m_nextReplayEvent = 0;
        m_playTime = 0.0f;
        m_replayMessage.clear();
    }
}

void EditorApp::DemoStep(std::uint64_t frame, std::uint64_t frames)
{
    if (frame == 2)
    {
        m_camera.target = {0.0f, 1.5f, 0.0f};
        m_camera.distance = 16.0f;
        StartPlay();
    }
    if (frame == 10 && m_play != nullptr)
    {
        destruction::RadialDamage damage;
        damage.center = {0.0f, 1.2f, 0.2f};
        damage.energy = m_structure.energy;
        damage.innerRadius = m_structure.innerRadius;
        damage.radius = m_structure.radius;
        m_play->Detonate(damage);
    }
    if (frames > 12 && frame == frames - 3)
    {
        HandleStructureRequests({.saveReplay = true});
    }
    if (frames > 12 && frame == frames - 2)
    {
        HandleStructureRequests({.verifyReplay = true});
    }
}

void EditorApp::DrawStructureOverlay(const ScreenRect& viewport)
{
    if (m_play == nullptr)
    {
        return;
    }
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.PushClipRect(ImVec2(viewport.x, viewport.y), ImVec2(viewport.x + viewport.width, viewport.y + viewport.height),
                      true);
    const World& world = m_play->GetWorld();
    const destruction::DestructionWorld& destruction = m_play->Destruction();
    for (const destruction::DestructibleId id : destruction.Instances())
    {
        if (m_structure.stressOverlay)
        {
            for (const BondLine& line : StressOverlayLines(world, destruction, id))
            {
                const Vec3 color = StressOverlayColor(line.utilization);
                DrawWorldLine(list, m_camera, viewport, line.from, line.to,
                              ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, 1.0f)), 2.5f);
            }
        }
        if (m_structure.anchors)
        {
            for (const Vec3& anchor : AnchorPoints(world, destruction, id))
            {
                if (const auto at = m_camera.Project(anchor, viewport.width, viewport.height))
                {
                    const ImVec2 centre(viewport.x + at->x, viewport.y + at->y);
                    list.AddTriangleFilled(ImVec2(centre.x, centre.y - 6.0f), ImVec2(centre.x - 6.0f, centre.y + 5.0f),
                                           ImVec2(centre.x + 6.0f, centre.y + 5.0f), IM_COL32(60, 160, 255, 255));
                }
            }
        }
    }
    list.PopClipRect();
}

void EditorApp::OpenSource(const std::string& path)
{
    if (auto opened = m_preview.Open(path, m_dataDir / path, m_dataDir / "destruction" / "materials.json"); !opened)
    {
        INTERI_LOG_ERROR("Editor", "{}: {}", path, opened.error().message);
        return;
    }
    m_fracture.active = path;
    m_fracture.object.clear();
    m_fracture.showPreview = true;
    (void)m_preview.Update(m_jobs);
    if (const FracturePreview::Entry* entry = m_preview.Find(path); entry != nullptr && entry->asset != nullptr)
    {
        const AABB bounds = ChunkBounds(*entry->asset, 0);
        m_camera.target = (bounds.min + bounds.max) * 0.5f;
        m_camera.distance = std::max(Length(bounds.max - bounds.min) * 1.2f, 2.0f);
    }
}

void EditorApp::SaveAndCook()
{
    auto report = m_preview.SaveAndCook(m_fracture.active, m_jobs);
    if (!report)
    {
        INTERI_LOG_ERROR("Editor", "cook {}: {}", m_fracture.active, report.error().message);
        return;
    }
    const std::string asset = std::filesystem::relative(report->output, m_dataDir).generic_string();
    INTERI_LOG_INFO("Editor", "saved {} and cooked {} ({} chunks{})", SidecarPath(m_fracture.active).generic_string(),
                    asset, report->chunkCount, report->skipped ? ", already up to date" : "");
    m_assets.erase(asset); // levels using it pick up the new cook
    m_missingAssets.erase(asset);
    m_assetList = ListAssets(m_dataDir);
}

void EditorApp::PaintStroke(const Ray& ray)
{
    FracturePreview::Entry* entry = m_preview.Find(m_fracture.active);
    if (entry == nullptr)
    {
        return;
    }
    std::optional<float> distance;
    for (const destruction::SourceObject& object : entry->scene.objects)
    {
        if (!m_fracture.object.empty() && object.name != m_fracture.object)
        {
            continue;
        }
        if (const auto hit = RayMesh(ray, object.mesh); hit && (!distance || *hit < *distance))
        {
            distance = hit;
        }
    }
    if (!distance)
    {
        return;
    }
    const Vec3 point = ray.origin + ray.direction * *distance;
    if (m_lastStroke && Length(point - *m_lastStroke) < m_fracture.brushRadius * 0.5f)
    {
        return; // spread strokes along the drag
    }
    m_lastStroke = point;
    destruction::ObjectCookSettings settings = EditedSettings(*entry, m_fracture.object);
    if (settings.levels.empty())
    {
        settings.levels.emplace_back();
    }
    destruction::SeedSettings& level = settings.levels[std::min<std::size_t>(
        static_cast<std::size_t>(m_fracture.levelEdited), settings.levels.size() - 1)];
    level.pattern = destruction::SeedPattern::Painted; // keeps the level's count as the uniform base
    level.strokes.push_back({.centre = point, .radius = m_fracture.brushRadius, .density = m_fracture.brushDensity});
    StoreSettings(*entry, m_fracture.object, settings);
}

void EditorApp::BuildPreviewScene()
{
    const FracturePreview::Entry* entry = m_preview.Find(m_fracture.active);
    if (entry == nullptr || entry->asset == nullptr)
    {
        return;
    }
    const destruction::DestructibleAsset& asset = *entry->asset;
    if (m_renderer != nullptr)
    {
        (void)m_renderer->AddAsset(entry->asset);
    }
    const auto level = static_cast<std::uint32_t>(std::max(m_fracture.levelShown, 0));
    const Vec3 centre = asset.chunks.empty() ? Vec3() : asset.chunks[0].centroid;
    for (std::uint32_t c = 0; c < asset.chunks.size(); ++c)
    {
        const destruction::AssetChunk& chunk = asset.chunks[c];
        if (chunk.level != level && !(chunk.childCount == 0 && chunk.level < level))
        {
            continue;
        }
        // A colour per chunk (cell preview); anchors darker.
        const std::uint64_t hash = HashCombine(0x5EEDull, c);
        Vec3 color(0.35f + 0.6f * static_cast<float>(hash & 0xFF) / 255.0f,
                   0.35f + 0.6f * static_cast<float>((hash >> 8) & 0xFF) / 255.0f,
                   0.35f + 0.6f * static_cast<float>((hash >> 16) & 0xFF) / 255.0f);
        if (std::ranges::find(asset.anchors, c) != asset.anchors.end())
        {
            color = color * 0.35f;
        }
        const Vec3 offset = (chunk.centroid - centre) * m_fracture.explode;
        m_chunks.push_back({.asset = &asset, .chunk = c, .transform = Mat4::Translation(offset), .color = color});
    }
}

void EditorApp::DrawPreviewOverlay(const ScreenRect& viewport)
{
    const FracturePreview::Entry* entry = m_preview.Find(m_fracture.active);
    if (entry == nullptr)
    {
        return;
    }
    ImDrawList& list = *ImGui::GetWindowDrawList();
    list.PushClipRect(ImVec2(viewport.x, viewport.y), ImVec2(viewport.x + viewport.width, viewport.y + viewport.height),
                      true);
    const destruction::ObjectCookSettings settings = EditedSettings(*entry, m_fracture.object);
    const auto circle = [&](const Vec3& centre, float radius, ImU32 color)
    {
        constexpr int kSegments = 32;
        for (const auto& [u, v] : {std::pair{Vec3(1, 0, 0), Vec3(0, 1, 0)}, std::pair{Vec3(0, 1, 0), Vec3(0, 0, 1)},
                                   std::pair{Vec3(1, 0, 0), Vec3(0, 0, 1)}})
        {
            for (int i = 0; i < kSegments; ++i)
            {
                const float a0 = 6.2831853f * static_cast<float>(i) / kSegments;
                const float a1 = 6.2831853f * static_cast<float>(i + 1) / kSegments;
                DrawWorldLine(list, m_camera, viewport, centre + (u * std::cos(a0) + v * std::sin(a0)) * radius,
                              centre + (u * std::cos(a1) + v * std::sin(a1)) * radius, color, 1.5f);
            }
        }
    };
    for (const destruction::MaterialVolumeSettings& volume : settings.materialVolumes)
    {
        const ImU32 color = IM_COL32(80, 200, 255, 255);
        if (volume.volume.shape == destruction::MaterialVolume::Shape::Sphere)
        {
            circle(volume.volume.centre, volume.volume.radius, color);
            continue;
        }
        const Vec3 c = volume.volume.centre;
        const Vec3 h = volume.volume.halfExtents;
        for (int edge = 0; edge < 12; ++edge)
        {
            // The 12 edges of the box: 4 along each axis.
            const int axis = edge / 4;
            const float s0 = (edge & 1) != 0 ? 1.0f : -1.0f;
            const float s1 = (edge & 2) != 0 ? 1.0f : -1.0f;
            Vec3 from = c;
            Vec3 to = c;
            const Vec3 along = axis == 0 ? Vec3(h.x, 0, 0) : (axis == 1 ? Vec3(0, h.y, 0) : Vec3(0, 0, h.z));
            const Vec3 side0 = axis == 0 ? Vec3(0, h.y * s0, 0) : Vec3(h.x * s0, 0, 0);
            const Vec3 side1 = axis == 2 ? Vec3(0, h.y * s1, 0) : Vec3(0, 0, h.z * s1);
            from = from - along + side0 + side1;
            to = to + along + side0 + side1;
            DrawWorldLine(list, m_camera, viewport, from, to, color, 1.5f);
        }
    }
    for (const destruction::SeedSettings& level : settings.levels)
    {
        for (const destruction::SeedStroke& stroke : level.strokes)
        {
            circle(stroke.centre, stroke.radius, IM_COL32(255, 160, 40, 200));
        }
    }
    list.PopClipRect();
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

    if (m_play != nullptr)
    {
        // Play: the left mouse sets off an explosion where it points (when enabled); the level is not editable.
        if (m_structure.detonate && hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            if (const auto hit = physics::Raycast(m_play->Physics(), ray.origin, ray.direction * 500.0f))
            {
                destruction::RadialDamage damage;
                damage.center = hit->point;
                damage.energy = m_structure.energy;
                damage.innerRadius = m_structure.innerRadius;
                damage.radius = std::max(m_structure.radius, m_structure.innerRadius);
                m_play->Detonate(damage);
            }
        }
        return;
    }
    if (PreviewActive())
    {
        // Fracture preview: the left mouse paints seed density (when enabled); level entities are not editable.
        if (m_fracture.paint && ImGui::IsMouseDown(ImGuiMouseButton_Left) && (hovered || m_painting))
        {
            m_painting = true;
            PaintStroke(ray);
        }
        else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_painting = false;
            m_lastStroke.reset();
        }
        return;
    }
    if (m_scenePlay != nullptr)
    {
        return; // the viewport shows the running scene: the camera moves, nothing is edited
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
    auto graph = sdk::ResolveModuleGraph(project->modules, *manifests, sdk::CurrentHost());
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
    if (m_projectFiles != nullptr)
    {
        (void)m_files.Unmount(*m_projectFiles); // before the old project's file system goes away
    }
    m_projectFiles = std::make_unique<NativeFileSystem>(folder);
    (void)m_files.Mount("", *m_projectFiles); // newest: the project's files shadow the data tree, writes go there
    m_dataDir = folder;
    m_assetList = ListAssets(m_dataDir);
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
}

void EditorApp::HandlePlay(const ScenePlayRequests& requests, float seconds)
{
    values::ValueRegistry* values = m_project != nullptr ? &m_project->Data().Values() : nullptr;
    if (requests.play && m_scenePlay == nullptr)
    {
        auto session = ScenePlaySession::Start(m_document.Scene(), *m_chains, values, m_files, m_jobs);
        if (!session)
        {
            INTERI_LOG_ERROR("Editor", "play: {}", session.error().message);
            return;
        }
        m_scenePlay = std::move(*session);
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
            EndPlay(*m_scenePlay, m_keepPlayValues, m_project != nullptr ? &m_project->Data() : nullptr,
                    m_document.Undo(), m_playUndoMark);
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
    m_chunks.clear();
    m_meshes.clear();
    m_meshColors.clear();
    if (PreviewActive())
    {
        BuildPreviewScene();
        m_boxes.push_back({.transform = Mat4::FromTRS({0.0f, -0.05f, 0.0f}, Quat(), {60.0f, 0.1f, 60.0f}),
                           .color = {0.3f, 0.32f, 0.3f},
                           .occluder = true});
        return;
    }
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
        const bool isDestructible = world.Has<DestructiblePlacementComponent>(entity);
        if (m_play != nullptr && (isDestructible || world.Has<SpawnPointComponent>(entity)))
        {
            continue; // playing: the simulation draws the destructibles; spawn points are not played
        }
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
            if (m_renderer != nullptr)
            {
                if (auto added = m_renderer->AddAsset(asset); !added)
                {
                    INTERI_LOG_WARN("Editor", "asset {}: {}", placement->asset, added.error().message);
                }
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
        HandleViewportInput(viewport);
        if (PreviewActive())
        {
            DrawPreviewOverlay(viewport);
        }
        DrawStructureOverlay(viewport);
        const EditorEntityId selection =
            (PreviewActive() || m_play != nullptr || m_scenePlay != nullptr) ? 0 : m_document.Selection();
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
    const ProjectRequests projectRequests = DrawProjectMenu(m_projectMenu, m_preferences.recentProjects, m_templates);
    MenuRequests requests = DrawMainMenu(m_document);
    (void)DrawDockSpace(m_buildLayout);
    m_buildLayout = false;
    HandleShortcuts(m_document, requests);
    DrawOutliner(m_document);
    DrawInspector(m_document);
    const ToolRequests tools = DrawToolsPanel(m_tools, m_document);
    const AssetRequests assets = DrawAssetsPanel(m_assetList);
    const FractureRequests fracture = DrawFracturePanel(m_fracture, m_preview, m_assetList);
    const StructureRequests structure = DrawStructurePanel(m_structure, Status());
    if (m_project != nullptr)
    {
        DrawValuesPanel(m_project->Data(), m_document.Undo(), m_dataPanels);
        DrawCharactersPanel(m_project->Data(), m_document.Undo(), m_dataPanels);
        DrawDataTablesPanel(m_project->Data(), m_document.Undo(), m_dataPanels);
    }
    DrawConsolePanel(*m_log, m_console);
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
    const bool editing = ImGui::IsAnyItemActive();
    ImGui::Render();

    HandleStructureRequests(structure);
    HandlePlay(m_playRequests, io.DeltaTime);
    UpdateServices(io.DeltaTime);
    if (m_play != nullptr && !m_structure.paused)
    {
        StepPlay(io.DeltaTime);
    }
    if (fracture.open)
    {
        OpenSource(*fracture.open);
    }
    if (fracture.saveAndCook)
    {
        SaveAndCook();
    }
    // Recook once a widget or brush stroke is released, not on every step of a drag.
    if (!editing && !m_painting)
    {
        (void)m_preview.Update(m_jobs);
    }

    if (tools.addPrimitive || tools.addSpawnPoint)
    {
        Transform transform;
        transform.position = Snap(m_camera.target, m_tools.snap ? m_tools.moveSnap : 0.0f);
        if (tools.addPrimitive)
        {
            scene::PrimitiveParams params;
            params.shape = *tools.addPrimitive;
            params.height = params.shape == scene::PrimitiveShape::Capsule ? 2.0f : 1.0f;
            (void)m_document.Add([&](EditorScene& scene) { return scene.CreatePrimitive(params, transform); });
        }
        else
        {
            transform.position.y = 0.0f;
            (void)m_document.Add([&](EditorScene& scene) { return CreateSpawnPoint(scene, transform, "spawn"); });
        }
    }
    HandleModeling(tools, requests.group);
    if (const EditorEntityId focused = m_document.Selection();
        focused != 0 && (tools.focusSelection || (!io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))))
    {
        m_camera.target = m_document.Scene().WorldTransform(focused).position;
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
    if (requests.exportLevel)
    {
        if (auto exported = m_document.ExportLevel(m_files); !exported)
        {
            INTERI_LOG_ERROR("Editor", "export: {}", exported.error().message);
        }
        else
        {
            INTERI_LOG_INFO("Editor", "exported {}", m_document.ExportPath());
        }
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
    if (m_play != nullptr)
    {
        for (const auto& asset : m_play->Level().assets)
        {
            (void)m_renderer->AddAsset(asset);
        }
    }
    result.presented = m_renderer->Render({.boxes = m_boxes,
                                           .chunks = m_chunks,
                                           .meshes = m_meshes,
                                           .world = m_play != nullptr ? &m_play->GetWorld() : nullptr,
                                           .destruction = m_play != nullptr ? &m_play->Destruction() : nullptr,
                                           .view = view,
                                           .ui = ImGui::GetDrawData()});
    return result;
}

} // namespace interi::editor
