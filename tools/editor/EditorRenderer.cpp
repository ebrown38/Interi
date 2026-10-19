// SPDX-License-Identifier: MIT
#include "EditorRenderer.h"

#include <interi/core/Log.h>
#include <interi/core/math/Mat4.h>
#include <interi/editor_ui/EditorExtensions.h>
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
#include <interi/scene/Mesh.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <utility>
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
    std::vector<Decal> decals;
    std::uint32_t poolGeneration = 0; ///< Raised whenever the mesh pool is cleared (see RenderFeatureContext).
    std::shared_ptr<void> alive = std::make_shared<int>(0); ///< RenderFeatureContext::renderer.
    std::unique_ptr<rhi::ISwapchain> swapchain;
    std::unique_ptr<FrameGraph> graph;
    MeshRange cube;
    /// Scene meshes by object: the mesh (kept alive so its address isn't reused) and a range per slot.
    struct SceneMesh
    {
        std::shared_ptr<const scene::TriangleMesh> mesh;
        std::vector<std::optional<MeshRange>> slots;
    };
    std::map<const scene::TriangleMesh*, SceneMesh> sceneMeshes;
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
    state->graph = std::make_unique<FrameGraph>(gpu, *state->resources);

    State& uploaded = *state;
    const auto upload = [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands) -> Result<void>
    {
        INTERI_TRY_ASSIGN(cube, uploaded.meshes->Add(uploads, commands, MakeCube(0.5f)));
        uploaded.cube = cube;
        return {};
    };
    INTERI_TRY(UploadNow(gpu, upload));
    return std::unique_ptr<EditorRenderer>(new EditorRenderer(std::move(state)));
}

Result<void> EditorRenderer::ResetMeshPool()
{
    State& state = *m_state;
    state.device->WaitForValue(state.device->LastSubmittedValue());
    state.sceneMeshes.clear();
    ++state.poolGeneration;
    state.meshes->Clear();
    return UploadNow(*state.device,
                     [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands) -> Result<void>
                     {
                         INTERI_TRY_ASSIGN(cube, state.meshes->Add(uploads, commands, MakeCube(0.5f)));
                         state.cube = cube;
                         return {};
                     });
}

