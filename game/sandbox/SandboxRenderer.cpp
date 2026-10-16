// SPDX-License-Identifier: MIT
#include "SandboxRenderer.h"

#include <interi/core/Log.h>
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/destruction_render/ChunkMeshCache.h>
#include <interi/destruction_render/CrackDecals.h>
#include <interi/destruction_render/InteriorMaterials.h>
#include <interi/jobs/JobSystem.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/render/Camera.h>
#include <interi/render/Decals.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/MeshPool.h>
#include <interi/render/Particles.h>
#include <interi/rhi/CommandListPool.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>
#include <interi/rhi_vulkan/VulkanDevice.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace sandbox
{

using namespace interi;
using namespace interi::render;

namespace
{

constexpr std::uint32_t kFramesInFlight = 2;
constexpr std::uint32_t kMaxObjects = 16384;
constexpr float kVerticalFov = 1.0471976f; // 60 degrees
constexpr float kNearPlane = 0.1f;

/// Records uploads into a one-off command list, submits it and waits.
template <typename Record>
Result<void> UploadNow(rhi::IDevice& device, Record&& record)
{
    rhi::UploadQueue uploads(device, 4 * 1024 * 1024);
    INTERI_TRY_ASSIGN(pool, device.CreateCommandPool());
    rhi::ICommandList& commands = device.BeginCommandList(pool);
    Result<void> recorded = record(uploads, commands);
    if (recorded)
    {
        rhi::ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device.Submit(lists);
        uploads.Submitted(value);
        device.WaitForValue(value);
    }
    device.DestroyCommandPool(pool);
    return recorded;
}

} // namespace

/// Members are destroyed in reverse order: everything that holds GPU resources goes before the device.
struct SandboxRenderer::State
{
    IWindow* window = nullptr;
    std::unique_ptr<rhi::VulkanDevice> device;
    std::unique_ptr<rhi::ResourceManager> resources;
    std::unique_ptr<rhi::CommandListPool> commandPools;
    std::unique_ptr<MeshPool> meshes;
    std::unique_ptr<ForwardRenderer> renderer;
    std::unique_ptr<GpuParticleSystem> particles;
    std::unique_ptr<DecalRenderer> decalRenderer;
    std::unique_ptr<destruction::ChunkMeshCache> chunkMeshes;
    destruction::InteriorMaterials interior;
    destruction::ChunkMaterials chunkMaterials;
    destruction::ChunkGatherer gatherer;
    std::vector<Decal> decals;
    std::unique_ptr<rhi::ISwapchain> swapchain;
    std::unique_ptr<FrameGraph> graph;
    MeshRange cube;
    MeshRange ground;
    std::vector<RenderObject> objects;
};

SandboxRenderer::SandboxRenderer(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

SandboxRenderer::~SandboxRenderer()
{
    if (m_state != nullptr && m_state->resources != nullptr)
    {
        destruction::DestroyInteriorMaterials(*m_state->resources, m_state->interior);
    }
}

Result<std::unique_ptr<SandboxRenderer>> SandboxRenderer::Create(SdlPlatform& platform, IWindow& window,
                                                                 JobSystem& jobs, bool validation)
{
    auto state = std::make_unique<State>();
    state->window = &window;
    INTERI_TRY_ASSIGN(extensions, platform.VulkanInstanceExtensions());
    INTERI_TRY_ASSIGN(device, rhi::VulkanDevice::Create(rhi::VulkanDeviceDesc{
                                  .validation = validation, .instanceExtensions = extensions, .swapchain = true}));
    state->device = std::move(device);
    rhi::VulkanDevice& gpu = *state->device;
    INTERI_LOG_INFO("Sandbox", "rendering with {}", gpu.DeviceName());

    INTERI_TRY_ASSIGN(surface, platform.CreateVulkanSurface(window, gpu.NativeInstance()));
    const WindowExtent size = window.Size();
    INTERI_TRY_ASSIGN(swapchain, gpu.CreateSwapchain(surface, {.width = size.width, .height = size.height}));
    state->swapchain = std::move(swapchain);
    state->resources = std::make_unique<rhi::ResourceManager>(gpu);
    INTERI_TRY_ASSIGN(pools, rhi::CommandListPool::Create(gpu, jobs, kFramesInFlight));
    state->commandPools = std::make_unique<rhi::CommandListPool>(std::move(pools));
    // Destructible assets put every hierarchy level of every chunk in the pool.
    INTERI_TRY_ASSIGN(meshes, MeshPool::Create(gpu, *state->resources, 512 * 1024, 2 * 1024 * 1024));
    state->meshes = std::move(meshes);
    INTERI_TRY_ASSIGN(renderer, ForwardRenderer::Create(
                                    gpu, *state->resources, *state->meshes, state->swapchain->ImageFormat(),
                                    ForwardSettings{.framesInFlight = kFramesInFlight, .maxObjects = kMaxObjects}));
    state->renderer = std::move(renderer);
    INTERI_TRY_ASSIGN(particles, GpuParticleSystem::Create(gpu, *state->resources, state->swapchain->ImageFormat(),
                                                           ParticleSettings{.framesInFlight = kFramesInFlight}));
    state->particles = std::move(particles);
    INTERI_TRY_ASSIGN(decalRenderer, DecalRenderer::Create(gpu, *state->resources, state->swapchain->ImageFormat(),
                                                           4096, kFramesInFlight));
    state->decalRenderer = std::move(decalRenderer);
    state->chunkMeshes = std::make_unique<destruction::ChunkMeshCache>(*state->meshes);
    state->graph = std::make_unique<FrameGraph>(gpu, *state->resources);

    // Upload the meshes and interior textures once, before the first frame.
    State& uploaded = *state;
    INTERI_TRY(UploadNow(gpu,
                         [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands) -> Result<void>
                         {
                             INTERI_TRY_ASSIGN(cube, uploaded.meshes->Add(uploads, commands, MakeCube(0.25f)));
                             INTERI_TRY_ASSIGN(ground, uploaded.meshes->Add(uploads, commands, MakePlane(1.0f)));
                             uploaded.cube = cube;
                             uploaded.ground = ground;
                             INTERI_TRY_ASSIGN(
                                 interior, destruction::CreateInteriorMaterials(*uploaded.device, uploads, commands));
                             uploaded.interior = std::move(interior);
                             return {};
                         }));
    state->chunkMaterials.surface = {Material{.baseColor = {0.72f, 0.71f, 0.68f}, .roughness = 0.85f}};
    state->chunkMaterials.interior = state->interior.materials;
    state->chunkMaterials.fallback = state->chunkMaterials.surface[0];
    return std::unique_ptr<SandboxRenderer>(new SandboxRenderer(std::move(state)));
}

Result<void> SandboxRenderer::AddAsset(const std::shared_ptr<const destruction::DestructibleAsset>& asset)
{
    State& state = *m_state;
    return UploadNow(*state.device, [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands)
                     { return state.chunkMeshes->Add(uploads, commands, asset); });
}

Vec3 SandboxRenderer::CameraPosition(double seconds)
{
    const auto angle = static_cast<float>(seconds * 0.15);
    return {std::cos(angle) * 22.0f, 10.0f, std::sin(angle) * 22.0f};
}

bool SandboxRenderer::Render(const SandboxFrame& frame)
{
    const std::span<const Transform> debris = frame.cubes;
    State& state = *m_state;
    state.commandPools->BeginFrame();
    state.resources->Collect();

    const WindowExtent size = state.window->Size();
    state.swapchain->Resize(size.width, size.height);
    auto image = state.swapchain->AcquireNextImage();
    if (!image.has_value())
    {
        return false; // minimised: nothing to draw into
    }

    // Scene: a ground plane and four pillars (occluders), then as many debris cubes as the renderer takes.
    state.objects.clear();
    state.objects.push_back({.mesh = state.ground,
                             .transform = Mat4::Scale({40.0f, 1.0f, 40.0f}),
                             .material = {.baseColor = {0.45f, 0.47f, 0.42f}, .roughness = 0.9f},
                             .castsShadow = false,
                             .occluder = true});
    for (const Vec3 pillar : {Vec3{-6, 3, -6}, Vec3{6, 3, -6}, Vec3{-6, 3, 6}, Vec3{6, 3, 6}})
    {
        state.objects.push_back({.mesh = state.cube,
                                 .transform = Mat4::FromTRS(pillar, Quat::Identity(), {4.0f, 12.0f, 4.0f}),
                                 .material = {.baseColor = {0.7f, 0.68f, 0.62f}, .roughness = 0.7f},
                                 .occluder = true});
    }
    state.decals.assign(frame.decals.begin(), frame.decals.end());
    if (frame.world != nullptr && frame.destruction != nullptr)
    {
        (void)state.gatherer.Gather(*frame.world, *frame.destruction, *state.chunkMeshes, state.chunkMaterials,
                                    state.objects, state.renderer->MaxObjects());
        destruction::GatherCrackDecals(*frame.world, *frame.destruction, state.decals);
    }
    const std::size_t debrisCount = std::min(debris.size(), state.renderer->MaxObjects() - state.objects.size());
    for (std::size_t i = 0; i < debrisCount; ++i)
    {
        state.objects.push_back({.mesh = state.cube,
                                 .transform = Mat4::FromTRS(debris[i].position, debris[i].rotation, Vec3(1.0f)),
                                 .material = {.baseColor = {0.75f, 0.35f, 0.2f}, .roughness = 0.5f}});
    }

    SceneView view;
    view.cameraPosition = CameraPosition(frame.seconds);
    view.verticalFov = kVerticalFov;
    view.nearPlane = kNearPlane;
    view.view = LookAt(view.cameraPosition, {0.0f, 2.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    view.sun = {.direction = {-0.5f, -1.0f, -0.3f}, .color = {1.0f, 0.95f, 0.85f}, .intensity = 3.0f};
    const std::array lights{
        PointLight{.position = {0.0f, 1.5f, 0.0f}, .radius = 8.0f, .color = {1.0f, 0.6f, 0.2f}, .intensity = 20.0f},
        PointLight{.position = {-8.0f, 1.0f, 0.0f}, .radius = 6.0f, .color = {0.3f, 0.5f, 1.0f}, .intensity = 15.0f},
    };
    view.pointLights = lights;

    FrameGraph& graph = *state.graph;
    graph.Reset();
    FGTexture target =
        graph.ImportTexture("swapchain", *image, rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    const std::uint32_t width = state.swapchain->Width();
    const std::uint32_t height = state.swapchain->Height();
    const ForwardOutputs outputs =
        state.renderer->AddPasses(graph, target, width, height, view, state.objects, {0.45f, 0.6f, 0.8f});
    const Mat4 viewProjection =
        PerspectiveReverseZ(kVerticalFov, static_cast<float>(width) / static_cast<float>(height), kNearPlane) *
        view.view;
    state.decalRenderer->AddPasses(graph, state.decals, DecalView{.viewProjection = viewProjection}, target,
                                   outputs.depth, width, height);
    (void)state.particles->Emit(frame.particles);
    state.particles->AddPasses(
        graph, frame.dt, ParticleView{.viewProjection = viewProjection, .view = view.view, .nearPlane = kNearPlane},
        target, outputs.depth, width, height);
    if (auto compiled = graph.Compile(); !compiled)
    {
        INTERI_LOG_ERROR("Sandbox", "frame graph: {}", compiled.error().message);
        return false;
    }
    rhi::ICommandList& commands = state.commandPools->Acquire();
    bool ok = true;
    if (auto executed = graph.Execute(commands); !executed)
    {
        INTERI_LOG_ERROR("Sandbox", "frame graph: {}", executed.error().message);
        ok = false;
    }
    rhi::ICommandList* const lists[] = {&commands};
    const std::uint64_t value = state.device->Submit(lists, *state.swapchain);
    if (auto presented = state.swapchain->Present(); !presented)
    {
        INTERI_LOG_ERROR("Sandbox", "present: {}", presented.error().message);
        ok = false;
    }
    state.commandPools->EndFrame(value);
    return ok;
}

} // namespace sandbox
