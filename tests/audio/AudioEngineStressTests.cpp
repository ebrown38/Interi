// SPDX-License-Identifier: MIT
// The game thread against a real audio thread (miniaudio's null device): plays, evictions, gain updates and clip
// drops while the device mixes. Meant for TSan and ASan.
#include <interi/audio/AudioEngine.h>
#include <interi/core/Random.h>

#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <thread>

using interi::Random;
using interi::Vec3;
using interi::audio::AudioBackend;
using interi::audio::AudioEngine;
using interi::audio::Listener;
using interi::audio::PlayParams;
using interi::audio::SoundClip;

TEST(AudioEngineStress, NullDevice_PlaysEvictsAndDrains)
{
    auto created = AudioEngine::Create(
        {.backend = AudioBackend::Null, .maxVoices = 16, .commandCapacity = 64, .periodFrames = 256});
    ASSERT_TRUE(created.has_value()) << created.error().message;
    AudioEngine& engine = **created;
    EXPECT_EQ(engine.Backend(), AudioBackend::Null);

    Random random(7);
    std::uint64_t plays = 0;
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(600))
    {
        for (int i = 0; i < 20; ++i)
        {
            auto clip = std::make_shared<SoundClip>();
            clip->samples.assign(256 + random.NextBelow(4096), 0.01f); // freed by the engine, never by the caller
            PlayParams params;
            params.position = Vec3(random.NextFloat(-40.0f, 40.0f), 0.0f, random.NextFloat(-40.0f, 40.0f));
            params.priority = random.NextFloat01();
            params.attenuation.maxDistance = 50.0f;
            (void)engine.Play(std::move(clip), params);
            ++plays;
        }
        Listener listener;
        listener.position = Vec3(random.NextFloat(-5.0f, 5.0f), 0.0f, 0.0f);
        engine.SetListener(listener);
        engine.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    const auto stats = engine.Stats();
    EXPECT_EQ(stats.started + stats.culled + stats.rejected + stats.dropped, plays);
    EXPECT_GT(stats.started, 0u);
    EXPECT_GT(stats.evicted + stats.rejected, 0u); // the cap was hit

    // Short clips: everything drains once plays stop.
    const auto drainStart = std::chrono::steady_clock::now();
    while (engine.Stats().playing > 0 && std::chrono::steady_clock::now() - drainStart < std::chrono::seconds(5))
    {
        engine.Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(engine.Stats().playing, 0u);
}
