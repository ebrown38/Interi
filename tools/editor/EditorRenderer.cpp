// SPDX-License-Identifier: MIT
#include "EditorRenderer.h"

#include <interi/core/Log.h>
#include <interi/core/math/Mat4.h>
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/destruction_render/ChunkMeshCache.h>
#include <interi/destruction_render/CrackDecals.h>
#include <interi/destruction_render/InteriorMaterials.h>
#include <interi/editor_ui/ImGuiRenderer.h>
#include <interi/platform_sdl/SdlPlatform.h>
#include <interi/render/Camera.h>
#include <interi/render/Decals.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/MeshPool.h>
#include <interi/rhi/CommandListPool.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>
#include <interi/rhi_vulkan/VulkanDevice.h>

#include <algorithm>
#include <array>
#include <vector>

namespace interi::editor
{

using namespace interi::render;

namespace
{

constexpr std::uint32_t kFramesInFlight = 2;
constexpr std::uint32_t kMaxObjects = 16384;

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
struct EditorRenderer::State
{
    IWindow* window = nullptr;
    std::unique_ptr<rhi::VulkanDevice> device;
    std::unique_ptr<rhi::ResourceManager> resources;
    std::unique_ptr<rhi::CommandListPool> commandPools;
    std::unique_ptr<MeshPool> meshes;
    std::unique_ptr<ForwardRenderer> renderer;
    std::unique_ptr<DecalRenderer> decalRenderer;
    std::unique_ptr<ImGuiRenderer> ui;
    std::unique_ptr<destruction::ChunkMeshCache> chunkMeshes;
    destruction::InteriorMaterials interior;
    destruction::ChunkMaterials chunkMaterials;
    destruction::ChunkGatherer gatherer;
    std::vector<Decal> decals;
    std::unique_ptr<rhi::ISwapchain> swapchain;
    std::unique_ptr<FrameGraph> graph;
    MeshRange cube;
    std::vector<RenderObject> objects;
    rhi::TextureHandle viewport;
    std::uint32_t viewportWidth = 0;
    std::uint32_t viewportHeight = 0;
    bool viewportWritten = false; ///< Holds a frame (ShaderRead) rather than undefined contents.
};

EditorRenderer::EditorRenderer(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

EditorRenderer::~EditorRenderer()
{
    if (m_state != nullptr && m_state->resources != nullptr)
    {
        m_state->device->WaitForValue(m_state->device->LastSubmittedValue());
        if (m_state->viewport.IsValid())
        {
            m_state->resources->DestroyTexture(m_state->viewport);
        }
        destruction::DestroyInteriorMaterials(*m_state->resources, m_state->interior);
    }
}

Result<std::unique_ptr<EditorRenderer>> EditorRenderer::Create(SdlPlatform& platform, IWindow& window, JobSystem& jobs,
                                                               bool validation)
{
    auto state = std::make_unique<State>();
    state->window = &window;
    INTERI_TRY_ASSIGN(extensions, platform.VulkanInstanceExtensions());
    INTERI_TRY_ASSIGN(device, rhi::VulkanDevice::Create(rhi::VulkanDeviceDesc{
                                  .validation = validation, .instanceExtensions = extensions, .swapchain = true}));
    state->device = std::move(device);
    rhi::VulkanDevice& gpu = *state->device;
    INTERI_LOG_INFO("Editor", "rendering with {}", gpu.DeviceName());

    INTERI_TRY_ASSIGN(surface, platform.CreateVulkanSurface(window, gpu.NativeInstance()));
    const WindowExtent size = window.Size();
    INTERI_TRY_ASSIGN(swapchain, gpu.CreateSwapchain(surface, {.width = size.width, .height = size.height}));
    state->swapchain = std::move(swapchain);
    const rhi::Format format = state->swapchain->ImageFormat();
    state->resources = std::make_unique<rhi::ResourceManager>(gpu);
    INTERI_TRY_ASSIGN(pools, rhi::CommandListPool::Create(gpu, jobs, kFramesInFlight));
    state->commandPools = std::make_unique<rhi::CommandListPool>(std::move(pools));
    INTERI_TRY_ASSIGN(meshes, MeshPool::Create(gpu, *state->resources, 1024 * 1024, 4 * 1024 * 1024));
    state->meshes = std::move(meshes);
    INTERI_TRY_ASSIGN(renderer, ForwardRenderer::Create(
                                    gpu, *state->resources, *state->meshes, format,
                                    ForwardSettings{.framesInFlight = kFramesInFlight, .maxObjects = kMaxObjects}));
    state->renderer = std::move(renderer);
    INTERI_TRY_ASSIGN(decalRenderer, DecalRenderer::Create(gpu, *state->resources, format, 4096, kFramesInFlight));
    state->decalRenderer = std::move(decalRenderer);
    INTERI_TRY_ASSIGN(ui, ImGuiRenderer::Create(gpu, *state->resources, format,
                                                ImGuiRenderer::Settings{.framesInFlight = kFramesInFlight}));
    state->ui = std::move(ui);
    state->chunkMeshes = std::make_unique<destruction::ChunkMeshCache>(*state->meshes);
    state->graph = std::make_unique<FrameGraph>(gpu, *state->resources);

    State& uploaded = *state;
    const auto upload = [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands) -> Result<void>
    {
        INTERI_TRY_ASSIGN(cube, uploaded.meshes->Add(uploads, commands, MakeCube(0.5f)));
        uploaded.cube = cube;
        INTERI_TRY_ASSIGN(interior, destruction::CreateInteriorMaterials(*uploaded.device, uploads, commands));
        uploaded.interior = std::move(interior);
        return {};
    };
    INTERI_TRY(UploadNow(gpu, upload));
    state->chunkMaterials.surface = {Material{.baseColor = {0.72f, 0.71f, 0.68f}, .roughness = 0.85f}};
    state->chunkMaterials.interior = state->interior.materials;
    state->chunkMaterials.fallback = state->chunkMaterials.surface[0];
    return std::unique_ptr<EditorRenderer>(new EditorRenderer(std::move(state)));
}

Result<void> EditorRenderer::AddAsset(const std::shared_ptr<const destruction::DestructibleAsset>& asset)
{
    State& state = *m_state;
    if (state.chunkMeshes->Find(asset.get()) != nullptr)
    {
        return {};
    }
    return UploadNow(*state.device, [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands)
                     { return state.chunkMeshes->Add(uploads, commands, asset); });
}

std::uint64_t EditorRenderer::ViewportTexture(std::uint32_t width, std::uint32_t height)
{
    State& state = *m_state;
    if (width == 0 || height == 0)
    {
        return 0;
    }
    if (width != state.viewportWidth || height != state.viewportHeight || !state.viewport.IsValid())
    {
        if (state.viewport.IsValid())
        {
            state.resources->DestroyTexture(state.viewport); // deferred: earlier frames may still sample it
            state.viewport = {};
        }
        auto texture =
            state.device->CreateTexture({.width = width,
                                         .height = height,
                                         .format = state.swapchain->ImageFormat(),
                                         .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled,
                                         .debugName = "Editor viewport"});
        if (!texture)
        {
            INTERI_LOG_ERROR("Editor", "viewport texture: {}", texture.error().message);
            state.viewportWidth = state.viewportHeight = 0;
            return 0;
        }
        state.viewport = *texture;
        state.viewportWidth = width;
        state.viewportHeight = height;
        state.viewportWritten = false;
    }
    return state.ui->TextureId(state.viewport);
}

bool EditorRenderer::Render(const EditorFrame& frame)
{
    State& state = *m_state;
    state.commandPools->BeginFrame();
    state.resources->Collect();

    const WindowExtent size = state.window->Size();
    state.swapchain->Resize(size.width, size.height);
    auto image = state.swapchain->AcquireNextImage();
    if (!image.has_value() || frame.ui == nullptr)
    {
        return false;
    }

    FrameGraph& graph = *state.graph;
    graph.Reset();
    FGTexture target =
        graph.ImportTexture("swapchain", *image, rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    std::vector<FGTexture> sampled;
    if (state.viewport.IsValid())
    {
        state.objects.clear();
        for (const EditorBox& box : frame.boxes)
        {
            state.objects.push_back({.mesh = state.cube,
                                     .transform = box.transform,
                                     .material = {.baseColor = box.color, .roughness = 0.8f},
                                     .occluder = box.occluder});
        }
        for (const EditorChunk& chunk : frame.chunks)
        {
            const destruction::AssetMeshes* meshes =
                chunk.asset == nullptr ? nullptr : state.chunkMeshes->Find(chunk.asset);
            if (meshes == nullptr)
            {
                continue;
            }
            for (const destruction::ChunkSubmesh& submesh : meshes->Submeshes(chunk.chunk))
            {
                Material material = state.chunkMaterials.Get(submesh);
                if (chunk.color)
                {
                    material = {.baseColor = *chunk.color, .roughness = 0.7f};
                }
                state.objects.push_back({.mesh = submesh.mesh, .transform = chunk.transform, .material = material});
            }
        }
        state.decals.clear();
        if (frame.world != nullptr && frame.destruction != nullptr)
        {
            (void)state.gatherer.Gather(*frame.world, *frame.destruction, *state.chunkMeshes, state.chunkMaterials,
                                        state.objects, state.renderer->MaxObjects());
            destruction::GatherCrackDecals(*frame.world, *frame.destruction, state.decals);
        }
        if (state.objects.size() > state.renderer->MaxObjects())
        {
            state.objects.resize(state.renderer->MaxObjects());
        }

        FGTexture viewport =
            graph.ImportTexture("viewport", state.viewport,
                                state.viewportWritten ? rhi::ResourceState::ShaderRead : rhi::ResourceState::Undefined,
                                rhi::ResourceState::ShaderRead);
        const ForwardOutputs outputs =
            state.renderer->AddPasses(graph, viewport, state.viewportWidth, state.viewportHeight, frame.view,
                                      state.objects, {0.32f, 0.36f, 0.42f});
        const Mat4 viewProjection =
            PerspectiveReverseZ(frame.view.verticalFov,
                                static_cast<float>(state.viewportWidth) / static_cast<float>(state.viewportHeight),
                                frame.view.nearPlane) *
            frame.view.view;
        state.decalRenderer->AddPasses(graph, state.decals, DecalView{.viewProjection = viewProjection}, viewport,
                                       outputs.depth, state.viewportWidth, state.viewportHeight);
        sampled.push_back(viewport);
        state.viewportWritten = true;
    }
    const std::uint32_t width = state.swapchain->Width();
    const std::uint32_t height = state.swapchain->Height();
    if (auto added = state.ui->AddPass(
            graph, *frame.ui, target, width, height,
            ImGuiRenderer::PassOptions{.sampled = sampled, .clear = true, .clearColor = {0.1f, 0.1f, 0.11f, 1.0f}});
        !added)
    {
        INTERI_LOG_ERROR("Editor", "ui: {}", added.error().message);
        return false;
    }
    if (auto compiled = graph.Compile(); !compiled)
    {
        INTERI_LOG_ERROR("Editor", "frame graph: {}", compiled.error().message);
        return false;
    }
    rhi::ICommandList& commands = state.commandPools->Acquire();
    bool ok = true;
    if (auto executed = graph.Execute(commands); !executed)
    {
        INTERI_LOG_ERROR("Editor", "frame graph: {}", executed.error().message);
        ok = false;
    }
    rhi::ICommandList* const lists[] = {&commands};
    const std::uint64_t value = state.device->Submit(lists, *state.swapchain);
    if (auto presented = state.swapchain->Present(); !presented)
    {
        INTERI_LOG_ERROR("Editor", "present: {}", presented.error().message);
        ok = false;
    }
    state.commandPools->EndFrame(value);
    return ok;
}

} // namespace interi::editor
