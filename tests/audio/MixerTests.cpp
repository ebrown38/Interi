// SPDX-License-Identifier: MIT
#include <interi/audio/Mixer.h>

#include <gtest/gtest.h>

#include <vector>

using interi::audio::Mixer;
using interi::audio::SoundClip;
using interi::audio::StereoGains;
using interi::audio::VoiceEnd;

namespace
{

SoundClip Constant(float value, std::size_t samples)
{
    SoundClip clip;
    clip.samples.assign(samples, value);
    return clip;
}

} // namespace

TEST(Mixer, TwoVoices_SumsGains)
{
    Mixer mixer(4);
    const SoundClip a = Constant(0.5f, 1000);
    const SoundClip b = Constant(0.25f, 1000);
    EXPECT_FALSE(mixer.Start(0, 1, &a, {.left = 1.0f, .right = 0.0f}, false).has_value());
    EXPECT_FALSE(mixer.Start(2, 2, &b, {.left = 0.5f, .right = 1.0f}, false).has_value());
    EXPECT_EQ(mixer.PlayingCount(), 2u);
    std::vector<float> out(2 * 64);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_TRUE(ended.empty());
    for (std::size_t frame = 0; frame < 64; ++frame)
    {
        EXPECT_FLOAT_EQ(out[2 * frame], 0.5f * 1.0f + 0.25f * 0.5f);
        EXPECT_FLOAT_EQ(out[2 * frame + 1], 0.25f);
    }
    mixer.Mix(out, 0.5f, ended); // master volume
    EXPECT_FLOAT_EQ(out[0], 0.5f * (0.5f + 0.125f));
}

TEST(Mixer, VoiceEnds_Frees)
{
    Mixer mixer(2);
    const SoundClip clip = Constant(0.5f, 100);
    (void)mixer.Start(1, 7, &clip, {.left = 1.0f, .right = 1.0f}, false);
    std::vector<float> out(2 * 64);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_TRUE(ended.empty());
    mixer.Mix(out, 1.0f, ended); // samples 64..99, then silence
    ASSERT_EQ(ended.size(), 1u);
    EXPECT_EQ(ended[0].slot, 1u);
    EXPECT_EQ(ended[0].generation, 7u);
    EXPECT_FLOAT_EQ(out[2 * 35], 0.5f);
    EXPECT_FLOAT_EQ(out[2 * 36], 0.0f);
    EXPECT_EQ(mixer.PlayingCount(), 0u);
}

TEST(Mixer, Loop_Wraps)
{
    Mixer mixer(1);
    SoundClip clip;
    clip.samples = {0.1f, 0.2f, 0.3f};
    (void)mixer.Start(0, 1, &clip, {.left = 1.0f, .right = 1.0f}, true);
    std::vector<float> out(2 * 7);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_TRUE(ended.empty());
    EXPECT_FLOAT_EQ(out[2 * 3], 0.1f);
    EXPECT_FLOAT_EQ(out[2 * 6], 0.1f);
}

TEST(Mixer, GainChange_RampsOverBlock)
{
    Mixer mixer(1);
    const SoundClip clip = Constant(1.0f, 1000);
    (void)mixer.Start(0, 1, &clip, {.left = 0.0f, .right = 0.0f}, false);
    mixer.SetGains(0, 1, {.left = 1.0f, .right = 0.5f});
    std::vector<float> out(2 * 4);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_FLOAT_EQ(out[0], 0.25f);
    EXPECT_FLOAT_EQ(out[2], 0.5f);
    EXPECT_FLOAT_EQ(out[6], 1.0f);
    EXPECT_FLOAT_EQ(out[7], 0.5f);
    mixer.Mix(out, 1.0f, ended); // reached
    EXPECT_FLOAT_EQ(out[0], 1.0f);
}

TEST(Mixer, Stop_FadesOutThenEnds)
{
    Mixer mixer(1);
    const SoundClip clip = Constant(1.0f, 1000);
    (void)mixer.Start(0, 3, &clip, {.left = 1.0f, .right = 1.0f}, false);
    mixer.Stop(0, 2); // stale generation: ignored
    mixer.Stop(0, 3);
    std::vector<float> out(2 * 4);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_FLOAT_EQ(out[0], 0.75f);
    EXPECT_FLOAT_EQ(out[6], 0.0f);
    ASSERT_EQ(ended.size(), 1u);
    EXPECT_EQ(ended[0].generation, 3u);
}

TEST(Mixer, StartOverLiveVoice_ReportsCut)
{
    Mixer mixer(1);
    const SoundClip clip = Constant(1.0f, 1000);
    (void)mixer.Start(0, 1, &clip, {}, false);
    const auto cut = mixer.Start(0, 2, &clip, {}, false);
    ASSERT_TRUE(cut.has_value());
    EXPECT_EQ(cut->generation, 1u);
    mixer.SetGains(0, 1, {.left = 1.0f}); // the old generation is gone
    std::vector<float> out(2 * 2);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_FLOAT_EQ(out[0], 0.0f);
}

TEST(Mixer, Output_Clamped)
{
    Mixer mixer(2);
    const SoundClip clip = Constant(0.9f, 100);
    (void)mixer.Start(0, 1, &clip, {.left = 1.0f, .right = -1.0f}, false);
    (void)mixer.Start(1, 2, &clip, {.left = 1.0f, .right = -1.0f}, false);
    std::vector<float> out(2 * 2);
    std::vector<VoiceEnd> ended;
    mixer.Mix(out, 1.0f, ended);
    EXPECT_EQ(out[0], 1.0f);
    EXPECT_EQ(out[1], -1.0f);
}
