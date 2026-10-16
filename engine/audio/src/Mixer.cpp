// SPDX-License-Identifier: MIT
#include <interi/audio/Mixer.h>
#include <interi/core/Assert.h>

#include <algorithm>

namespace interi::audio
{

Mixer::Mixer(std::uint32_t voices)
    : m_voices(voices)
{
}

std::optional<VoiceEnd> Mixer::Start(std::uint32_t slot, std::uint32_t generation, const SoundClip* clip,
                                     const StereoGains& gains, bool loop)
{
    INTERI_ASSERT(slot < m_voices.size() && clip != nullptr);
    Voice& voice = m_voices[slot];
    std::optional<VoiceEnd> cut;
    if (voice.clip != nullptr)
    {
        cut = VoiceEnd{.slot = slot, .generation = voice.generation};
    }
    voice = {.clip = clip, .generation = generation, .current = gains, .target = gains, .loop = loop};
    if (clip->samples.empty())
    {
        voice.stopping = true; // nothing to play: ends at the next Mix
    }
    return cut;
}

void Mixer::SetGains(std::uint32_t slot, std::uint32_t generation, const StereoGains& gains)
{
    INTERI_ASSERT(slot < m_voices.size());
    Voice& voice = m_voices[slot];
    if (voice.clip != nullptr && voice.generation == generation && !voice.stopping)
    {
        voice.target = gains;
    }
}

void Mixer::Stop(std::uint32_t slot, std::uint32_t generation)
{
    INTERI_ASSERT(slot < m_voices.size());
    Voice& voice = m_voices[slot];
    if (voice.clip != nullptr && voice.generation == generation)
    {
        voice.stopping = true;
        voice.target = {};
    }
}

void Mixer::Mix(std::span<float> out, float masterVolume, std::vector<VoiceEnd>& ended)
{
    std::ranges::fill(out, 0.0f);
    const std::size_t frames = out.size() / 2;
    const float step = frames > 0 ? 1.0f / static_cast<float>(frames) : 0.0f;
    for (std::uint32_t slot = 0; slot < m_voices.size(); ++slot)
    {
        Voice& voice = m_voices[slot];
        if (voice.clip == nullptr)
        {
            continue;
        }
        const std::span<const float> samples = voice.clip->samples;
        const StereoGains from = voice.current;
        const float dLeft = (voice.target.left - from.left) * step;
        const float dRight = (voice.target.right - from.right) * step;
        bool finished = samples.empty();
        for (std::size_t frame = 0; frame < frames && !finished; ++frame)
        {
            const auto t = static_cast<float>(frame + 1);
            const float sample = samples[voice.position];
            out[2 * frame] += sample * (from.left + dLeft * t);
            out[2 * frame + 1] += sample * (from.right + dRight * t);
            if (++voice.position == samples.size())
            {
                voice.position = 0;
                finished = !voice.loop;
            }
        }
        voice.current = voice.target;
        if (finished || voice.stopping)
        {
            ended.push_back({.slot = slot, .generation = voice.generation});
            voice = {};
        }
    }
    for (float& sample : out)
    {
        sample = std::clamp(sample * masterVolume, -1.0f, 1.0f);
    }
}

std::uint32_t Mixer::PlayingCount() const noexcept
{
    return static_cast<std::uint32_t>(
        std::ranges::count_if(m_voices, [](const Voice& voice) { return voice.clip != nullptr; }));
}

} // namespace interi::audio
