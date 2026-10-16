// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/math/Quat.h>
#include <interi/destruction/Debris.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace interi::destruction
{

float DebrisPriority(float radius, float age, float distance, const DebrisPolicy& policy) noexcept
{
    return policy.sizeWeight * radius - policy.ageWeight * age - policy.distanceWeight * distance;
}

float FadeOpacity(float age, float delay, float duration) noexcept
{
    if (age < delay)
    {
        return 1.0f;
    }
    if (!(duration > 0.0f))
    {
        return 0.0f;
    }
    return std::clamp(1.0f - (age - delay) / duration, 0.0f, 1.0f);
}

float BodyOpacity(float volume, float age, const DebrisPolicy& policy) noexcept
{
    return volume <= policy.fadeMaxVolume ? FadeOpacity(age, policy.fadeDelay, policy.fadeDuration) : 1.0f;
}

float ParticleOpacity(float age, const DebrisPolicy& policy) noexcept
{
    const float duration = std::min(policy.fadeDuration, policy.particleLifetime);
    return FadeOpacity(age, policy.particleLifetime - duration, duration);
}

DebrisAction ChooseDebrisAction(const DebrisSample& sample, const DebrisPolicy& policy) noexcept
{
    if (!(BodyOpacity(sample.volume, sample.age, policy) > 0.0f))
    {
        return DebrisAction::Remove;
    }
    if (sample.asleep)
    {
        return DebrisAction::Settle;
    }
    if (sample.distance > policy.particleDistance && sample.volume <= policy.particleMaxVolume)
    {
        return DebrisAction::ToParticle;
    }
    return DebrisAction::Keep;
}

void IntegrateParticle(Transform& pose, Vec3& linearVelocity, const Vec3& angularVelocity, const Vec3& gravity,
                       float deltaSeconds) noexcept
{
    linearVelocity += gravity * deltaSeconds;
    pose.position += linearVelocity * deltaSeconds;
    // dq/dt = 0.5 (w, 0) q
    const Quat spin{angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.0f};
    const Quat delta = spin * pose.rotation;
    const float half = 0.5f * deltaSeconds;
    pose.rotation = Normalize(Quat{pose.rotation.x + delta.x * half, pose.rotation.y + delta.y * half,
                                   pose.rotation.z + delta.z * half, pose.rotation.w + delta.w * half});
}

bool SphereVisible(const Vec3& center, float radius, std::span<const Vec4> planes) noexcept
{
    return std::ranges::all_of(planes,
                               [&](const Vec4& plane) { return Dot(plane.XYZ(), center) + plane.w >= -radius; });
}

std::vector<std::vector<std::uint32_t>> ClusterByGrid(std::span<const Vec3> points, float cellSize,
                                                      std::uint32_t minCount)
{
    INTERI_ASSERT(cellSize > 0.0f);
    using Key = std::array<std::int64_t, 3>;
    std::vector<std::pair<Key, std::uint32_t>> keyed;
    keyed.reserve(points.size());
    for (std::uint32_t i = 0; i < points.size(); ++i)
    {
        const auto cell = [&](float v) { return static_cast<std::int64_t>(std::floor(v / cellSize)); };
        keyed.emplace_back(Key{cell(points[i].x), cell(points[i].y), cell(points[i].z)}, i);
    }
    std::ranges::sort(keyed); // by cell, then index
    std::vector<std::vector<std::uint32_t>> clusters;
    for (std::size_t begin = 0; begin < keyed.size();)
    {
        std::size_t end = begin + 1;
        while (end < keyed.size() && keyed[end].first == keyed[begin].first)
        {
            ++end;
        }
        if (end - begin >= minCount)
        {
            std::vector<std::uint32_t>& cluster = clusters.emplace_back();
            for (std::size_t i = begin; i < end; ++i)
            {
                cluster.push_back(keyed[i].second);
            }
        }
        begin = end;
    }
    std::ranges::sort(clusters, {}, [](const std::vector<std::uint32_t>& cluster) { return cluster.front(); });
    return clusters;
}

} // namespace interi::destruction
