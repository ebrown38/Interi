// SPDX-License-Identifier: MIT
#include <interi/core/math/Quat.h>
#include <interi/destruction/Debris.h>

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::destruction;

TEST(DebrisPriority, LargerNewerCloser_RanksHigher)
{
    const DebrisPolicy policy;
    const float base = DebrisPriority(0.5f, 1.0f, 10.0f, policy);
    EXPECT_GT(DebrisPriority(1.0f, 1.0f, 10.0f, policy), base);
    EXPECT_LT(DebrisPriority(0.5f, 20.0f, 10.0f, policy), base);
    EXPECT_LT(DebrisPriority(0.5f, 1.0f, 50.0f, policy), base);
}

TEST(DebrisPriority, ZeroWeights_IgnoreThatCriterion)
{
    DebrisPolicy policy;
    policy.ageWeight = 0.0f;
    policy.distanceWeight = 0.0f;
    EXPECT_EQ(DebrisPriority(0.5f, 1.0f, 1.0f, policy), DebrisPriority(0.5f, 100.0f, 500.0f, policy));
}

TEST(FadeOpacity, BeforeDelayOpaque_ThenLinearToZero)
{
    EXPECT_EQ(FadeOpacity(0.0f, 2.0f, 1.0f), 1.0f);
    EXPECT_EQ(FadeOpacity(1.99f, 2.0f, 1.0f), 1.0f);
    EXPECT_NEAR(FadeOpacity(2.5f, 2.0f, 1.0f), 0.5f, 1e-6f);
    EXPECT_EQ(FadeOpacity(3.0f, 2.0f, 1.0f), 0.0f);
    EXPECT_EQ(FadeOpacity(10.0f, 2.0f, 1.0f), 0.0f);
    EXPECT_EQ(FadeOpacity(2.0f, 2.0f, 0.0f), 0.0f);
}

TEST(BodyOpacity, LargePieces_NeverFade)
{
    const DebrisPolicy policy;
    EXPECT_EQ(BodyOpacity(policy.fadeMaxVolume * 2.0f, 1000.0f, policy), 1.0f);
    EXPECT_EQ(BodyOpacity(policy.fadeMaxVolume, 1000.0f, policy), 0.0f);
}

TEST(ParticleOpacity, FadesOverLifetimeEnd)
{
    DebrisPolicy policy;
    policy.particleLifetime = 4.0f;
    policy.fadeDuration = 1.0f;
    EXPECT_EQ(ParticleOpacity(2.9f, policy), 1.0f);
    EXPECT_NEAR(ParticleOpacity(3.5f, policy), 0.5f, 1e-6f);
    EXPECT_EQ(ParticleOpacity(4.0f, policy), 0.0f);
}

TEST(DebrisAction, BeyondDistanceSmall_ToParticle)
{
    const DebrisPolicy policy;
    DebrisSample sample{.volume = policy.particleMaxVolume, .age = 0.0f, .distance = policy.particleDistance + 1.0f};
    EXPECT_EQ(ChooseDebrisAction(sample, policy), DebrisAction::ToParticle);
    sample.distance = policy.particleDistance - 1.0f;
    EXPECT_EQ(ChooseDebrisAction(sample, policy), DebrisAction::Keep);
    sample.distance = policy.particleDistance + 1.0f;
    sample.volume = policy.particleMaxVolume * 2.0f;
    EXPECT_EQ(ChooseDebrisAction(sample, policy), DebrisAction::Keep);
}

TEST(DebrisAction, Asleep_SettlesBeforeParticle)
{
    const DebrisPolicy policy;
    const DebrisSample sample{
        .volume = policy.particleMaxVolume, .age = 0.0f, .distance = policy.particleDistance + 1.0f, .asleep = true};
    EXPECT_EQ(ChooseDebrisAction(sample, policy), DebrisAction::Settle);
}

