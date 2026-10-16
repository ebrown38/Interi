// SPDX-License-Identifier: MIT
#include <interi/audio/AudioEngine.h>

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

using interi::Vec3;
using interi::audio::AudioBackend;
using interi::audio::AudioEngine;
using interi::audio::AudioEngineDesc;
using interi::audio::Listener;
using interi::audio::PlayParams;
using interi::audio::SoundClip;
using interi::audio::VoiceId;

namespace
{

std::shared_ptr<SoundClip> Constant(float value, std::size_t samples, std::uint32_t rate = 48000)
{
    auto clip = std::make_shared<SoundClip>();
    clip->sampleRate = rate;
    clip->samples.assign(samples, value);
    return clip;
}

std::unique_ptr<AudioEngine> Offline(std::uint32_t voices = 8)
{
    auto engine = AudioEngine::Create({.backend = AudioBackend::Offline, .maxVoices = voices});
    EXPECT_TRUE(engine.has_value());
    return std::move(*engine);
}

PlayParams Flat(float priority = 1.0f)
{
    PlayParams params;
    params.spatial = false;
    params.priority = priority;
    return params;
}

PlayParams At(const Vec3& position)
{
    PlayParams params;
    params.position = position;
    return params;
}

} // namespace

TEST(AudioEngine, Offline_PlaysAndFinishes)
{
    auto engine = Offline();
    const VoiceId voice = engine->Play(Constant(0.5f, 1000), Flat());
    ASSERT_TRUE(voice.IsValid());
    EXPECT_TRUE(engine->IsPlaying(voice));
    std::vector<float> out(2 * 480);
    engine->Render(out);
    EXPECT_NEAR(out[0], 0.5f * std::sqrt(0.5f), 1e-6f); // centred, equal power
    EXPECT_NEAR(out[1], out[0], 1e-6f);
    engine->Render(out);
    engine->Update();
    EXPECT_TRUE(engine->IsPlaying(voice)); // not reported ended until a block finds the end
    engine->Render(out);                   // samples 960..999, then silence
    EXPECT_GT(out[2 * 39], 0.0f);
    EXPECT_EQ(out[2 * 40], 0.0f);
    engine->Update();
    EXPECT_FALSE(engine->IsPlaying(voice));
    EXPECT_EQ(engine->Stats().playing, 0u);
    EXPECT_EQ(engine->Stats().started, 1u);
}

TEST(AudioEngine, OverCap_DropsLowestPriority)
{
    auto engine = Offline(2);
    const VoiceId low = engine->Play(Constant(0.1f, 48000), Flat(0.2f));
    const VoiceId high = engine->Play(Constant(0.1f, 48000), Flat(0.9f));
    const VoiceId rejected = engine->Play(Constant(0.1f, 48000), Flat(0.1f));
    EXPECT_FALSE(rejected.IsValid());
    const VoiceId newcomer = engine->Play(Constant(0.1f, 48000), Flat(0.5f));
    ASSERT_TRUE(newcomer.IsValid());
    EXPECT_FALSE(engine->IsPlaying(low));
    EXPECT_TRUE(engine->IsPlaying(high));
    EXPECT_EQ(newcomer.slot, low.slot);
    const auto stats = engine->Stats();
    EXPECT_EQ(stats.rejected, 1u);
    EXPECT_EQ(stats.evicted, 1u);
    EXPECT_EQ(stats.playing, 2u);
}

TEST(AudioEngine, Inaudible_Culled)
{
    auto engine = Offline();
    PlayParams far = At(Vec3(0.0f, 0.0f, -500.0f));
    far.attenuation.maxDistance = 100.0f;
    EXPECT_FALSE(engine->Play(Constant(0.5f, 100), far).IsValid());
    PlayParams silent = Flat();
    silent.volume = 0.0f;
    EXPECT_FALSE(engine->Play(Constant(0.5f, 100), silent).IsValid());
    EXPECT_EQ(engine->Stats().culled, 2u);
    EXPECT_FALSE(engine->Play(nullptr, Flat()).IsValid());
}

