// SPDX-License-Identifier: MIT
#include <interi/core/math/Transform.h>
#include <interi/runtime/FixedStepClock.h>
#include <interi/runtime/Interpolation.h>
#include <interi/testing/ExpectNear.h>

#include <gtest/gtest.h>

#include <chrono>
#include <numbers>

using interi::FixedStepClock;
using interi::FixedStepConfig;
using namespace std::chrono_literals;

TEST(FixedStepClock, DefaultStep_Is60Hz)
{
    const FixedStepClock clock;
    EXPECT_EQ(clock.Step(), std::chrono::nanoseconds(16'666'667));
    EXPECT_NEAR(clock.StepSeconds(), 1.0 / 60.0, 1e-9);
}

TEST(FixedStepClock, ExactFrames_OneStepEach)
{
    FixedStepClock clock;
    for (int frame = 0; frame < 600; ++frame)
    {
        EXPECT_EQ(clock.Advance(clock.Step()), 1u);
    }
    EXPECT_EQ(clock.TotalSteps(), 600u);
    EXPECT_EQ(clock.Alpha(), 0.0);
}

TEST(FixedStepClock, VariableFrames_AccumulateRemainder)
{
    FixedStepClock clock;
    // 144 Hz rendering: 6.944 ms frames → steps on roughly every 2–3 frames, no time lost.
    std::uint64_t steps = 0;
    const auto frame = std::chrono::nanoseconds(6'944'444);
    for (int i = 0; i < 144; ++i)
    {
        steps += clock.Advance(frame);
        EXPECT_GE(clock.Alpha(), 0.0);
        EXPECT_LT(clock.Alpha(), 1.0);
    }
    EXPECT_EQ(steps, 59u); // 144 * 6.944444 ms = 999.99994 ms, just under 60 steps
    EXPECT_EQ(clock.Advance(1ms), 1u);
}

TEST(FixedStepClock, LongFrame_ClampsSubsteps)
{
    FixedStepClock clock(FixedStepConfig{.step = 10ms, .maxSubsteps = 4});
    EXPECT_EQ(clock.Advance(1s), 4u); // a 1 s hitch would need 100 steps
    EXPECT_EQ(clock.DroppedTime(), 960ms);
    EXPECT_LT(clock.Alpha(), 1.0); // the backlog was dropped, not carried
    EXPECT_EQ(clock.Advance(10ms), 1u);
}

TEST(FixedStepClock, Alpha_IsFractionOfStep)
{
    FixedStepClock clock(FixedStepConfig{.step = 10ms, .maxSubsteps = 8});
    EXPECT_EQ(clock.Advance(25ms), 2u);
    EXPECT_DOUBLE_EQ(clock.Alpha(), 0.5);
}

TEST(Interpolator, Alpha_BlendsTransforms)
{
    interi::Transform previous;
    previous.position = interi::Vec3{0, 0, 0};
    previous.rotation = interi::Quat::Identity();
    previous.scale = interi::Vec3{1.0f};
    interi::Transform current;
    current.position = interi::Vec3{10, -4, 2};
    current.rotation = interi::Quat::FromAxisAngle(interi::Vec3{0, 1, 0}, std::numbers::pi_v<float> / 2);
    current.scale = interi::Vec3{3.0f};

    const interi::Transform start = interi::Interpolate(previous, current, 0.0f);
    INTERI_EXPECT_VEC_NEAR(start.position, previous.position, 1e-6f);
    INTERI_EXPECT_QUAT_NEAR(start.rotation, previous.rotation, 1e-6f);

    const interi::Transform end = interi::Interpolate(previous, current, 1.0f);
    INTERI_EXPECT_VEC_NEAR(end.position, current.position, 1e-6f);
    INTERI_EXPECT_QUAT_NEAR(end.rotation, current.rotation, 1e-6f);
    INTERI_EXPECT_VEC_NEAR(end.scale, current.scale, 1e-6f);

    const interi::Transform mid = interi::Interpolate(previous, current, 0.25f);
    INTERI_EXPECT_VEC_NEAR(mid.position, (interi::Vec3{2.5f, -1.0f, 0.5f}), 1e-5f);
    INTERI_EXPECT_VEC_NEAR(mid.scale, interi::Vec3{1.5f}, 1e-5f);
    // Slerp: a quarter of the 90° turn.
    INTERI_EXPECT_QUAT_NEAR(mid.rotation,
                            interi::Quat::FromAxisAngle(interi::Vec3{0, 1, 0}, std::numbers::pi_v<float> / 8), 1e-5f);
}
