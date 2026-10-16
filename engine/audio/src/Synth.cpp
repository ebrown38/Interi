// SPDX-License-Identifier: MIT
#include <interi/audio/Synth.h>
#include <interi/core/Random.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace interi::audio
{

namespace
{

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

/// One-pole low-pass filter.
struct LowPass
{
    float coefficient = 1.0f;
    float state = 0.0f;

    float operator()(float x) noexcept
    {
        state += coefficient * (x - state);
        return state;
    }
};

/// Filter coefficient for a cutoff frequency.
float Coefficient(float cutoffHz, float sampleRate)
{
    return 1.0f - std::exp(-kTwoPi * cutoffHz / sampleRate);
}

float Attack(float t, float seconds)
{
    return t < seconds ? t / seconds : 1.0f;
}

} // namespace

SoundClip Synthesize(const SynthDesc& desc, std::uint32_t sampleRate)
{
    SoundClip clip;
    clip.sampleRate = sampleRate;
    const auto rate = static_cast<float>(sampleRate);
    const auto count = static_cast<std::size_t>(std::max(desc.duration, 0.0f) * rate);
    clip.samples.resize(count);
    Random random(desc.seed);
    const float brightness = std::clamp(desc.brightness, 0.0f, 1.0f);
    // Cutoff from 200 Hz (dull) to 12 kHz (bright), exponentially.
    LowPass noiseFilter{.coefficient = Coefficient(200.0f * std::pow(60.0f, brightness), rate)};
    LowPass secondFilter{.coefficient = Coefficient(120.0f, rate)};
    float pulsePhase = 0.0f;
    float resonance = 0.0f;
    float resonanceVelocity = 0.0f;
    const float creakPitch = desc.pitch > 0.0f ? desc.pitch : 420.0f;
    for (std::size_t i = 0; i < count; ++i)
    {
        const float t = static_cast<float>(i) / rate;
        const float remaining = desc.duration - t;
        const float release = std::clamp(remaining / 0.02f, 0.0f, 1.0f); // 20 ms fade at the end: no click
        const float noise = random.NextFloat(-1.0f, 1.0f);
        float sample = 0.0f;
        switch (desc.kind)
        {
        case SynthKind::Impact:
        {
            const float envelope = Attack(t, 0.002f) * std::exp(-desc.decay * t);
            sample = noiseFilter(noise) * envelope;
            if (desc.pitch > 0.0f)
            {
                sample += 0.6f * std::sin(kTwoPi * desc.pitch * t) * std::exp(-0.4f * desc.decay * t);
            }
            break;
        }
        case SynthKind::Explosion:
        {
            const float envelope = Attack(t, 0.005f) * std::exp(-desc.decay * t);
            const float thump = std::sin(kTwoPi * (45.0f + 40.0f * std::exp(-8.0f * t)) * t) * std::exp(-5.0f * t);
            sample = noiseFilter(noise) * envelope + 0.8f * thump;
            break;
        }
        case SynthKind::Rumble:
        {
            const float swell =
                Attack(t, 0.25f * desc.duration) * std::clamp(remaining / (0.5f * desc.duration), 0.0f, 1.0f);
            const float wobble = 0.7f + 0.3f * std::sin(kTwoPi * 3.0f * t);
            sample =
                secondFilter(noiseFilter(noise)) * 6.0f * swell * wobble + 0.3f * std::sin(kTwoPi * 32.0f * t) * swell;
            break;
        }
        case SynthKind::Creak:
        {
            // Stick-slip: a pulse train whose rate wanders between ~15 and ~60 Hz kicks a damped resonator.
            const float pulseRate = 37.0f + 22.0f * std::sin(kTwoPi * 0.7f * t + 1.3f * std::sin(kTwoPi * 0.23f * t));
            pulsePhase += pulseRate / rate;
            float kick = 0.0f;
            if (pulsePhase >= 1.0f)
            {
                pulsePhase -= 1.0f;
                kick = 0.5f + 0.5f * random.NextFloat01();
            }
            const float omega = kTwoPi * creakPitch * (1.0f + 0.05f * std::sin(kTwoPi * 0.5f * t));
            const float damping = 60.0f;
            resonanceVelocity += (kick * omega - omega * omega * resonance / rate - damping * resonanceVelocity / rate);
            resonance += resonanceVelocity / rate;
            const float envelope =
                Attack(t, 0.15f * desc.duration) * std::clamp(remaining / (0.4f * desc.duration), 0.0f, 1.0f);
            sample = (resonance * omega + 0.05f * noiseFilter(noise)) * envelope;
            break;
        }
        }
        clip.samples[i] = sample * release;
    }
    float peak = 0.0f;
    for (const float sample : clip.samples)
    {
        peak = std::max(peak, std::abs(sample));
    }
    if (peak > 0.0f)
    {
        const float scale = desc.volume / peak;
        for (float& sample : clip.samples)
        {
            sample *= scale;
        }
    }
    return clip;
}

} // namespace interi::audio
