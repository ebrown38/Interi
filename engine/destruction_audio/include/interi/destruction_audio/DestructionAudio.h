// SPDX-License-Identifier: MIT
#pragma once

#include <interi/audio/AudioEngine.h>
#include <interi/audio/SoundClip.h>
#include <interi/core/Random.h>
#include <interi/core/StringId.h>
#include <interi/destruction_audio/AudioConfig.h>
#include <interi/destruction_audio/ImpactBatcher.h>
#include <interi/destruction_audio/StructureAudio.h>
#include <interi/destruction_runtime/DestructionWorld.h>
#include <interi/ecs/Component.h>
#include <interi/ecs/World.h>
#include <interi/physics/PhysicsWorld.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace interi::destruction
{

/// The surface material of a non-destructible body for impact sounds (a vehicle's "metal", the ground's "dirt").
/// Destructible pieces, debris and rubble use their chunks' DestructionMaterial names instead.
struct AudioMaterialComponent
{
    static constexpr const char* kComponentName = "interi.destruction.AudioMaterial";
    StringId material;
};

static_assert(Component<AudioMaterialComponent>);

/// Named sounds the config's cues refer to.
class ClipLibrary
{
public:
    void Add(std::string name, std::shared_ptr<const audio::SoundClip> clip);
    [[nodiscard]] std::shared_ptr<const audio::SoundClip> Find(std::string_view name) const;
    [[nodiscard]] std::size_t Size() const noexcept { return m_clips.size(); }

private:
    std::map<std::string, std::shared_ptr<const audio::SoundClip>, std::less<>> m_clips;
};

struct DestructionAudioSettings
{
    float impactRegionSize = 4.0f; ///< m: contacts of one bank in one cell are one voice per frame.
    StressAudioSettings stress;
    RumbleSettings rumble;
    std::uint64_t seed = 1; ///< Picks among a cue's clips.
};

struct DestructionAudioStats
{
    std::uint32_t contacts = 0;     ///< Contacts loud enough for their bank.
    std::uint32_t impactVoices = 0; ///< Groups played (one per bank and region).
    std::uint32_t creaks = 0;
    std::uint32_t rumbles = 0;
    std::uint32_t explosions = 0;
};

/// Destruction sounds: impacts chosen by material pair and impulse and batched per frame and region, creaks where
/// stress nears the limits, collapse rumble scaled by the mass that falls, and explosions on request.
///
/// Call Update once per fixed step after DestructionWorld::Update (it reads the step's contacts and new pieces), on
/// the AudioEngine's thread. The config and clip library are referenced, not copied.
class DestructionAudio
{
public:
    DestructionAudio(audio::AudioEngine& engine, const DestructionAudioConfig& config, const ClipLibrary& clips,
                     const DestructionAudioSettings& settings = {});

    void Update(const World& world, const physics::PhysicsWorld& physics, const DestructionWorld& destruction,
                float deltaSeconds);

    /// A blast at `position`; `strength` in [0, 1] scales the explosion cue's volume.
    void PlayExplosion(const Vec3& position, float strength);

    /// The material a contact side is heard as: a destructible chunk's or a debris/rubble piece's
    /// DestructionMaterial name, else its AudioMaterialComponent, else none (only "*" banks match).
    [[nodiscard]] static StringId MaterialOf(const World& world, const DestructionWorld& destruction, Entity entity,
                                             std::uint32_t subShape);

    [[nodiscard]] const DestructionAudioStats& LastStats() const noexcept { return m_stats; }

private:
    void PlayCue(const AudioCue& cue, const Vec3& position, float volume);

    audio::AudioEngine* m_engine;
    const DestructionAudioConfig* m_config;
    const ClipLibrary* m_clips;
    ImpactBatcher m_batcher;
    StressAudio m_stress;
    CollapseAudio m_collapse;
    Random m_random;
    DestructionAudioStats m_stats;
    std::vector<CreakEvent> m_creaks;
    std::vector<CollapseEvent> m_collapses;
};

} // namespace interi::destruction