TEST(AudioEngine, PriorityScalesWithAudibleGain)
{
    // Same priority, but the far voice is quieter, so it gives way first.
    auto engine = Offline(2);
    const PlayParams near = At(Vec3(0.0f, 0.0f, -1.0f));
    const PlayParams far = At(Vec3(0.0f, 0.0f, -60.0f));
    const VoiceId nearVoice = engine->Play(Constant(0.1f, 48000), near);
    const VoiceId farVoice = engine->Play(Constant(0.1f, 48000), far);
    const PlayParams mid = At(Vec3(0.0f, 0.0f, -10.0f));
    ASSERT_TRUE(engine->Play(Constant(0.1f, 48000), mid).IsValid());
    EXPECT_TRUE(engine->IsPlaying(nearVoice));
    EXPECT_FALSE(engine->IsPlaying(farVoice));
}

TEST(AudioEngine, ListenerMoves_GainsFollow)
{
    auto engine = Offline();
    PlayParams params = At(Vec3(5.0f, 0.0f, 0.0f));
    params.attenuation.minDistance = 10.0f;
    const VoiceId voice = engine->Play(Constant(1.0f, 48000), params);
    ASSERT_TRUE(voice.IsValid());
    std::vector<float> out(2 * 256);
    engine->Render(out);
    EXPECT_GT(out[2 * 255 + 1], 0.99f); // right
    EXPECT_LT(out[2 * 255], 0.01f);

    Listener turned;
    turned.forward = Vec3(0.0f, 0.0f, 1.0f); // the source is now on the left
    engine->SetListener(turned);
    engine->Update();
    engine->Render(out); // ramps over this block
    engine->Render(out);
    EXPECT_GT(out[0], 0.99f);
    EXPECT_LT(out[1], 0.01f);

    engine->SetPosition(voice, Vec3(0.0f, 0.0f, 5.0f)); // straight ahead of the turned listener
    engine->SetVolume(voice, 0.5f);
    engine->Update();
    engine->Render(out);
    engine->Render(out);
    EXPECT_NEAR(out[0], 0.5f * std::sqrt(0.5f), 1e-4f);
    EXPECT_NEAR(out[1], out[0], 1e-4f);
}

TEST(AudioEngine, Stop_EndsVoice)
{
    auto engine = Offline();
    PlayParams looping = Flat();
    looping.loop = true;
    const VoiceId voice = engine->Play(Constant(0.5f, 48000), looping);
    engine->Stop(voice);
    EXPECT_TRUE(engine->IsPlaying(voice));
    std::vector<float> out(2 * 128);
    engine->Render(out);
    EXPECT_EQ(out[2 * 127], 0.0f); // faded out over the block
    engine->Update();
    EXPECT_FALSE(engine->IsPlaying(voice));
}

TEST(AudioEngine, DroppedClip_KeptUntilAudioReportsEnd)
{
    auto engine = Offline(1);
    std::weak_ptr<SoundClip> watched;
    {
        auto clip = Constant(0.5f, 48000);
        watched = clip;
        ASSERT_TRUE(engine->Play(clip, Flat(0.1f)).IsValid());
    } // the caller drops its reference while the voice plays
    EXPECT_FALSE(watched.expired());
    std::vector<float> out(2 * 64);
    engine->Render(out);
    EXPECT_GT(out[0], 0.0f);

    // A higher-priority play takes the slot over: the old clip must survive until the audio side has cut it.
    ASSERT_TRUE(engine->Play(Constant(0.2f, 48000), Flat(0.9f)).IsValid());
    EXPECT_FALSE(watched.expired());
    engine->Render(out);
    engine->Update();
    EXPECT_TRUE(watched.expired());
}

TEST(AudioEngine, InvalidDesc_Fails)
{
    EXPECT_FALSE(AudioEngine::Create({.backend = AudioBackend::Offline, .maxVoices = 0}).has_value());
}
