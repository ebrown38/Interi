// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/editor_ui/ImGuiRenderer.h>
#include <interi/render/Texture.h>
#include <interi/rhi/Device.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi/UploadQueue.h>
#include <interi/shaders/ImGui.ps.h>
#include <interi/shaders/ImGui.vs.h>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>

namespace interi::editor
{

namespace
{

static_assert(sizeof(ImDrawVert) == 20, "ImGui.vs.hlsl pulls 20-byte vertices");
static_assert(sizeof(ImDrawIdx) == 2, "the renderer binds 16-bit indices");

// Must match engine/editor_ui/shaders/ImGuiCommon.hlsli.
struct ImGuiPush
{
    float scale[2];
    float translate[2];
    std::uint32_t vertices;
    std::uint32_t texture;
    std::uint32_t sampler;
    std::uint32_t linearizeColors;
};

[[nodiscard]] bool IsSrgb(rhi::Format format) noexcept
{
    return format == rhi::Format::R8G8B8A8Srgb || format == rhi::Format::B8G8R8A8Srgb;
}

} // namespace

ImGuiRenderer::ImGuiRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, rhi::Format colorFormat,
                             const Settings& settings)
    : m_device(device)
    , m_resources(resources)
    , m_settings(settings)
    , m_linearize(IsSrgb(colorFormat))
{
}

Result<std::unique_ptr<ImGuiRenderer>> ImGuiRenderer::Create(rhi::IDevice& device, rhi::ResourceManager& resources,
                                                             rhi::Format colorFormat, const Settings& settings)
{
    INTERI_ASSERT(settings.framesInFlight > 0 && settings.maxVertices > 0 && settings.maxIndices > 0);
    std::unique_ptr<ImGuiRenderer> renderer(new ImGuiRenderer(device, resources, colorFormat, settings));
    const std::array formats{colorFormat};
    INTERI_TRY_ASSIGN(pipeline, device.CreateGraphicsPipeline({.vertexCode = std::as_bytes(std::span(kImGuiVs)),
                                                               .pixelCode = std::as_bytes(std::span(kImGuiPs)),
                                                               .colorFormats = formats,
                                                               .depthFormat = settings.depthFormat,
                                                               .cullMode = rhi::CullMode::None,
                                                               .depthTest = false,
                                                               .depthWrite = false,
                                                               .blend = rhi::BlendMode::Alpha,
                                                               .debugName = "ImGui"}));
    renderer->m_pipeline = pipeline;
    INTERI_TRY_ASSIGN(sampler,
                      device.CreateSampler({.filter = rhi::Filter::Linear, .address = rhi::AddressMode::ClampToEdge}));
    renderer->m_sampler = sampler;
    for (std::uint32_t i = 0; i < settings.framesInFlight; ++i)
    {
        INTERI_TRY_ASSIGN(vertices,
                          device.CreateBuffer({.size = std::uint64_t{sizeof(ImDrawVert)} * settings.maxVertices,
                                               .usage = rhi::BufferUsage::Storage,
                                               .memory = rhi::MemoryUsage::Upload,
                                               .debugName = "ImGui vertices"}));
        renderer->m_frames.push_back({.vertices = vertices, .indices = {}});
        INTERI_TRY_ASSIGN(indices, device.CreateBuffer({.size = std::uint64_t{sizeof(ImDrawIdx)} * settings.maxIndices,
                                                        .usage = rhi::BufferUsage::Index,
                                                        .memory = rhi::MemoryUsage::Upload,
                                                        .debugName = "ImGui indices"}));
        renderer->m_frames.back().indices = indices;
    }

    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName = "interi_rhi";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset | ImGuiBackendFlags_RendererHasTextures;
    return renderer;
}

ImGuiRenderer::~ImGuiRenderer()
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        for (ImTextureData* texture : ImGui::GetPlatformIO().Textures)
        {
            if (texture->RefCount == 1)
            {
                DestroyTexture(*texture);
            }
        }
    }
    for (const auto& [id, texture] : m_textures)
    {
        m_resources.DestroyTexture(texture);
    }
    for (const Frame& frame : m_frames)
    {
        m_resources.DestroyBuffer(frame.vertices);
        if (frame.indices.IsValid())
        {
            m_resources.DestroyBuffer(frame.indices);
        }
    }
    if (m_sampler.IsValid())
    {
        m_resources.DestroySampler(m_sampler);
    }
    if (m_pipeline.IsValid())
    {
        m_resources.DestroyPipeline(m_pipeline);
    }
}

