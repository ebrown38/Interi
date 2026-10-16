// SPDX-License-Identifier: MIT
#include <interi/audio/SoundClip.h>
#include <interi/platform/FileSystem.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <numbers>

using interi::ErrorCode;
using interi::MemoryFileSystem;
using interi::audio::DecodeClip;
using interi::audio::EncodeWav16;
using interi::audio::LoadClip;
using interi::audio::SoundClip;

namespace
{

SoundClip Sine(std::uint32_t rate, float hz, float seconds)
{
    SoundClip clip;
    clip.sampleRate = rate;
    const auto count = static_cast<std::size_t>(seconds * static_cast<float>(rate));
    for (std::size_t i = 0; i < count; ++i)
    {
        clip.samples.push_back(
            0.5f * std::sin(2.0f * std::numbers::pi_v<float> * hz * static_cast<float>(i) / static_cast<float>(rate)));
    }
    return clip;
}

} // namespace

TEST(SoundClip, Wav_Decodes)
{
    const SoundClip source = Sine(48000, 440.0f, 0.25f);
    const auto decoded = DecodeClip(EncodeWav16(source), 48000);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->sampleRate, 48000u);
    ASSERT_EQ(decoded->samples.size(), source.samples.size());
    for (std::size_t i = 0; i < source.samples.size(); i += 97)
    {
        EXPECT_NEAR(decoded->samples[i], source.samples[i], 1.0f / 16384.0f) << i;
    }
    EXPECT_NEAR(decoded->Duration(), 0.25f, 1e-4f);
}

TEST(SoundClip, Wav_ResampledToEngineRate)
{
    const SoundClip source = Sine(22050, 220.0f, 0.5f);
    const auto decoded = DecodeClip(EncodeWav16(source), 48000);
    ASSERT_TRUE(decoded.has_value()) << decoded.error().message;
    EXPECT_EQ(decoded->sampleRate, 48000u);
    EXPECT_NEAR(decoded->Duration(), 0.5f, 0.01f);
}

TEST(SoundClip, Garbage_Errors)
{
    const std::byte garbage[] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}, std::byte{5}};
    const auto decoded = DecodeClip(garbage, 48000);
    ASSERT_FALSE(decoded.has_value());
    EXPECT_EQ(decoded.error().code, ErrorCode::InvalidArgument);
    EXPECT_FALSE(DecodeClip({}, 48000).has_value());
}

TEST(SoundClip, LoadClip_ThroughFileSystem)
{
    MemoryFileSystem files;
    const std::vector<std::byte> wav = EncodeWav16(Sine(48000, 100.0f, 0.1f));
    ASSERT_TRUE(files.WriteFile("sounds/hum.wav", wav).has_value());
    const auto clip = LoadClip(files, "sounds/hum.wav", 48000);
    ASSERT_TRUE(clip.has_value()) << clip.error().message;
    EXPECT_EQ(clip->samples.size(), 4800u);
    EXPECT_EQ(LoadClip(files, "sounds/missing.wav", 48000).error().code, ErrorCode::NotFound);
}
