// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/containers/SlotMap.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace interi::rhi
{

struct BufferTag;
struct TextureTag;
struct PipelineTag;
struct CommandPoolTag;
struct HeapTag;
struct SamplerTag;

/// Backend objects are referred to by generational handles; using a destroyed handle is a programming error.
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using PipelineHandle = Handle<PipelineTag>;
using CommandPoolHandle = Handle<CommandPoolTag>;
using HeapHandle = Handle<HeapTag>;
using SamplerHandle = Handle<SamplerTag>;

/// Enables |, & and ~ on a flags enum.
template <typename E>
struct EnableFlags : std::false_type
{
};

template <typename E>
    requires EnableFlags<E>::value
[[nodiscard]] constexpr E operator|(E a, E b) noexcept
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) | static_cast<U>(b));
}

template <typename E>
    requires EnableFlags<E>::value
[[nodiscard]] constexpr E operator&(E a, E b) noexcept
{
    using U = std::underlying_type_t<E>;
    return static_cast<E>(static_cast<U>(a) & static_cast<U>(b));
}

/// True when every bit of `bits` is set in `flags`.
template <typename E>
    requires EnableFlags<E>::value
[[nodiscard]] constexpr bool HasFlags(E flags, E bits) noexcept
{
    return (flags & bits) == bits;
}

enum class BufferUsage : std::uint8_t
{
    None = 0,
    CopySrc = 1u << 0,
    CopyDst = 1u << 1,
    Storage = 1u << 2, ///< Read/write from shaders (bindless storage buffer).
    Uniform = 1u << 3,
    Vertex = 1u << 4,
    Index = 1u << 5,
    Indirect = 1u << 6,
};
template <>
struct EnableFlags<BufferUsage> : std::true_type
{
};

enum class TextureUsage : std::uint8_t
{
    None = 0,
    CopySrc = 1u << 0,
    CopyDst = 1u << 1,
    Sampled = 1u << 2,
    Storage = 1u << 3,
    ColorTarget = 1u << 4,
    DepthTarget = 1u << 5,
};
template <>
struct EnableFlags<TextureUsage> : std::true_type
{
};

/// Where a buffer lives. Upload and Readback buffers are persistently mapped.
enum class MemoryUsage : std::uint8_t
{
    GpuOnly,
    Upload,   ///< CPU writes, GPU reads (staging, per-frame constants).
    Readback, ///< GPU writes, CPU reads.
};

enum class Format : std::uint8_t
{
    Unknown,
    R8G8B8A8Unorm,
    R8G8B8A8Srgb,
    B8G8R8A8Unorm,
    B8G8R8A8Srgb,
    R16G16B16A16Float,
    R32Float,
    R32Uint,
    R32G32Float,
    R32G32B32Float,
    R32G32B32A32Float,
    D32Float,
};

/// How a resource is used at a point in a command list. Barriers transition between states.
enum class ResourceState : std::uint8_t
{
    Undefined, ///< Contents may be discarded.
    CopySrc,
    CopyDst,
    ShaderRead,  ///< Sampled or read-only storage access from any shader stage.
    ShaderWrite, ///< Read/write storage access from any shader stage.
    ColorTarget,
    DepthWrite,
    DepthRead,
    VertexBuffer,
    IndexBuffer,
    IndirectArgument,
    HostRead, ///< Visible to the CPU after the submission completes (readback).
    Present,
};

struct BufferDesc
{
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::None;
    MemoryUsage memory = MemoryUsage::GpuOnly;
    std::string_view debugName{}; // NOLINT(readability-redundant-member-init): lets designated inits omit it
};

struct TextureDesc
{
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    Format format = Format::Unknown;
    TextureUsage usage = TextureUsage::None;
    std::string_view debugName{}; // NOLINT(readability-redundant-member-init): lets designated inits omit it
};

enum class ShaderStage : std::uint8_t
{
    Compute,
    Vertex,
    Pixel,
};

/// Push constants available to every pipeline (the guaranteed Vulkan minimum; D3D12 root constants are larger).
inline constexpr std::uint32_t kMaxPushConstantSize = 128;

/// Texture-to-buffer copies need row pitches aligned to this (the D3D12 placement rule, adopted everywhere).
inline constexpr std::uint32_t kTextureRowPitchAlignment = 256;

/// Bytes per pixel of uncompressed formats.
[[nodiscard]] constexpr std::uint32_t BytesPerPixel(Format format) noexcept
{
    switch (format)
    {
    case Format::R8G8B8A8Unorm:
    case Format::R8G8B8A8Srgb:
    case Format::B8G8R8A8Unorm:
    case Format::B8G8R8A8Srgb:
    case Format::R32Float:
    case Format::R32Uint:
    case Format::D32Float:
        return 4;
    case Format::R16G16B16A16Float:
    case Format::R32G32Float:
        return 8;
    case Format::R32G32B32Float:
        return 12;
    case Format::R32G32B32A32Float:
        return 16;
    case Format::Unknown:
        return 0;
    }
    return 0;
}