std::uint64_t ImGuiRenderer::TextureId(rhi::TextureHandle texture) const
{
    return std::uint64_t{m_device.BindlessIndex(texture)} + 1; // 0 is ImTextureID_Invalid
}

Result<void> ImGuiRenderer::UpdateTexture(ImTextureData& texture)
{
    // The whole texture is re-created: the atlas changes rarely (new font sizes), so a synchronous upload is fine.
    render::ImageRgba8 image{.width = static_cast<std::uint32_t>(texture.Width),
                             .height = static_cast<std::uint32_t>(texture.Height),
                             .texels = {}};
    image.texels.resize(std::size_t{image.width} * image.height * 4);
    const auto* pixels = static_cast<const std::uint8_t*>(texture.GetPixels());
    if (texture.Format == ImTextureFormat_RGBA32)
    {
        std::memcpy(image.texels.data(), pixels, image.texels.size());
    }
    else
    {
        for (std::size_t i = 0; i < std::size_t{image.width} * image.height; ++i)
        {
            image.texels[i * 4 + 0] = 255;
            image.texels[i * 4 + 1] = 255;
            image.texels[i * 4 + 2] = 255;
            image.texels[i * 4 + 3] = pixels[i];
        }
    }

    const std::uint64_t staging =
        std::max<std::uint64_t>(std::uint64_t{4} << 20, (std::uint64_t{image.width} * 4 + 256) * image.height);
    rhi::UploadQueue uploads(m_device, staging);
    INTERI_TRY_ASSIGN(pool, m_device.CreateCommandPool());
    rhi::ICommandList& commands = m_device.BeginCommandList(pool);
    const std::array mips{std::move(image)};
    auto created =
        render::CreateTexture(m_device, uploads, commands, mips, rhi::Format::R8G8B8A8Unorm, "ImGui texture");
    if (created)
    {
        rhi::ICommandList* const lists[] = {&commands};
        const std::uint64_t value = m_device.Submit(lists);
        uploads.Submitted(value);
        m_device.WaitForValue(value);
    }
    m_device.DestroyCommandPool(pool);
    if (!created)
    {
        return std::unexpected(created.error());
    }

    DestroyTexture(texture);
    const std::uint64_t id = TextureId(*created);
    m_textures[id] = *created;
    texture.SetTexID(id);
    texture.SetStatus(ImTextureStatus_OK);
    return {};
}

void ImGuiRenderer::DestroyTexture(ImTextureData& texture)
{
    const auto found = m_textures.find(texture.GetTexID());
    if (texture.GetTexID() != ImTextureID_Invalid && found != m_textures.end())
    {
        m_resources.DestroyTexture(found->second); // deferred until the GPU is done with it
        m_textures.erase(found);
    }
    texture.SetTexID(ImTextureID_Invalid);
    texture.SetStatus(ImTextureStatus_Destroyed);
}

Result<void> ImGuiRenderer::Prepare(ImDrawData& drawData)
{
    if (drawData.Textures != nullptr)
    {
        for (ImTextureData* texture : *drawData.Textures)
        {
            if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
            {
                INTERI_TRY(UpdateTexture(*texture));
            }
            else if (texture->Status == ImTextureStatus_WantDestroy &&
                     std::cmp_greater_equal(texture->UnusedFrames, m_settings.framesInFlight))
            {
                DestroyTexture(*texture);
            }
        }
    }

    m_draws.clear();
    m_frame = (m_frame + 1) % static_cast<std::uint32_t>(m_frames.size());
    const Frame& frame = m_frames[m_frame];
    std::byte* vertices = m_device.MappedMemory(frame.vertices).data();
    std::byte* indices = m_device.MappedMemory(frame.indices).data();

    const ImVec2 scale = drawData.FramebufferScale;
    m_scale[0] = 2.0f / drawData.DisplaySize.x;
    m_scale[1] = 2.0f / drawData.DisplaySize.y;
    m_translate[0] = -1.0f - drawData.DisplayPos.x * m_scale[0];
    m_translate[1] = -1.0f - drawData.DisplayPos.y * m_scale[1];

    std::uint32_t vertexCount = 0;
    std::uint32_t indexCount = 0;
    for (const ImDrawList* list : drawData.CmdLists)
    {
        const auto listVertices = static_cast<std::uint32_t>(list->VtxBuffer.Size);
        const auto listIndices = static_cast<std::uint32_t>(list->IdxBuffer.Size);
        if (vertexCount + listVertices > m_settings.maxVertices || indexCount + listIndices > m_settings.maxIndices)
        {
            INTERI_LOG_WARN("Editor", "ImGui draw data exceeds the renderer's capacity; dropping the rest");
            break;
        }
        std::memcpy(vertices + std::size_t{vertexCount} * sizeof(ImDrawVert), list->VtxBuffer.Data,
                    std::size_t{listVertices} * sizeof(ImDrawVert));
        std::memcpy(indices + std::size_t{indexCount} * sizeof(ImDrawIdx), list->IdxBuffer.Data,
                    std::size_t{listIndices} * sizeof(ImDrawIdx));
        for (const ImDrawCmd& command : list->CmdBuffer)
        {
            if (command.UserCallback != nullptr || command.ElemCount == 0)
            {
                continue; // the editor uses no callbacks
            }
            Draw draw{.clip = {(command.ClipRect.x - drawData.DisplayPos.x) * scale.x,
                               (command.ClipRect.y - drawData.DisplayPos.y) * scale.y,
                               (command.ClipRect.z - drawData.DisplayPos.x) * scale.x,
                               (command.ClipRect.w - drawData.DisplayPos.y) * scale.y},
                      .texture = static_cast<std::uint32_t>(command.GetTexID() - 1),
                      .indexCount = command.ElemCount,
                      .firstIndex = indexCount + command.IdxOffset,
                      .vertexOffset = static_cast<std::int32_t>(vertexCount + command.VtxOffset)};
            m_draws.push_back(draw);
        }
        vertexCount += listVertices;
        indexCount += listIndices;
    }
    return {};
}

