// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/math/Vec.h>

namespace interi::audio
{

/// Where sound is heard from. `forward` and `up` are unit and perpendicular; right = forward x up.
struct Listener
{
    Vec3 position;
    Vec3 forward{0.0f, 0.0f, -1.0f};
    Vec3 up{0.0f, 1.0f, 0.0f};
};

/// Distance attenuation: inverse distance, full volume up to `minDistance`, silent from `maxDistance` on:
///
///     inv(d)  = minDistance / (minDistance + rolloff * (d - minDistance))
///     gain(d) = (inv(d) - inv(maxDistance)) / (1 - inv(maxDistance))      for minDistance <= d <= maxDistance
///
/// The offset makes the curve reach zero at maxDistance without a step, so a voice can be culled there silently.
struct AttenuationCurve
{
    float minDistance = 1.0f;   ///< m (> 0).
    float maxDistance = 100.0f; ///< m (> minDistance).
    float rolloff = 1.0f;       ///< >= 0; 0 = no attenuation inside maxDistance.
};

[[nodiscard]] float Attenuation(float distance, const AttenuationCurve& curve) noexcept;

/// Per-channel gains of a stereo voice.
struct StereoGains
{
    float left = 0.0f;
    float right = 0.0f;

    [[nodiscard]] float Max() const noexcept { return left > right ? left : right; }
};

/// Equal-power panning: pan p in [-1, 1] (left to right) gives left = cos((p + 1) pi / 4), right = sin(...), so
/// left^2 + right^2 = 1 and the centre is 1/sqrt(2) per channel.
[[nodiscard]] StereoGains EqualPowerPan(float pan) noexcept;

/// Gains of a point source at `source` with `volume`: attenuation times equal-power pan, where the pan is the
/// source direction's component along the listener's right. A source at the listener is centred.
[[nodiscard]] StereoGains Spatialize(const Listener& listener, const Vec3& source, const AttenuationCurve& curve,
                                     float volume) noexcept;

} // namespace interi::audio
