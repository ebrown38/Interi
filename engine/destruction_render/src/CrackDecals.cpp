// SPDX-License-Identifier: MIT
#include <interi/core/Hash.h>
#include <interi/core/math/Transform.h>
#include <interi/destruction_render/CrackDecals.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/World.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace interi::destruction
{

float CrackOverlayIntensity(float health, float fullHealth) noexcept
{
    if (!(fullHealth > 0.0f))
    {
        return 0.0f;
    }
    const float lost = std::clamp(1.0f - health / fullHealth, 0.0f, 1.0f);
    const float t = std::clamp((lost - 0.1f) / 0.8f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

void GatherCrackDecals(World& world, const DestructionWorld& destruction, std::vector<render::Decal>& out,
                       const CrackDecalSettings& settings)
{
    constexpr std::uint32_t kNone = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> pieceOf;
    std::vector<Transform> poses;
    for (const DestructibleId id : destruction.Instances())
    {
        const DestructibleAsset& asset = destruction.Asset(id);
        const std::span<const float> health = destruction.BondHealth(id);
        const std::span<const float> fullHealth = destruction.BondFullHealth(id);
        pieceOf.assign(asset.chunks.size(), kNone);
        poses.assign(destruction.PieceCount(id), destruction.InstanceTransform(id));
        for (std::uint32_t piece = 0; piece < destruction.PieceCount(id); ++piece)
        {
            for (const std::uint32_t chunk : destruction.PieceChunks(id, piece))
            {
                pieceOf[chunk] = piece;
            }
            const Entity entity = destruction.PieceEntity(id, piece);
            if (const Transform* transform = entity.IsValid() ? world.Get<Transform>(entity) : nullptr)
            {
                poses[piece] = *transform;
            }
        }
        for (std::uint32_t bond = 0; bond < asset.bonds.size(); ++bond)
        {
            const Bond& b = asset.bonds[bond];
            const std::uint32_t piece = pieceOf[b.chunkA];
            if (piece == kNone || pieceOf[b.chunkB] != piece)
            {
                continue; // separated, or no longer part of the instance
            }
            const float intensity =
                destruction.IsBondBroken(id, bond) ? 1.0f : CrackOverlayIntensity(health[bond], fullHealth[bond]);
            if (intensity < settings.minIntensity)
            {
                continue;
            }
            const Transform& pose = poses[piece];
            const float size = std::sqrt(b.area);
            out.push_back(
                {.transform = render::DecalBox(pose.TransformPoint(b.centroid), pose.TransformVector(b.normal),
                                               size * settings.across, size * settings.along),
                 .color = settings.color,
                 .intensity = intensity,
                 .kind = render::DecalKind::Crack,
                 .seed = static_cast<std::uint32_t>(HashCombine(id.index, bond))});
        }
    }
}

} // namespace interi::destruction
