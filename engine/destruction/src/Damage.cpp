// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/destruction/Damage.h>
#include <interi/destruction/DestructibleAsset.h>

#include <algorithm>
#include <cmath>

namespace interi::destruction
{

std::vector<float> InitialBondHealth(const DestructibleAsset& asset)
{
    std::vector<float> health;
    health.reserve(asset.bonds.size());
    for (const Bond& bond : asset.bonds)
    {
        const DestructionMaterial& a = asset.materials[asset.chunks[bond.chunkA].material];
        const DestructionMaterial& b = asset.materials[asset.chunks[bond.chunkB].material];
        health.push_back(std::min(a.fractureEnergy, b.fractureEnergy) * bond.area);
    }
    return health;
}

float RadialFalloff(float distance, float innerRadius, float radius) noexcept
{
    if (distance >= radius)
    {
        return 0.0f;
    }
    if (distance <= innerRadius)
    {
        return 1.0f;
    }
    const float ratio = innerRadius / distance;
    return ratio * ratio;
}

RadialDamage ImpactDamage(const Vec3& point, float impulse, const DamageSettings& settings) noexcept
{
    const float excess = std::max(impulse - settings.impactThreshold, 0.0f);
    return {.center = point,
            .energy = excess * settings.impactEnergyPerImpulse,
            .innerRadius = settings.impactInnerRadius,
            .radius = settings.impactRadius};
}

void ApplyRadialDamage(std::span<const Vec3> bondCentroids, const RadialDamage& damage, float scale,
                       std::span<float> health, std::vector<std::uint32_t>& broken)
{
    INTERI_ASSERT(bondCentroids.size() == health.size());
    const float energy = damage.energy * scale;
    if (!(energy > 0.0f))
    {
        return;
    }
    const float radiusSquared = damage.radius * damage.radius;
    for (std::uint32_t i = 0; i < health.size(); ++i)
    {
        if (health[i] <= 0.0f)
        {
            continue;
        }
        const float distanceSquared = LengthSquared(bondCentroids[i] - damage.center);
        if (distanceSquared >= radiusSquared)
        {
            continue;
        }
        health[i] = std::max(
            health[i] - energy * RadialFalloff(std::sqrt(distanceSquared), damage.innerRadius, damage.radius), 0.0f);
        if (health[i] <= 0.0f)
        {
            broken.push_back(i);
        }
    }
}

} // namespace interi::destruction
