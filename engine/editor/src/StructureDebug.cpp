// SPDX-License-Identifier: MIT
#include <interi/destruction/StressSolver.h>
#include <interi/editor/StructureDebug.h>

#include <algorithm>
#include <cmath>

namespace interi::editor
{

namespace
{

/// A chunk's centroid where its piece is now (asset space when the chunk has no pose, e.g. released to debris).
[[nodiscard]] Vec3 ChunkPosition(const World& world, const destruction::DestructionWorld& destruction,
                                 destruction::DestructibleId id, std::uint32_t chunk)
{
    const Vec3 local = destruction.Asset(id).chunks[chunk].centroid;
    if (const auto pose = destruction.ChunkPose(world, id, chunk))
    {
        return pose->TransformPoint(local);
    }
    return destruction.InstanceTransform(id).TransformPoint(local);
}

} // namespace

Vec3 StressOverlayColor(float utilization) noexcept
{
    const float u = std::isnan(utilization) ? 0.0f : std::clamp(utilization, 0.0f, 1.0f);
    const Vec3 green{0.1f, 0.85f, 0.2f};
    const Vec3 yellow{1.0f, 0.9f, 0.1f};
    const Vec3 red{1.0f, 0.1f, 0.05f};
    return u < 0.5f ? Lerp(green, yellow, u * 2.0f) : Lerp(yellow, red, (u - 0.5f) * 2.0f);
}

std::vector<BondLine> StressOverlayLines(const World& world, const destruction::DestructionWorld& destruction,
                                         destruction::DestructibleId id)
{
    std::vector<BondLine> lines;
    const destruction::DestructibleAsset& asset = destruction.Asset(id);
    const destruction::StressSolver& solver = destruction.Stress(id);
    const std::span<const float> health = destruction.BondHealth(id);
    const std::span<const float> full = destruction.BondFullHealth(id);
    for (std::uint32_t bond = 0; bond < asset.bonds.size(); ++bond)
    {
        if (destruction.IsBondBroken(id, bond))
        {
            continue;
        }
        const float fraction = full[bond] > 0.0f ? std::max(health[bond], 0.0f) / full[bond] : 1.0f;
        lines.push_back({.from = ChunkPosition(world, destruction, id, asset.bonds[bond].chunkA),
                         .to = ChunkPosition(world, destruction, id, asset.bonds[bond].chunkB),
                         .utilization = destruction::BondUtilization(solver, bond, fraction)});
    }
    return lines;
}

std::vector<Vec3> AnchorPoints(const World& world, const destruction::DestructionWorld& destruction,
                               destruction::DestructibleId id)
{
    std::vector<Vec3> points;
    for (const std::uint32_t chunk : destruction.Asset(id).anchors)
    {
        points.push_back(ChunkPosition(world, destruction, id, chunk));
    }
    return points;
}

} // namespace interi::editor
