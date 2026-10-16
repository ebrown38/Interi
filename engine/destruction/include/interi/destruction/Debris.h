// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Transform.h>
#include <interi/core/math/Vec.h>

#include <cstdint>
#include <span>
#include <vector>

namespace interi::destruction
{

/// How debris is ranked, simplified and retired. Distances in m, volumes in m^3, times in s. Starting values are
/// proposals; M8.4 baselines (benchmarks/baselines) inform the final ones.
struct DebrisPolicy
{
    /// Priority = sizeWeight x radius - ageWeight x age - distanceWeight x distance. The lowest is evicted first.
    float sizeWeight = 1.0f;
    float ageWeight = 0.02f;
    float distanceWeight = 0.01f;
    /// Pieces up to particleMaxVolume farther than particleDistance from the view become non-colliding particles,
    /// which fall for particleLifetime (fading out over its last fadeDuration) and are then removed.
    float particleDistance = 60.0f;
    float particleMaxVolume = 0.02f;
    float particleLifetime = 4.0f;
    /// Pieces up to fadeMaxVolume start fading fadeDelay after they were adopted and are removed fadeDuration later.
    float fadeMaxVolume = 0.002f;
    float fadeDelay = 8.0f;
    float fadeDuration = 1.5f;
};

/// Higher = more worth keeping.
[[nodiscard]] float DebrisPriority(float radius, float age, float distance, const DebrisPolicy& policy) noexcept;

/// 1 until `delay`, then linear down to 0 at `delay + duration` (0 duration: a step).
[[nodiscard]] float FadeOpacity(float age, float delay, float duration) noexcept;

/// Opacity of a debris body: fading only for pieces up to fadeMaxVolume.
[[nodiscard]] float BodyOpacity(float volume, float age, const DebrisPolicy& policy) noexcept;

/// Opacity of a particle `age` seconds after it became one.
[[nodiscard]] float ParticleOpacity(float age, const DebrisPolicy& policy) noexcept;

/// What to do with a debris body this update.
enum class DebrisAction : std::uint8_t
{
    Keep,
    ToParticle, ///< Far and small: drop the body, keep falling as a particle.
    Remove,     ///< Faded out.
    Settle,     ///< Asleep: a candidate for merging into a rubble pile.
};

struct DebrisSample
{
    float volume = 0.0f;
    float age = 0.0f;
    float distance = 0.0f;
    bool asleep = false;
};

/// Removal (faded out) wins over settling, which wins over particle conversion.
[[nodiscard]] DebrisAction ChooseDebrisAction(const DebrisSample& sample, const DebrisPolicy& policy) noexcept;

/// One semi-implicit Euler step of a free (non-colliding) particle.
void IntegrateParticle(Transform& pose, Vec3& linearVelocity, const Vec3& angularVelocity, const Vec3& gravity,
                       float deltaSeconds) noexcept;

/// True unless the sphere lies entirely outside one of the planes. A plane (n, w) keeps the points p with
/// Dot(n, p) + w >= 0 (n unit length). No planes: always visible.
[[nodiscard]] bool SphereVisible(const Vec3& center, float radius, std::span<const Vec4> planes) noexcept;

/// Groups the points by the grid cell (edge `cellSize`) they fall in and returns the groups of at least `minCount`
/// points: each ascending, the groups ordered by their smallest index. Independent of anything but the input.
[[nodiscard]] std::vector<std::vector<std::uint32_t>> ClusterByGrid(std::span<const Vec3> points, float cellSize,
                                                                    std::uint32_t minCount);

} // namespace interi::destruction