Result<void> EditorRenderer::AddMesh(const std::shared_ptr<const scene::TriangleMesh>& mesh)
{
    State& state = *m_state;
    if (mesh == nullptr || state.sceneMeshes.contains(mesh.get()))
    {
        return {};
    }
    // One render mesh per slot: three vertices per triangle (the corner normals are per corner).
    std::vector<MeshData> slots(mesh->SlotCount());
    for (std::size_t t = 0; t < mesh->TriangleCount(); ++t)
    {
        MeshData& data = slots[mesh->slots[t]];
        for (std::size_t k = 0; k < 3; ++k)
        {
            data.indices.push_back(static_cast<std::uint32_t>(data.vertices.size()));
            data.vertices.push_back(
                {.position = mesh->positions[mesh->indices[3 * t + k]], .normal = mesh->normals[3 * t + k]});
        }
    }
    State::SceneMesh entry{.mesh = mesh, .slots = {}};
    const auto add = [&](rhi::UploadQueue& uploads, rhi::ICommandList& commands) -> Result<void>
    {
        entry.slots.clear();
        for (const MeshData& data : slots)
        {
            if (data.indices.empty())
            {
                entry.slots.emplace_back();
                continue;
            }
            INTERI_TRY_ASSIGN(range, state.meshes->Add(uploads, commands, data));
            entry.slots.emplace_back(range);
        }
        return {};
    };
    Result<void> added = UploadNow(*state.device, add);
    if (!added && added.error().code == ErrorCode::OutOfMemory)
    {
        INTERI_LOG_INFO("Editor", "mesh pool full: clearing it ({} meshes cached)", state.sceneMeshes.size());
        INTERI_TRY(ResetMeshPool());
        added = UploadNow(*state.device, add);
    }
    if (added)
    {
        state.sceneMeshes.emplace(mesh.get(), std::move(entry));
    }
    return added;
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
    const bool capture = !m_capturePath.empty();
    rhi::TextureHandle image;
    if (!capture)
    {
        auto acquired = state.swapchain->AcquireNextImage();
        if (!acquired)
        {
            return false; // minimised: nothing to draw into
        }
        image = *acquired;
    }
    if (frame.ui == nullptr)
    {
        return false;
    }

    FrameGraph& graph = *state.graph;
    graph.Reset();
    std::vector<FGTexture> sampled;
    if (state.viewport.IsValid())
    {
        state.objects.clear();
        state.decals.clear();
        // Features first: a full mesh pool is cleared while they gather, which invalidates every range handed out so
        // far, so the editor's own objects take their ranges afterwards.
        if (frame.extensions != nullptr && frame.document != nullptr)
        {
            RenderFeatureContext context;
            context.device = state.device.get();
            context.resources = state.resources.get();
            context.meshes = state.meshes.get();
            context.generation = state.poolGeneration;
            context.renderer = state.alive;
            context.maxObjects = state.renderer->MaxObjects();
            context.upload = [&state](const RenderFeatureContext::UploadFunction& record)
            { return UploadNow(*state.device, record); };
            RenderFeatureOutput output{.objects = &state.objects, .decals = &state.decals};
            const auto gathered = frame.extensions->GatherRenderObjects(context, output, *frame.document,
                                                                        [this]() { return ResetMeshPool(); });
            state.poolGeneration = context.generation;
            if (!gathered)
            {
                INTERI_LOG_ERROR("Editor", "render features: {}", gathered.error().message);
            }
        }
        const auto boxes = frame.replaceScene ? std::span<const EditorBox>{} : frame.boxes;
        const auto meshes = frame.replaceScene ? std::span<const EditorMesh>{} : frame.meshes;
        for (const EditorBox& box : boxes)
        {
            state.objects.push_back({.mesh = state.cube,
                                     .transform = box.transform,
                                     .material = {.baseColor = box.color, .roughness = 0.8f},
                                     .occluder = box.occluder});
        }
        for (const EditorMesh& mesh : meshes)
        {
            const auto found = state.sceneMeshes.find(mesh.mesh);
            if (found == state.sceneMeshes.end())
            {
                continue;
            }
            for (std::size_t slot = 0; slot < found->second.slots.size(); ++slot)
            {
                if (!found->second.slots[slot])
                {
                    continue;
                }
                const Vec3 color = mesh.color ? *mesh.color
                                   : mesh.slotColors.empty()
                                       ? Vec3(0.6f)
                                       : mesh.slotColors[std::min(slot, mesh.slotColors.size() - 1)];
                state.objects.push_back({.mesh = *found->second.slots[slot],
                                         .transform = mesh.transform,
                                         .material = {.baseColor = color, .roughness = 0.75f},
                                         .occluder = mesh.occluder});
            }
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
    if (capture)
    {
        return Capture(frame, sampled.empty() ? FGTexture{} : sampled[0]);
    }
    FGTexture target =
        graph.ImportTexture("swapchain", image, rhi::ResourceState::Undefined, rhi::ResourceState::Present);
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

bool EditorRenderer::Capture(const EditorFrame& frame, FGTexture viewport)
{
    State& state = *m_state;
    const std::string path = std::exchange(m_capturePath, {});
    const std::uint32_t width = state.swapchain->Width();
    const std::uint32_t height = state.swapchain->Height();
    const std::uint32_t rowPitch = (width * 4 + rhi::kTextureRowPitchAlignment - 1) / rhi::kTextureRowPitchAlignment *
                                   rhi::kTextureRowPitchAlignment;
    auto image = state.device->CreateTexture({.width = width,
                                              .height = height,
                                              .format = state.swapchain->ImageFormat(), // the UI pipeline's format
                                              .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::CopySrc,
                                              .debugName = "Editor capture"});
    auto readback = state.device->CreateBuffer({.size = std::uint64_t{rowPitch} * height,
                                                .usage = rhi::BufferUsage::CopyDst,
                                                .memory = rhi::MemoryUsage::Readback,
                                                .debugName = "Editor capture"});
    if (!image || !readback)
    {
        INTERI_LOG_ERROR("Editor", "capture: no target");
        return false;
    }
    FrameGraph& graph = *state.graph;
    FGTexture target =
        graph.ImportTexture("capture", *image, rhi::ResourceState::Undefined, rhi::ResourceState::CopySrc);
    const FGBuffer out =
        graph.ImportBuffer("capture readback", *readback, rhi::ResourceState::Undefined, rhi::ResourceState::HostRead);
    std::vector<FGTexture> sampled;
    if (viewport.IsValid())
    {
        sampled.push_back(viewport);
    }
    bool ok = static_cast<bool>(state.ui->AddPass(
        graph, *frame.ui, target, width, height,
        ImGuiRenderer::PassOptions{.sampled = sampled, .clear = true, .clearColor = {0.1f, 0.1f, 0.11f, 1.0f}}));
    graph.AddPass(
        "Capture",
        [&](PassBuilder& builder)
        {
            builder.Read(target, rhi::ResourceState::CopySrc);
            (void)builder.Write(out, rhi::ResourceState::CopyDst);
        },
        [target, out, rowPitch](PassContext& context)
        { context.Commands().CopyTextureToBuffer(context.Texture(target), context.Buffer(out), 0, rowPitch); });
    ok = ok && graph.Compile().has_value();
    rhi::ICommandList& commands = state.commandPools->Acquire();
    ok = ok && graph.Execute(commands).has_value();
    rhi::ICommandList* const lists[] = {&commands};
    const std::uint64_t value = state.device->Submit(lists);
    state.commandPools->EndFrame(value);
    state.device->WaitForValue(value);
    if (ok)
    {
        std::ofstream file(path, std::ios::binary);
        file << "P6\n" << width << " " << height << "\n255\n";
        const std::byte* mapped = state.device->MappedMemory(*readback).data();
        const rhi::Format format = state.swapchain->ImageFormat();
        const bool bgra = format == rhi::Format::B8G8R8A8Srgb || format == rhi::Format::B8G8R8A8Unorm;
        for (std::uint32_t y = 0; y < height; ++y)
        {
            for (std::uint32_t x = 0; x < width; ++x)
            {
                const std::byte* texel = mapped + std::size_t{y} * rowPitch + std::size_t{x} * 4;
                const std::array rgb{texel[bgra ? 2 : 0], texel[1], texel[bgra ? 0 : 2]};
                file.write(reinterpret_cast<const char*>(rgb.data()), 3);
            }
        }
        INTERI_LOG_INFO("Editor", "wrote {}", path);
    }
    state.resources->DestroyTexture(*image);
    state.resources->DestroyBuffer(*readback);
    return ok;
}

} // namespace interi::editor
