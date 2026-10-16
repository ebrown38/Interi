// SPDX-License-Identifier: MIT
#pragma once

#include <interi/audio/SoundClip.h>
#include <interi/audio/Spatializer.h>
#include <interi/core/Result.h>
#include <interi/core/math/Vec.h>

#include <cstdint>
#include <memory>
#include <span>

namespace interi::audio
{

enum class AudioBackend : std::uint8_t
{
    Default, ///< The platform's audio device (WASAPI, PulseAudio, ALSA, ...).
    Null,    ///< miniaudio's null device: a real audio thread, no sound card (CI, headless runs).
    Offline, ///< No device and no audio thread: the caller pulls samples with Render (tests, recording).
};

struct AudioEngineDesc
{
    AudioBackend backend = AudioBackend::Default;
    std::uint32_t sampleRate = 48000;
    std::uint32_t maxVoices = 64;
    /// Commands (play, gain, stop) that can wait for the audio thread; a Play that finds the queue full is dropped.
    std::uint32_t commandCapacity = 1024;
    std::uint32_t periodFrames = 0; ///< Device buffer size; 0 = the backend's default.
};

/// A playing voice. Stale ids (the voice ended, or its slot was taken over) are ignored by every call.
struct VoiceId
{
    std::uint32_t slot = 0;
    std::uint32_t generation = 0; ///< 0 = invalid.

    [[nodiscard]] constexpr bool IsValid() const noexcept { return generation != 0; }
    friend constexpr bool operator==(const VoiceId&, const VoiceId&) = default;
};

struct PlayParams
{
    Vec3 position;
    bool spatial = true; ///< false: centred at `volume`, no distance attenuation (UI, music).
    float volume = 1.0f;
    /// Importance when voices run out; the limiter ranks by priority x audible gain, so a quiet distant voice of the
    /// same priority gives way first.
    float priority = 1.0f;
    AttenuationCurve attenuation;
    bool loop = false;
};

struct AudioStats
{
    std::uint32_t playing = 0;  ///< Live voices (game-side view).
    std::uint64_t started = 0;  ///< Plays that got a voice.
    std::uint64_t culled = 0;   ///< Plays that were inaudible (beyond maxDistance or zero volume).
    std::uint64_t rejected = 0; ///< Plays that ranked below every live voice.
    std::uint64_t evicted = 0;  ///< Live voices cut for a higher-ranked play.
    std::uint64_t dropped = 0;  ///< Plays lost to a full command queue.
};

/// The audio engine: miniaudio drives the device; mixing, 3D spatialization and voice limiting are the engine's own
/// (Mixer, Spatialize, VoiceLimiter).
///
/// Threading: every member function is called from one game thread. The device's audio thread only drains a
/// lock-free command queue and mixes; it reports ended voices through a second queue that Play and Update drain.
/// Clips are shared: the engine keeps a playing clip alive until the audio thread has reported its voice ended, so
/// clips are never freed on the audio thread and callers may drop theirs at any time.
class AudioEngine
{
public:
    /// Opens the device (Default, Null) and starts the audio thread. Unsupported if no device could be opened.
    [[nodiscard]] static Result<std::unique_ptr<AudioEngine>> Create(const AudioEngineDesc& desc = {});
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    AudioEngine& operator=(AudioEngine&&) = delete;

    /// Starts a voice, or returns an invalid id when it is culled, rejected or dropped (see AudioStats). The clip
    /// must have the engine's sample rate (asserted; invalid id otherwise).
    VoiceId Play(std::shared_ptr<const SoundClip> clip, const PlayParams& params);
    /// Fades the voice out; it stays IsPlaying until the audio thread reports it ended.
    void Stop(VoiceId voice);
    void SetPosition(VoiceId voice, const Vec3& position);
    void SetVolume(VoiceId voice, float volume);
    [[nodiscard]] bool IsPlaying(VoiceId voice) const noexcept;

    void SetListener(const Listener& listener) noexcept;
    [[nodiscard]] const Listener& GetListener() const noexcept;
    void SetMasterVolume(float volume) noexcept;

    /// Once per frame: frees ended voices, re-spatializes live ones for the current listener and positions, and
    /// re-ranks them in the limiter.
    void Update();

    /// Offline backend only: mixes the next out.size() / 2 stereo frames into `out` on the calling thread.
    void Render(std::span<float> out);

    [[nodiscard]] std::uint32_t SampleRate() const noexcept;
    [[nodiscard]] AudioBackend Backend() const noexcept;
    [[nodiscard]] AudioStats Stats() const noexcept;

private:
    struct Impl;
    explicit AudioEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};

} // namespace interi::audio