/// Sentinel for "no bindless slot", e.g. a resource created without Storage or Sampled usage.
inline constexpr std::uint32_t kInvalidBindlessIndex = 0xFFFF'FFFFu;

enum class IndexFormat : std::uint8_t
{
    Uint16,
    Uint32,
};

enum class CompareOp : std::uint8_t
{
    Never,
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
    Always,
};

enum class CullMode : std::uint8_t
{
    None,
    Back,
    Front,
};

enum class LoadOp : std::uint8_t
{
    Load,
    Clear,
    DontCare,
};

enum class StoreOp : std::uint8_t
{
    Store,
    DontCare,
};

/// Rasterization pipeline. Conventions: counter-clockwise triangles (seen in clip space with +Y up) are front
/// faces; clip-space depth is 0..1. Vertices are pulled by the shaders from bindless buffers, so there is no vertex
/// input layout.
/// How a pipeline's colour output combines with the target (the same for every colour attachment).
enum class BlendMode : std::uint8_t
{
    Opaque,   ///< Replace.
    Alpha,    ///< rgb = src * a + dst * (1 - a); alpha keeps dst.
    Additive, ///< rgb = src * a + dst; alpha keeps dst.
};

struct GraphicsPipelineDesc
{
    std::span<const std::byte> vertexCode{}; // NOLINT(readability-redundant-member-init)
    std::span<const std::byte> pixelCode{};  // NOLINT(readability-redundant-member-init)
    std::span<const Format> colorFormats{};  // NOLINT(readability-redundant-member-init)
    Format depthFormat = Format::Unknown;
    CullMode cullMode = CullMode::Back;
    bool depthTest = true;
    bool depthWrite = true;
    CompareOp depthCompare = CompareOp::Greater; ///< Reverse-Z: larger depth is nearer.
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    BlendMode blend = BlendMode::Opaque;
    std::string_view debugName{}; // NOLINT(readability-redundant-member-init)
};

enum class Filter : std::uint8_t
{
    Nearest,
    Linear,
};

enum class AddressMode : std::uint8_t
{
    Repeat,
    ClampToEdge,
};

struct SamplerDesc
{
    Filter filter = Filter::Linear;
    AddressMode address = AddressMode::Repeat;
    /// Depth-comparison sampler (shadow maps): SampleCmp returns the fraction of texels where
    /// `reference compareOp texel` holds.
    bool compare = false;
    CompareOp compareOp = CompareOp::GreaterEqual;
};

struct ColorAttachment
{
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    std::array<float, 4> clearColor{};
};

struct DepthAttachment
{
    TextureHandle texture;
    LoadOp load = LoadOp::Clear;
    StoreOp store = StoreOp::Store;
    float clearDepth = 0.0f; ///< Reverse-Z: 0 is the far plane.
    std::uint32_t arrayLayer = 0;
};

/// A rendering pass with dynamic rendering: attachments in ColorTarget / DepthWrite state.
struct RenderingDesc
{
    std::span<const ColorAttachment> colors{}; // NOLINT(readability-redundant-member-init)
    const DepthAttachment* depth = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct ComputePipelineDesc
{
    /// Backend bytecode: SPIR-V for Vulkan (see interi_add_shaders), one entry point named "main".
    std::span<const std::byte> code{}; // NOLINT(readability-redundant-member-init)
    std::string_view debugName{};      // NOLINT(readability-redundant-member-init)
};

/// What a resource needs from memory, for placing it in a heap.
struct MemoryRequirements
{
    std::uint64_t size = 0;
    std::uint64_t alignment = 1;
    std::uint32_t memoryTypeBits = 0; ///< Backend memory types the resource can live in.
};

/// A block of GPU-only memory that placed resources are created in; resources whose lifetimes do not overlap can
/// share its bytes (transient aliasing).
struct HeapDesc
{
    std::uint64_t size = 0;
    std::uint32_t memoryTypeBits = 0; ///< Intersection of the placed resources' MemoryRequirements.
};

struct BufferBarrier
{
    BufferHandle buffer;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;

    friend constexpr bool operator==(const BufferBarrier&, const BufferBarrier&) noexcept = default;
};

struct TextureBarrier
{
    TextureHandle texture;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;

    friend constexpr bool operator==(const TextureBarrier&, const TextureBarrier&) noexcept = default;
};

} // namespace interi::rhi
