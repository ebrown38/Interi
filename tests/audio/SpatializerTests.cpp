// SPDX-License-Identifier: MIT
#include <interi/audio/Spatializer.h>

#include <gtest/gtest.h>

#include <cmath>

using interi::Vec3;
using interi::audio::Attenuation;
using interi::audio::AttenuationCurve;
using interi::audio::EqualPowerPan;
using interi::audio::Listener;
using interi::audio::Spatialize;
using interi::audio::StereoGains;

TEST(Spatializer, Attenuation_MatchesCurve)
{
    const AttenuationCurve curve{.minDistance = 2.0f, .maxDistance = 50.0f, .rolloff = 1.5f};
    const auto inverse = [&](float d)
    {
        return curve.minDistance /
               (curve.minDistance + curve.rolloff * (std::max(d, curve.minDistance) - curve.minDistance));
    };
    const float floor = inverse(curve.maxDistance);
    for (const float d : {0.0f, 1.0f, 2.0f, 3.0f, 5.0f, 10.0f, 20.0f, 35.0f, 49.0f})
    {
        const float expected = (inverse(d) - floor) / (1.0f - floor);
        EXPECT_NEAR(Attenuation(d, curve), expected, 1e-6f) << d;
    }
    EXPECT_EQ(Attenuation(0.5f, curve), 1.0f);  // full volume inside minDistance
    EXPECT_EQ(Attenuation(50.0f, curve), 0.0f); // silent from maxDistance on
    EXPECT_EQ(Attenuation(500.0f, curve), 0.0f);
    EXPECT_NEAR(Attenuation(49.999f, curve), 0.0f, 1e-4f); // continuous at maxDistance: no step to cull at
    // Monotonic.
    float previous = 1.0f;
    for (float d = 0.0f; d < 50.0f; d += 0.25f)
    {
        const float gain = Attenuation(d, curve);
        EXPECT_LE(gain, previous);
        previous = gain;
    }
}

TEST(Spatializer, ZeroRolloff_FlatUntilMax)
{
    const AttenuationCurve curve{.minDistance = 1.0f, .maxDistance = 10.0f, .rolloff = 0.0f};
    EXPECT_EQ(Attenuation(5.0f, curve), 1.0f);
    EXPECT_EQ(Attenuation(10.0f, curve), 0.0f);
}

TEST(Spatializer, EqualPowerPan_ConstantPower)
{
    for (const float pan : {-1.0f, -0.5f, 0.0f, 0.3f, 1.0f})
    {
        const StereoGains gains = EqualPowerPan(pan);
        EXPECT_NEAR(gains.left * gains.left + gains.right * gains.right, 1.0f, 1e-6f) << pan;
    }
    EXPECT_NEAR(EqualPowerPan(-1.0f).right, 0.0f, 1e-6f);
    EXPECT_NEAR(EqualPowerPan(1.0f).left, 0.0f, 1e-6f);
    EXPECT_NEAR(EqualPowerPan(0.0f).left, EqualPowerPan(0.0f).right, 1e-6f);
}

TEST(Spatializer, Pan_LeftRight)
{
    Listener listener; // looking along -Z, so +X is right
    const AttenuationCurve curve{.minDistance = 10.0f, .maxDistance = 100.0f};
    const StereoGains right = Spatialize(listener, Vec3(5.0f, 0.0f, 0.0f), curve, 1.0f);
    const StereoGains left = Spatialize(listener, Vec3(-5.0f, 0.0f, 0.0f), curve, 1.0f);
    const StereoGains ahead = Spatialize(listener, Vec3(0.0f, 0.0f, -5.0f), curve, 1.0f);
    EXPECT_NEAR(right.right, 1.0f, 1e-5f);
    EXPECT_NEAR(right.left, 0.0f, 1e-5f);
    EXPECT_NEAR(left.left, 1.0f, 1e-5f);
    EXPECT_NEAR(ahead.left, ahead.right, 1e-6f);

    // Turning the listener around swaps the sides.
    listener.forward = Vec3(0.0f, 0.0f, 1.0f);
    const StereoGains turned = Spatialize(listener, Vec3(5.0f, 0.0f, 0.0f), curve, 1.0f);
    EXPECT_NEAR(turned.left, 1.0f, 1e-5f);
}

TEST(Spatializer, Spatialize_AttenuatesAndScalesVolume)
{
    Listener listener;
    listener.position = Vec3(10.0f, 0.0f, 0.0f);
    const AttenuationCurve curve{.minDistance = 1.0f, .maxDistance = 40.0f};
    const StereoGains near = Spatialize(listener, Vec3(10.0f, 0.0f, -1.0f), curve, 0.5f);
    const StereoGains far = Spatialize(listener, Vec3(10.0f, 0.0f, -20.0f), curve, 0.5f);
    EXPECT_NEAR(near.left, 0.5f * std::sqrt(0.5f), 1e-5f);
    EXPECT_NEAR(far.left, 0.5f * std::sqrt(0.5f) * Attenuation(20.0f, curve), 1e-5f);
    const StereoGains gone = Spatialize(listener, Vec3(10.0f, 0.0f, -45.0f), curve, 1.0f);
    EXPECT_EQ(gone.Max(), 0.0f);
    const StereoGains here = Spatialize(listener, listener.position, curve, 1.0f);
    EXPECT_NEAR(here.left, here.right, 1e-6f);
}
