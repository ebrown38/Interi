// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/containers/SlotMap.h>

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace interi::rhi
{

struct BufferTag;
struct TextureTag;
struct PipelineTag;
struct CommandPoolTag;

/// Backend objects are referred to by generational handles; using a destroyed handle is a programming error.
using BufferHandle = Handle<BufferTag>;
using TextureHandle = Handle<TextureTag>;
using PipelineHandle = Handle<PipelineTag>;
using CommandPoolHandle = Handle<CommandPoolTag>;

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
    std::string_view debugName;
};

struct TextureDesc
{
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t mipLevels = 1;
    std::uint32_t arrayLayers = 1;
    Format format = Format::Unknown;
    TextureUsage usage = TextureUsage::None;
    std::string_view debugName;
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
