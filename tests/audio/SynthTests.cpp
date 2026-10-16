// SPDX-License-Identifier: MIT
#include <interi/audio/Synth.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using interi::audio::SoundClip;
using interi::audio::SynthDesc;
using interi::audio::Synthesize;
using interi::audio::SynthKind;

TEST(Synth, EveryKind_DurationAndPeak)
{
    for (const SynthKind kind : {SynthKind::Impact, SynthKind::Explosion, SynthKind::Rumble, SynthKind::Creak})
    {
        const SoundClip clip = Synthesize({.kind = kind, .duration = 0.8f, .pitch = 300.0f, .volume = 0.7f}, 48000);
        EXPECT_EQ(clip.samples.size(), 38400u);
        float peak = 0.0f;
        bool finite = true;
        for (const float sample : clip.samples)
        {
            peak = std::max(peak, std::abs(sample));
            finite = finite && std::isfinite(sample);
        }
        EXPECT_TRUE(finite) << static_cast<int>(kind);
        EXPECT_NEAR(peak, 0.7f, 1e-5f) << static_cast<int>(kind);
        EXPECT_NEAR(clip.samples.back(), 0.0f, 1e-3f) << static_cast<int>(kind); // faded out: no click
    }
}

TEST(Synth, SameSeed_SameSamples)
{
    const SynthDesc desc{.kind = SynthKind::Impact, .duration = 0.3f, .brightness = 0.8f, .seed = 42};
    const SoundClip a = Synthesize(desc, 44100);
    const SoundClip b = Synthesize(desc, 44100);
    EXPECT_EQ(a.samples, b.samples);
    SynthDesc other = desc;
    other.seed = 43;
    EXPECT_NE(Synthesize(other, 44100).samples, a.samples);
}

TEST(Synth, Impact_DecaysOverTime)
{
    const SoundClip clip = Synthesize({.kind = SynthKind::Impact, .duration = 1.0f, .decay = 8.0f}, 48000);
    const auto energy = [&](std::size_t from, std::size_t to)
    {
        double sum = 0.0;
        for (std::size_t i = from; i < to; ++i)
        {
            sum += static_cast<double>(clip.samples[i]) * static_cast<double>(clip.samples[i]);
        }
        return sum;
    };
    EXPECT_GT(energy(0, 4800), 10.0 * energy(24000, 28800));
}