TEST(DebrisAction, FadedOut_RemovedEvenIfAsleep)
{
    const DebrisPolicy policy;
    const DebrisSample sample{.volume = policy.fadeMaxVolume,
                              .age = policy.fadeDelay + policy.fadeDuration,
                              .distance = 0.0f,
                              .asleep = true};
    EXPECT_EQ(ChooseDebrisAction(sample, policy), DebrisAction::Remove);
}

TEST(IntegrateParticle, Gravity_MatchesSemiImplicitEuler)
{
    Transform pose;
    Vec3 velocity(1.0f, 0.0f, 0.0f);
    const Vec3 gravity(0.0f, -10.0f, 0.0f);
    IntegrateParticle(pose, velocity, Vec3(), gravity, 0.1f);
    EXPECT_NEAR(velocity.y, -1.0f, 1e-6f);
    EXPECT_NEAR(pose.position.x, 0.1f, 1e-6f);
    EXPECT_NEAR(pose.position.y, -0.1f, 1e-6f);
}

TEST(IntegrateParticle, Spin_RotatesAboutAxis)
{
    Transform pose;
    Vec3 velocity;
    const Vec3 spin(0.0f, std::numbers::pi_v<float>, 0.0f); // half a turn per second about +Y
    for (int i = 0; i < 1000; ++i)
    {
        IntegrateParticle(pose, velocity, spin, Vec3(), 0.0005f);
    }
    const Vec3 x = Rotate(pose.rotation, Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_NEAR(x.x, 0.0f, 1e-2f); // a quarter turn: +X -> -Z
    EXPECT_NEAR(x.z, -1.0f, 1e-2f);
}

TEST(SphereVisible, OutsideOnePlane_Culled)
{
    const std::array<Vec4, 1> planes{Vec4(0.0f, 0.0f, 1.0f, 0.0f)}; // keeps z >= 0
    EXPECT_TRUE(SphereVisible(Vec3(0.0f, 0.0f, 1.0f), 0.5f, planes));
    EXPECT_TRUE(SphereVisible(Vec3(0.0f, 0.0f, -0.4f), 0.5f, planes));
    EXPECT_FALSE(SphereVisible(Vec3(0.0f, 0.0f, -0.6f), 0.5f, planes));
    EXPECT_TRUE(SphereVisible(Vec3(0.0f, 0.0f, -100.0f), 0.5f, {}));
}

TEST(ClusterByGrid, SameCell_GroupedAboveMinCount)
{
    const std::vector<Vec3> points{{0.1f, 0.1f, 0.1f}, {5.5f, 0.2f, 0.0f},  {0.9f, 0.5f, 0.3f},
                                   {5.1f, 0.9f, 0.9f}, {-0.5f, 0.0f, 0.0f}, {0.5f, 0.5f, 0.5f}};
    const auto clusters = ClusterByGrid(points, 1.0f, 2);
    ASSERT_EQ(clusters.size(), 2u);
    EXPECT_EQ(clusters[0], (std::vector<std::uint32_t>{0, 2, 5}));
    EXPECT_EQ(clusters[1], (std::vector<std::uint32_t>{1, 3}));
    EXPECT_EQ(ClusterByGrid(points, 1.0f, 3).size(), 1u);
}

TEST(ClusterByGrid, InputOrderPermuted_SameClustersOfSamePoints)
{
    const std::vector<Vec3> a{{0.1f, 0.0f, 0.0f}, {3.2f, 0.0f, 0.0f}, {0.2f, 0.0f, 0.0f}, {3.1f, 0.0f, 0.0f}};
    const std::vector<Vec3> b{a[3], a[2], a[1], a[0]};
    const auto ca = ClusterByGrid(a, 1.0f, 2);
    const auto cb = ClusterByGrid(b, 1.0f, 2);
    ASSERT_EQ(ca.size(), 2u);
    ASSERT_EQ(cb.size(), 2u);
    EXPECT_EQ(cb[0], (std::vector<std::uint32_t>{0, 2})); // b[0], b[2] = a[3], a[1]
}

} // namespace
