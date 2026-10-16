// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>
#include <interi/render/Decals.h>

#include <vector>

namespace interi
{
class World;
}

namespace interi::destruction
{

class DestructionWorld;

/// How visible the crack of a bond is, from its health: nothing while it has lost at most 10% of its full health,
/// rising smoothly to 1 at 90% lost. 0 when fullHealth <= 0.
[[nodiscard]] float CrackOverlayIntensity(float health, float fullHealth) noexcept;

struct CrackDecalSettings
{
    float minIntensity = 0.02f; ///< Fainter cracks are not drawn.
    float across = 1.6f;        ///< Decal extent across the bond, times sqrt(bond area).
    float along = 0.6f;         ///< Decal thickness along the bond normal, times sqrt(bond area).
    Vec3 color{0.04f, 0.035f, 0.03f};
};

/// Appends a crack decal for every damaged bond of every instance whose two chunks still move together (one
/// piece). A broken bond inside a piece shows as a full crack; one between two pieces is not drawn (the chunks
/// separated, and their fracture faces show instead). Each decal is centred on the bond in world space with local z
/// along its normal, so the crack line appears where the bond's face meets the surface.
void GatherCrackDecals(World& world, const DestructionWorld& destruction, std::vector<render::Decal>& out,
                       const CrackDecalSettings& settings = {});

} // namespace interi::destruction