void ImGuiRenderer::Record(rhi::ICommandList& commands, std::uint32_t width, std::uint32_t height) const
{
    if (m_draws.empty())
    {
        return;
    }
    const Frame& frame = m_frames[m_frame];
    commands.BindPipeline(m_pipeline);
    commands.BindIndexBuffer(frame.indices, 0, rhi::IndexFormat::Uint16);
    const auto maxX = static_cast<float>(width);
    const auto maxY = static_cast<float>(height);
    for (const Draw& draw : m_draws)
    {
        const float x0 = std::clamp(draw.clip[0], 0.0f, maxX);
        const float y0 = std::clamp(draw.clip[1], 0.0f, maxY);
        const float x1 = std::clamp(draw.clip[2], 0.0f, maxX);
        const float y1 = std::clamp(draw.clip[3], 0.0f, maxY);
        if (x1 <= x0 || y1 <= y0)
        {
            continue;
        }
        commands.SetScissor(static_cast<std::int32_t>(x0), static_cast<std::int32_t>(y0),
                            static_cast<std::uint32_t>(x1 - x0), static_cast<std::uint32_t>(y1 - y0));
        commands.PushConstants(ImGuiPush{.scale = {m_scale[0], m_scale[1]},
                                         .translate = {m_translate[0], m_translate[1]},
                                         .vertices = m_device.BindlessIndex(frame.vertices),
                                         .texture = draw.texture,
                                         .sampler = m_device.BindlessIndex(m_sampler),
                                         .linearizeColors = m_linearize ? 1u : 0u});
        commands.DrawIndexed(draw.indexCount, 1, draw.firstIndex, draw.vertexOffset, 0);
    }
}

Result<void> ImGuiRenderer::AddPass(render::FrameGraph& graph, ImDrawData& drawData, render::FGTexture& color,
                                    std::uint32_t width, std::uint32_t height, const PassOptions& options)
{
    INTERI_TRY(Prepare(drawData));
    graph.AddPass(
        "ImGui",
        [&](render::PassBuilder& builder)
        {
            for (const render::FGTexture texture : options.sampled)
            {
                builder.Read(texture, rhi::ResourceState::ShaderRead);
            }
            color = builder.Write(color, rhi::ResourceState::ColorTarget);
        },
        [this, color, width, height, clear = options.clear,
         clearColor = options.clearColor](render::PassContext& context)
        {
            rhi::ICommandList& commands = context.Commands();
            const std::array colors{rhi::ColorAttachment{.texture = context.Texture(color),
                                                         .load = clear ? rhi::LoadOp::Clear : rhi::LoadOp::Load,
                                                         .store = rhi::StoreOp::Store,
                                                         .clearColor = clearColor}};
            commands.BeginRendering({.colors = colors, .width = width, .height = height});
            Record(commands, width, height);
            commands.EndRendering();
        });
    return {};
}

} // namespace interi::editor
