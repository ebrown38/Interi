// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/rhi/Types.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace interi::rhi
{

/// What the engine needs to know about a SPIR-V module's interface.
struct ShaderReflection
{
    ShaderStage stage = ShaderStage::Compute;
    /// numthreads of a compute shader; {0, 0, 0} for other stages.
    std::array<std::uint32_t, 3> workgroupSize{};
    /// Bytes of push constants the entry point uses (0 if none).
    std::uint32_t pushConstantSize = 0;
};

/// Reflects a SPIR-V module with a single entry point. Fails with InvalidArgument when the module is not valid
/// SPIR-V, or when it breaks the engine's binding model: every resource must come from the bindless tables
/// (descriptor set 0, bindings 0-3) and push constants must fit kMaxPushConstantSize.
[[nodiscard]] Result<ShaderReflection> ReflectSpirv(std::span<const std::byte> spirv);

} // namespace interi::rhi
