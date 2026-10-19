// SPDX-License-Identifier: MIT
#pragma once

// A software-rendered preview sphere for PBR materials: GGX specular and Lambert diffuse under one directional light
// and a sky/ground ambient, tone mapped and sRGB encoded. CPU-only, so the material editor, the asset browser's
// thumbnails and the tests all use the same code. Normal maps are not previewed.

#include <interi/assets/Material.h>
#include <interi/assets/Texture.h>
#include <interi/core/math/Vec.h>

#include <cstdint>
#include <functional>

namespace interi::editor
{

/// Linear RGBA of a material texture at (u, v), both in [0, 1] with v down; the preview multiplies it into the factor.
/// Return white for a texture that can't be read.
using TextureSampler = std::function<Vec4(const assets::TextureRef& texture, float u, float v)>;

struct MaterialPreviewSettings
{
    Vec3 toLight{-0.45f, 0.65f, 0.6f}; ///< Direction to the light, camera space (+Z towards the viewer).
    Vec3 lightColor{3.0f, 3.0f, 3.0f};
};

/// A `size` x `size` image of the material on a sphere: transparent (alpha 0) outside it; inside, alpha is 255, or the
/// material's alpha for Blend materials. `size` of 0 gives an empty image.
[[nodiscard]] assets::Image8 RenderMaterialPreview(const assets::PbrMaterial& material, std::uint32_t size,
                                                   const TextureSampler& sampler = {},
                                                   const MaterialPreviewSettings& settings = {});

} // namespace interi::editor
