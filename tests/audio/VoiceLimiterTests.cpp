// SPDX-License-Identifier: MIT
#include <interi/audio/VoiceLimiter.h>

#include <gtest/gtest.h>

using interi::audio::VoiceAdmission;
using interi::audio::VoiceLimiter;

TEST(VoiceLimiter, UnderCap_UsesFreeSlots)
{
    VoiceLimiter limiter(3);
    for (std::uint32_t i = 0; i < 3; ++i)
    {
        const VoiceAdmission admission = limiter.Acquire(1.0f);
        ASSERT_TRUE(admission.slot.has_value());
        EXPECT_EQ(*admission.slot, i);
        EXPECT_FALSE(admission.evicted.has_value());
    }
    EXPECT_EQ(limiter.LiveCount(), 3u);
    limiter.Release(1);
    EXPECT_EQ(limiter.LiveCount(), 2u);
    const VoiceAdmission reuse = limiter.Acquire(0.1f);
    EXPECT_EQ(reuse.slot, 1u);
    EXPECT_FALSE(reuse.evicted.has_value());
}

TEST(VoiceLimiter, OverCap_DropsLowestPriority)
{
    VoiceLimiter limiter(4);
    for (const float priority : {0.5f, 0.2f, 0.9f, 0.4f})
    {
        ASSERT_TRUE(limiter.Acquire(priority).slot.has_value());
    }
    const VoiceAdmission admission = limiter.Acquire(0.3f);
    ASSERT_TRUE(admission.slot.has_value());
    EXPECT_EQ(admission.evicted, 1u); // 0.2 was the lowest
    EXPECT_EQ(*admission.slot, 1u);
    EXPECT_EQ(limiter.Priority(1), 0.3f);
    EXPECT_EQ(limiter.LiveCount(), 4u);

    const VoiceAdmission next = limiter.Acquire(1.0f);
    EXPECT_EQ(next.evicted, 1u); // now 0.3 is the lowest
}

TEST(VoiceLimiter, NewcomerLowest_Rejected)
{
    VoiceLimiter limiter(2);
    ASSERT_TRUE(limiter.Acquire(0.5f).slot);
    ASSERT_TRUE(limiter.Acquire(0.6f).slot);
    const VoiceAdmission lower = limiter.Acquire(0.1f);
    EXPECT_FALSE(lower.slot.has_value());
    EXPECT_FALSE(lower.evicted.has_value());
    const VoiceAdmission equal = limiter.Acquire(0.5f); // ties do not churn
    EXPECT_FALSE(equal.slot.has_value());
    EXPECT_EQ(limiter.Priority(0), 0.5f);
}

TEST(VoiceLimiter, EqualLowest_EvictsOldest)
{
    VoiceLimiter limiter(3);
    ASSERT_TRUE(limiter.Acquire(0.5f).slot);
    ASSERT_TRUE(limiter.Acquire(0.5f).slot);
    ASSERT_TRUE(limiter.Acquire(0.5f).slot);
    EXPECT_EQ(limiter.Acquire(0.8f).evicted, 0u);
    EXPECT_EQ(limiter.Acquire(0.8f).evicted, 1u);
}

TEST(VoiceLimiter, SetPriority_ChangesWhoIsEvicted)
{
    VoiceLimiter limiter(2);
    ASSERT_TRUE(limiter.Acquire(0.9f).slot);
    ASSERT_TRUE(limiter.Acquire(0.8f).slot);
    limiter.SetPriority(0, 0.1f); // e.g. the listener walked away from it
    EXPECT_EQ(limiter.Acquire(0.5f).evicted, 0u);
}
