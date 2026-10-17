// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/render/FrameGraph.h>
#include <interi/rhi/Types.h>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

struct ImDrawData;
struct ImTextureData;

namespace interi::rhi
{
class IDevice;
class ICommandList;
class ResourceManager;
} // namespace interi::rhi

namespace interi::editor
{

/// Draws Dear ImGui's draw data with the RHI: vertices are pulled from a raw buffer (one per frame in flight),
/// textures (the font atlas, or anything passed to ImGui::Image as TextureId) are sampled through the bindless table,
/// and every command gets a scissor clamped to the target. Implements ImGui's texture protocol
/// (ImGuiBackendFlags_RendererHasTextures): the atlas is created and updated by synchronous uploads.
class ImGuiRenderer
{
public:
    struct Settings
    {
        std::uint32_t framesInFlight = 2;
        std::uint32_t maxVertices = 1u << 18;
        std::uint32_t maxIndices = 1u << 19;
        rhi::Format depthFormat = rhi::Format::Unknown; ///< Of the pass it draws in (not tested or written).
    };

    /// Sets ImGui's backend flags on the current context.
    [[nodiscard]] static Result<std::unique_ptr<ImGuiRenderer>>
    Create(rhi::IDevice& device, rhi::ResourceManager& resources, rhi::Format colorFormat, const Settings& settings);
    [[nodiscard]] static Result<std::unique_ptr<ImGuiRenderer>>
    Create(rhi::IDevice& device, rhi::ResourceManager& resources, rhi::Format colorFormat)
    {
        return Create(device, resources, colorFormat, Settings{});
    }
    ~ImGuiRenderer();

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    ImGuiRenderer(ImGuiRenderer&&) = delete;
    ImGuiRenderer& operator=(ImGuiRenderer&&) = delete;

    /// The ImTextureID to pass to ImGui::Image for a sampled texture: its bindless index + 1 (0 is invalid).
    [[nodiscard]] std::uint64_t TextureId(rhi::TextureHandle texture) const;

    /// Handles the draw data's texture requests and copies its vertices and indices into the next frame slot.
    /// Call once per frame after ImGui::Render, then Record (or AddPass) the same frame. Draws past the vertex or
    /// index capacity are dropped (logged).
    [[nodiscard]] Result<void> Prepare(ImDrawData& drawData);

    /// Records the prepared draws into a rendering pass over a `width` x `height` target.
    void Record(rhi::ICommandList& commands, std::uint32_t width, std::uint32_t height) const;

    struct PassOptions
    {
        /// Frame-graph textures the draw data shows through ImGui::Image (read as ShaderRead).
        std::span<const render::FGTexture> sampled{}; // NOLINT(readability-redundant-member-init)
        bool clear = false;                           ///< Clear `color` first instead of drawing over it.
        std::array<float, 4> clearColor{};
    };

    /// Prepares and adds a pass that draws into `color`.
    [[nodiscard]] Result<void> AddPass(render::FrameGraph& graph, ImDrawData& drawData, render::FGTexture& color,
                                       std::uint32_t width, std::uint32_t height, const PassOptions& options);

    /// Number of draws (ImDrawCmds) recorded by the last Prepare.
    [[nodiscard]] std::size_t DrawCount() const noexcept { return m_draws.size(); }

private:
    struct Draw
    {
        float clip[4];         ///< x0, y0, x1, y1 in target pixels.
        std::uint32_t texture; ///< Bindless index.
        std::uint32_t indexCount;
        std::uint32_t firstIndex;
        std::int32_t vertexOffset;
    };
    struct Frame
    {
        rhi::BufferHandle vertices;
        rhi::BufferHandle indices;
    };

    ImGuiRenderer(rhi::IDevice& device, rhi::ResourceManager& resources, rhi::Format colorFormat,
                  const Settings& settings);
    [[nodiscard]] Result<void> UpdateTexture(ImTextureData& texture);
    void DestroyTexture(ImTextureData& texture);

    rhi::IDevice& m_device;
    rhi::ResourceManager& m_resources;
    Settings m_settings;
    bool m_linearize;
    rhi::PipelineHandle m_pipeline;
    rhi::SamplerHandle m_sampler;
    std::vector<Frame> m_frames;
    std::uint32_t m_frame = 0;
    std::map<std::uint64_t, rhi::TextureHandle> m_textures; ///< ImGui-owned textures by TexID.
    std::vector<Draw> m_draws;
    float m_scale[2] = {};
    float m_translate[2] = {};
};

} // namespace interi::editor
