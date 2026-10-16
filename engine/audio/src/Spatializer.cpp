// SPDX-License-Identifier: MIT
#include <interi/audio/Spatializer.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace interi::audio
{

float Attenuation(float distance, const AttenuationCurve& curve) noexcept
{
    if (!(distance < curve.maxDistance))
    {
        return 0.0f;
    }
    const float min = curve.minDistance;
    const auto inverse = [&](float d) { return min / (min + curve.rolloff * (std::max(d, min) - min)); };
    const float floor = inverse(curve.maxDistance);
    if (!(floor < 1.0f))
    {
        return 1.0f; // rolloff 0: flat until maxDistance
    }
    return std::clamp((inverse(distance) - floor) / (1.0f - floor), 0.0f, 1.0f);
}

StereoGains EqualPowerPan(float pan) noexcept
{
    const float angle = (std::clamp(pan, -1.0f, 1.0f) + 1.0f) * 0.25f * std::numbers::pi_v<float>;
    return {.left = std::cos(angle), .right = std::sin(angle)};
}

StereoGains Spatialize(const Listener& listener, const Vec3& source, const AttenuationCurve& curve,
                       float volume) noexcept
{
    const Vec3 offset = source - listener.position;
    const float distance = Length(offset);
    const float gain = volume * Attenuation(distance, curve);
    float pan = 0.0f;
    if (distance > 1e-4f)
    {
        pan = Dot(offset / distance, Cross(listener.forward, listener.up));
    }
    const StereoGains panned = EqualPowerPan(pan);
    return {.left = gain * panned.left, .right = gain * panned.right};
}

} // namespace interi::audio
