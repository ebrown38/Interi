// SPDX-License-Identifier: MIT
#include <interi/audio/AudioEngine.h>
#include <interi/audio/Synth.h>
#include <interi/destruction_audio/DestructionAudio.h>

#include "../destruction_runtime/RuntimeTestUtil.h"
#include "../gameplay/GameplayTestUtil.h"
#include <gtest/gtest.h>

#include <memory>

using interi::Entity;
using interi::StringId;
using interi::Transform;
using interi::Vec3;
using interi::audio::AudioBackend;
using interi::audio::AudioEngine;
using interi::audio::SynthDesc;
using interi::audio::Synthesize;
using interi::destruction::AudioMaterialComponent;
using interi::destruction::ClipLibrary;
using interi::destruction::DestructionAudio;
using interi::destruction::DestructionAudioConfig;
using interi::destruction::DestructionWorld;
using interi::destruction::ImpactBank;
using interi::testing::GameplayScene;
using interi::testing::kGameplayStep;

namespace
{

class DestructionAudioTest : public ::testing::Test
{
protected:
    DestructionAudioTest()
    {
        ImpactBank metal;
        metal.materialA = "metal";
        metal.idA = StringId("metal");
        metal.clips = {"clank"};
        metal.minImpulse = 50.0f;
        config.impacts.push_back(metal);
        ImpactBank any;
        any.clips = {"thud"};
        any.minImpulse = 50.0f;
        config.impacts.push_back(any);
        config.creak.clips = {"creak"};
        config.rumble.clips = {"rumble"};
        config.explosion.clips = {"boom"};
        for (const char* name : {"clank", "thud", "creak", "rumble", "boom"})
        {
            clips.Add(name, std::make_shared<const interi::audio::SoundClip>(
                                Synthesize(SynthDesc{.duration = 0.2f}, engine->SampleRate())));
        }
        ground = scene.AddGround();
        scene.world.Add(ground, AudioMaterialComponent{.material = StringId("dirt")});
    }

    Entity DropBox(const Vec3& center)
    {
        const Entity box = scene.AddDynamicBox(center, Vec3(0.25f), 100.0f);
        scene.world.Add(box, AudioMaterialComponent{.material = StringId("metal")});
        return box;
    }

    /// Steps until some step produced impact voices; returns how many.
    std::uint32_t StepUntilImpact(int maxSteps = 120)
    {
        for (int i = 0; i < maxSteps; ++i)
        {
            scene.Step();
            destruction.Update(scene.world, scene.jobs);
            audio.Update(scene.world, scene.physics, destruction, kGameplayStep);
            if (audio.LastStats().impactVoices > 0)
            {
                return audio.LastStats().impactVoices;
            }
        }
        return 0;
    }

    std::unique_ptr<AudioEngine> engine = std::move(*AudioEngine::Create({.backend = AudioBackend::Offline}));
    DestructionAudioConfig config;
    ClipLibrary clips;
    GameplayScene scene;
    DestructionWorld destruction{scene.physics};
    DestructionAudio audio{*engine, config, clips};
    Entity ground;
};

} // namespace

TEST_F(DestructionAudioTest, BoxLands_PlaysItsMaterialBank)
{
    const Entity box = DropBox(Vec3(0.0f, 2.0f, 0.0f));
    EXPECT_EQ(DestructionAudio::MaterialOf(scene.world, destruction, box, 0), StringId("metal"));
    EXPECT_EQ(StepUntilImpact(), 1u);
    EXPECT_EQ(audio.LastStats().contacts, 1u);
    EXPECT_EQ(engine->Stats().started, 1u);
}

TEST_F(DestructionAudioTest, ManyContactsSameRegion_OneVoice)
{
    for (int i = 0; i < 9; ++i)
    {
        DropBox(Vec3(0.6f * static_cast<float>(i % 3), 2.0f, 0.6f * static_cast<float>(i / 3)));
    }
    EXPECT_EQ(StepUntilImpact(), 1u);
    EXPECT_EQ(audio.LastStats().contacts, 9u);
    EXPECT_EQ(engine->Stats().started, 1u);
}

TEST_F(DestructionAudioTest, DestructiblePiece_HeardAsChunkMaterial)
{
    auto asset = std::make_shared<const interi::destruction::DestructibleAsset>(
        interi::destruction::testing::MakeGridAsset(interi::destruction::testing::BoxCells(2, 1, 1)));
    const auto id = destruction.Spawn(scene.world, scene.jobs, asset, Transform{});
    scene.Step();
    const Entity piece = destruction.PieceEntity(id, 0);
    EXPECT_EQ(DestructionAudio::MaterialOf(scene.world, destruction, piece, 0), StringId("test"));
    EXPECT_FALSE(DestructionAudio::MaterialOf(scene.world, destruction, Entity(), 0).IsValid());
}

TEST_F(DestructionAudioTest, Explosion_Plays)
{
    audio.PlayExplosion(Vec3(0.0f, 0.0f, -5.0f), 1.0f);
    EXPECT_EQ(engine->Stats().started, 1u);
    audio.PlayExplosion(Vec3(0.0f, 0.0f, -5.0f), 0.0f); // silent: not played
    EXPECT_EQ(engine->Stats().started, 1u);
}
