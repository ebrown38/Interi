// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{

struct DestructibleAsset;

/// Damage is energy (J) taken from bond health. A bond's full health is the energy needed to open its crack:
/// the weaker material's fractureEnergy (J/m^2) times its area, so tough materials (steel) resist far more than
/// brittle ones (concrete, glass) without per-material tuning.
[[nodiscard]] std::vector<float> InitialBondHealth(const DestructibleAsset& asset);

/// Damage that falls off with distance: full `energy` up to `innerRadius`, then inverse-square
/// (energy * (innerRadius / d)^2), and none from `radius` on.
struct RadialDamage
{
    Vec3 center;
    float energy = 0.0f;      ///< J dealt to each bond within innerRadius.
    float innerRadius = 0.5f; ///< m (> 0).
    float radius = 5.0f;      ///< m.
};

/// Fraction of a radial damage's energy dealt at `distance`.
[[nodiscard]] float RadialFalloff(float distance, float innerRadius, float radius) noexcept;

/// Tuning of the damage sources. Every value scales energy dealt to bond health.
struct DamageSettings
{
    float explosionScale = 1.0f;
    float meleeScale = 1.0f;
    /// Contact impulses (N·s) up to this deal no damage (resting contact, light bumps).
    float impactThreshold = 2000.0f;
    /// Energy per N·s of contact impulse above the threshold (J / N·s): an impulse J of a body with speed v carries
    /// about J * v / 2 joules, so this is half a typical impact speed.
    float impactEnergyPerImpulse = 2.0f;
    float impactInnerRadius = 0.25f; ///< m.
    float impactRadius = 1.0f;       ///< m.
};

/// The radial damage of a contact impulse at `point`, or energy 0 when the impulse is below the threshold.
[[nodiscard]] RadialDamage ImpactDamage(const Vec3& point, float impulse, const DamageSettings& settings) noexcept;

/// Deals `damage` (energy times `scale`) to every bond within its radius: health[i] decreases (to no less than 0)
/// by the falloff of the distance to bondCentroids[i]. Indices of bonds whose health reached 0 in this call are
/// appended to `broken` in ascending order. Bonds already at 0 are skipped.
void ApplyRadialDamage(std::span<const Vec3> bondCentroids, const RadialDamage& damage, float scale,
                       std::span<float> health, std::vector<std::uint32_t>& broken);

} // namespace interi::destruction
