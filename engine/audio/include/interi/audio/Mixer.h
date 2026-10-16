// SPDX-License-Identifier: MIT
#pragma once

#include <interi/audio/SoundClip.h>
#include <interi/audio/Spatializer.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace interi::audio
{

/// A voice that stopped sounding: its clip is no longer read.
struct VoiceEnd
{
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
};

/// The software mixer of the audio thread: a fixed number of voice slots, each playing one mono clip into an
/// interleaved stereo float buffer with per-channel gains. Gain changes are ramped linearly over one Mix block and a
/// stop fades out over one block, so neither clicks.
///
/// A voice is addressed by (slot, generation); commands for a generation that no longer plays are ignored. The
/// mixer holds raw clip pointers: a clip must stay alive until its voice is reported ended. Not thread-safe (one
/// audio thread); never allocates after construction.
class Mixer
{
public:
    explicit Mixer(std::uint32_t voices);

    /// Starts a voice in `slot`. If another voice still played there, it is cut and returned as ended.
    std::optional<VoiceEnd> Start(std::uint32_t slot, std::uint32_t generation, const SoundClip* clip,
                                  const StereoGains& gains, bool loop);
    void SetGains(std::uint32_t slot, std::uint32_t generation, const StereoGains& gains);
    /// Fades the voice out over the next Mix block; it ends then.
    void Stop(std::uint32_t slot, std::uint32_t generation);

    /// Overwrites `out` (interleaved stereo, out.size() / 2 frames) with the mix times `masterVolume`, clamped to
    /// [-1, 1]. Voices that end in this block are appended to `ended` (at most one per slot, so it never allocates
    /// while ended has room for VoiceCount() more).
    void Mix(std::span<float> out, float masterVolume, std::vector<VoiceEnd>& ended);

    [[nodiscard]] std::uint32_t VoiceCount() const noexcept { return static_cast<std::uint32_t>(m_voices.size()); }
    [[nodiscard]] std::uint32_t PlayingCount() const noexcept;

private:
    struct Voice
    {
        const SoundClip* clip = nullptr; ///< Null = idle.
        std::uint32_t generation = 0;
        std::size_t position = 0;
        StereoGains current;
        StereoGains target;
        bool loop = false;
        bool stopping = false;
    };

    std::vector<Voice> m_voices;
};

} // namespace interi::audio
