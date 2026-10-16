// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>
#include <interi/render/ForwardRenderer.h>
#include <interi/render/Texture.h>
#include <interi/rhi/Types.h>

#include <cstdint>
#include <vector>

namespace interi::rhi
{
class IDevice;
class ICommandList;
class ResourceManager;
class UploadQueue;
} // namespace interi::rhi

namespace interi::destruction
{

/// Looks of the faces fracture exposes, by DestructionMaterial::interiorMaterialId (data/destruction/materials.json).
enum class InteriorPattern : std::uint8_t
{
    Concrete, ///< 0: grey cement with embedded aggregate stones.
    Steel,    ///< 1: bright cut steel with rust blotches.
    Wood,     ///< 2: splintered grain (fibres along the texture's x axis).
    Glass,    ///< 3: faintly green, nearly uniform.
    Metal,    ///< 4: sheet metal, brushed.
};

inline constexpr std::uint32_t kInteriorPatternCount = 5;

/// A tileable size x size sRGB albedo of `pattern` (size a power of two, >= 128). Uses only integer arithmetic, so
/// the texels are identical on every platform and compiler.
[[nodiscard]] render::ImageRgba8 MakeInteriorTexture(InteriorPattern pattern, std::uint32_t size, std::uint64_t seed);

/// The interior materials on the GPU: one triplanar-textured Material per InteriorPattern (index = interior id).
struct InteriorMaterials
{
    std::vector<rhi::TextureHandle> textures;
    std::vector<render::Material> materials;
};

/// Generates every pattern (mipmapped, R8G8B8A8Srgb) and records the uploads into `commands`. The uploads need an
/// UploadQueue chunk of at least size * size * 4 bytes. On failure, `commands` must not be submitted (they may
/// reference destroyed textures).
[[nodiscard]] Result<InteriorMaterials> CreateInteriorMaterials(rhi::IDevice& device, rhi::UploadQueue& uploads,
                                                                rhi::ICommandList& commands, std::uint32_t size = 256);

/// Releases the textures (deferred until the GPU is done with them).
void DestroyInteriorMaterials(rhi::ResourceManager& resources, InteriorMaterials& materials);

} // namespace interi::destruction
