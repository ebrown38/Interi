// SPDX-License-Identifier: MIT
#include <interi/core/Profiling.h>
#include <interi/destruction_audio/DestructionAudio.h>
#include <interi/destruction_runtime/DebrisSystem.h>

#include <algorithm>
#include <limits>

namespace interi::destruction
{

namespace
{

StringId ChunkMaterial(const DestructibleAsset& asset, std::uint32_t chunk)
{
    if (chunk >= asset.chunks.size())
    {
        return {};
    }
    const std::uint32_t material = asset.chunks[chunk].material;
    return material < asset.materials.size() ? StringId(asset.materials[material].name) : StringId();
}

} // namespace

void ClipLibrary::Add(std::string name, std::shared_ptr<const audio::SoundClip> clip)
{
    m_clips.insert_or_assign(std::move(name), std::move(clip));
}

std::shared_ptr<const audio::SoundClip> ClipLibrary::Find(std::string_view name) const
{
    const auto it = m_clips.find(name);
    return it != m_clips.end() ? it->second : nullptr;
}

DestructionAudio::DestructionAudio(audio::AudioEngine& engine, const DestructionAudioConfig& config,
                                   const ClipLibrary& clips, const DestructionAudioSettings& settings)
    : m_engine(&engine)
    , m_config(&config)
    , m_clips(&clips)
    , m_batcher(settings.impactRegionSize)
    , m_stress(settings.stress)
    , m_collapse(settings.rumble)
    , m_random(settings.seed)
{
}

StringId DestructionAudio::MaterialOf(const World& world, const DestructionWorld& destruction, Entity entity,
                                      std::uint32_t subShape)
{
    if (!world.IsAlive(entity))
    {
        return {};
    }
    if (const auto* piece = world.Get<DestructibleComponent>(entity);
        piece != nullptr && destruction.IsAlive(piece->instance))
    {
        const std::optional<std::uint32_t> chunk = destruction.ChunkAt(world, entity, subShape);
        return chunk ? ChunkMaterial(destruction.Asset(piece->instance), *chunk) : StringId();
    }
    if (const auto* debris = world.Get<DebrisSourceComponent>(entity);
        debris != nullptr && debris->asset != nullptr && !debris->chunks.empty())
    {
        return ChunkMaterial(*debris->asset, debris->chunks.front());
    }
    if (const auto* rubble = world.Get<RubbleSourceComponent>(entity); rubble != nullptr && !rubble->parts.empty() &&
                                                                       rubble->parts.front().asset != nullptr &&
                                                                       !rubble->parts.front().chunks.empty())
    {
        return ChunkMaterial(*rubble->parts.front().asset, rubble->parts.front().chunks.front());
    }
    if (const auto* material = world.Get<AudioMaterialComponent>(entity))
    {
        return material->material;
    }
    return {};
}

void DestructionAudio::Update(const World& world, const physics::PhysicsWorld& physics,
                              const DestructionWorld& destruction, float deltaSeconds)
{
    INTERI_ZONE_NAMED("DestructionAudio::Update");
    m_stats = {};
    const std::span<const ImpactBank> banks = m_config->impacts;
    float minImpulse = std::numeric_limits<float>::max(); // quieter contacts are skipped before material lookups
    for (const ImpactBank& bank : banks)
    {
        minImpulse = std::min(minImpulse, bank.minImpulse);
    }
    for (const physics::ContactEvent& contact : physics.Contacts())
    {
        if (contact.impulse < minImpulse)
        {
            continue;
        }
        const StringId a = MaterialOf(world, destruction, contact.entityA, contact.subShapeA);
        const StringId b = MaterialOf(world, destruction, contact.entityB, contact.subShapeB);
        const std::optional<std::uint32_t> bank = SelectImpactBank(banks, a, b);
        if (bank && contact.impulse >= banks[*bank].minImpulse)
        {
            m_batcher.Add({.point = contact.point, .impulse = contact.impulse, .bank = *bank});
            ++m_stats.contacts;
        }
    }
    for (const ImpactGroup& group : m_batcher.Flush())
    {
        const ImpactBank& bank = banks[group.bank];
        PlayCue(bank, group.position, ImpactVolume(bank, group.impulse));
        ++m_stats.impactVoices;
    }

    m_creaks.clear();
    m_stress.Update(destruction, deltaSeconds, m_creaks);
    for (const CreakEvent& creak : m_creaks)
    {
        PlayCue(m_config->creak, creak.position, m_config->creak.volume);
        ++m_stats.creaks;
    }

    m_collapses.clear();
    m_collapse.Update(world, destruction, m_collapses);
    for (const CollapseEvent& collapse : m_collapses)
    {
        const float volume = RumbleVolume(m_config->rumble, collapse.mass);
        if (volume > 0.0f)
        {
            PlayCue(m_config->rumble, collapse.position, volume);
            ++m_stats.rumbles;
        }
    }
}

void DestructionAudio::PlayExplosion(const Vec3& position, float strength)
{
    PlayCue(m_config->explosion, position, m_config->explosion.volume * std::clamp(strength, 0.0f, 1.0f));
    ++m_stats.explosions;
}

void DestructionAudio::PlayCue(const AudioCue& cue, const Vec3& position, float volume)
{
    if (cue.clips.empty() || !(volume > 0.0f))
    {
        return;
    }
    const auto pick = m_random.NextBelow(static_cast<std::uint32_t>(cue.clips.size()));
    std::shared_ptr<const audio::SoundClip> clip = m_clips->Find(cue.clips[pick]);
    if (clip == nullptr)
    {
        return;
    }
    audio::PlayParams params;
    params.position = position;
    params.volume = volume;
    params.priority = cue.priority;
    params.attenuation = cue.attenuation;
    (void)m_engine->Play(std::move(clip), params);
}

} // namespace interi::destruction
