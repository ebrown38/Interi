// SPDX-License-Identifier: MIT
#include <interi/rhi_vulkan/ShaderReflection.h>

#include <spirv-reflect/spirv_reflect.h>

#include <format>
#include <vector>

namespace interi::rhi
{

namespace
{

/// Releases a reflected module on every path.
class ModuleGuard
{
public:
    explicit ModuleGuard(SpvReflectShaderModule& module)
        : m_module(module)
    {
    }
    ~ModuleGuard() { spvReflectDestroyShaderModule(&m_module); }

    ModuleGuard(const ModuleGuard&) = delete;
    ModuleGuard& operator=(const ModuleGuard&) = delete;
    ModuleGuard(ModuleGuard&&) = delete;
    ModuleGuard& operator=(ModuleGuard&&) = delete;

private:
    SpvReflectShaderModule& m_module;
};

} // namespace

Result<ShaderReflection> ReflectSpirv(std::span<const std::byte> spirv)
{
    SpvReflectShaderModule module{};
    if (spvReflectCreateShaderModule2(SPV_REFLECT_MODULE_FLAG_NO_COPY, spirv.size(), spirv.data(), &module) !=
        SPV_REFLECT_RESULT_SUCCESS)
    {
        return MakeError(ErrorCode::InvalidArgument, "not a valid SPIR-V module");
    }
    const ModuleGuard guard(module);
    if (module.entry_point_count != 1)
    {
        return MakeError(ErrorCode::InvalidArgument, "shader modules must have exactly one entry point");
    }

    ShaderReflection reflection;
    const SpvReflectEntryPoint& entry = module.entry_points[0];
    switch (entry.shader_stage)
    {
    case SPV_REFLECT_SHADER_STAGE_COMPUTE_BIT:
        reflection.stage = ShaderStage::Compute;
        reflection.workgroupSize = {entry.local_size.x, entry.local_size.y, entry.local_size.z};
        break;
    case SPV_REFLECT_SHADER_STAGE_VERTEX_BIT:
        reflection.stage = ShaderStage::Vertex;
        break;
    case SPV_REFLECT_SHADER_STAGE_FRAGMENT_BIT:
        reflection.stage = ShaderStage::Pixel;
        break;
    default:
        return MakeError(ErrorCode::Unsupported, "unsupported shader stage");
    }

    for (std::uint32_t i = 0; i < module.push_constant_block_count; ++i)
    {
        const SpvReflectBlockVariable& block = module.push_constant_blocks[i];
        reflection.pushConstantSize = std::max(reflection.pushConstantSize, block.offset + block.size);
    }
    if (reflection.pushConstantSize > kMaxPushConstantSize)
    {
        return MakeError(ErrorCode::InvalidArgument, std::format("push constants use {} bytes, the limit is {}",
                                                                 reflection.pushConstantSize, kMaxPushConstantSize));
    }

    for (std::uint32_t i = 0; i < module.descriptor_binding_count; ++i)
    {
        const SpvReflectDescriptorBinding& binding = module.descriptor_bindings[i];
        if (binding.set != 0 || binding.binding > 3)
        {
            return MakeError(ErrorCode::InvalidArgument,
                             std::format("resource '{}' uses set {} binding {}; only the bindless tables are allowed",
                                         binding.name != nullptr ? binding.name : "", binding.set, binding.binding));
        }
    }
    return reflection;
}

} // namespace interi::rhi
