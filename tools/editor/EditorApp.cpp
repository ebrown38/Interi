// SPDX-License-Identifier: MIT
#include "EditorApp.h"

#include <interi/core/Hash.h>
#include <interi/core/Log.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/destruction/BondGraph.h>
#include <interi/destruction/ChunkHierarchy.h>
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction_cook/Cooker.h>
#include <interi/editor/Inspector.h>
#include <interi/editor/Replay.h>
#include <interi/editor/StructureDebug.h>
#include <interi/editor_ui/EditorPanels.h>
#include <interi/editor_ui/ViewportOverlay.h>
#include <interi/jobs/JobSystem.h>
#include <interi/physics/Queries.h>
#include <interi/platform_sdl/SdlPlatform.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <iterator>
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
    , m_jobs(jobs)
    , m_dataDir(desc.dataDir)
    , m_dataFiles(desc.dataDir)
    // Versioned: a layout saved before a window existed would leave that window floating.
    , m_iniPath((std::filesystem::temp_directory_path() / "interi_editor_layout_v2.ini").string())
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
    m_structure.replayPath = (std::filesystem::temp_directory_path() / "interi_editor_session.irpl").string();
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
    (void)m_document.Add([&](EditorScene& scene) { return scene.CreateDestructible(transform, asset); });
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
    auto session = PlaySession::Start(m_document.Scene().ToLevel(), m_files, m_jobs);
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
    if (PreviewActive())
    {
        BuildPreviewScene();
        m_boxes.push_back({.transform = Mat4::FromTRS({0.0f, -0.05f, 0.0f}, Quat(), {60.0f, 0.1f, 60.0f}),
                           .color = {0.3f, 0.32f, 0.3f},
                           .occluder = true});
        return;
    }
    const World& world = m_document.Scene().GetWorld();
    const EditorEntityId selection = m_document.Selection();
    for (const EditorEntityId id : m_document.Scene().Ids())
    {
        const Entity entity = *m_document.Scene().Find(id);
        const Transform& transform = *world.Get<Transform>(entity);
        const bool selected = id == selection;
        if (m_play != nullptr && !world.Has<StaticBoxComponent>(entity))
        {
            continue; // playing: the simulation draws the destructibles; spawn points are not played
        }
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
        const EditorEntityId selection = (PreviewActive() || m_play != nullptr) ? 0 : m_document.Selection();
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
    const FractureRequests fracture = DrawFracturePanel(m_fracture, m_preview, m_assetList);
    const StructureRequests structure = DrawStructurePanel(m_structure, Status());
    DrawViewport();
    const bool editing = ImGui::IsAnyItemActive();
    ImGui::Render();

    HandleStructureRequests(structure);
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
    if (m_play != nullptr)
    {
        for (const auto& asset : m_play->Level().assets)
        {
            (void)m_renderer->AddAsset(asset);
        }
    }
    result.presented = m_renderer->Render({.boxes = m_boxes,
                                           .chunks = m_chunks,
                                           .world = m_play != nullptr ? &m_play->GetWorld() : nullptr,
                                           .destruction = m_play != nullptr ? &m_play->Destruction() : nullptr,
                                           .view = view,
                                           .ui = ImGui::GetDrawData()});
    return result;
}

} // namespace interi::editor
