// SPDX-License-Identifier: MIT
#pragma once

#include <interi/audio/SoundClip.h>

#include <cstdint>

namespace interi::audio
{

enum class SynthKind : std::uint8_t
{
    Impact,    ///< A noise burst with a fast decay, plus an optional ringing tone (metal, glass).
    Explosion, ///< A broadband blast with a low thump and a long tail.
    Rumble,    ///< Low, slowly swelling noise: a structure coming down.
    Creak,     ///< Stick-slip friction pulses exciting a resonance: a structure under strain.
};

/// A procedurally generated sound. The sandbox's sounds are made this way, so no recorded audio with its own
/// licence has to ship. Noise comes from the integer Random (seeded), so a desc always gives the same samples.
struct SynthDesc
{
    SynthKind kind = SynthKind::Impact;
    float duration = 0.5f;   ///< s (> 0).
    float brightness = 0.5f; ///< 0 (dull) .. 1 (bright): the noise low-pass cutoff.
    float pitch = 0.0f;      ///< Hz of the ringing tone (Impact), or the resonance (Creak); 0 = none / default.
    float decay = 10.0f;     ///< 1/s: the envelope's exponential decay rate (Impact, Explosion).
    float volume = 0.8f;     ///< Peak amplitude of the result.
    std::uint64_t seed = 1;
};

/// Renders a sound at `sampleRate`, normalised so its peak is `volume`.
[[nodiscard]] SoundClip Synthesize(const SynthDesc& desc, std::uint32_t sampleRate);

} // namespace interi::audio
