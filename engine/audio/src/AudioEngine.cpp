// SPDX-License-Identifier: MIT
#include <interi/audio/AudioEngine.h>
#include <interi/audio/Mixer.h>
#include <interi/audio/VoiceLimiter.h>
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/core/containers/SPSCQueue.h>

#include "Miniaudio.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <format>
#include <utility>
#include <vector>

namespace interi::audio
{

namespace
{

enum class CommandKind : std::uint8_t
{
    Start,
    Gains,
    Stop,
};

struct Command
{
    CommandKind kind = CommandKind::Start;
    bool loop = false;
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;
    const SoundClip* clip = nullptr;
    StereoGains gains;
};

/// Below this change a gain is not re-sent to the audio thread.
constexpr float kGainEpsilon = 1.0e-4f;

} // namespace

struct AudioEngine::Impl
{
    /// The game thread's view of a slot.
    struct Voice
    {
        std::shared_ptr<const SoundClip> clip; ///< Null = free.
        std::uint32_t generation = 0;
        PlayParams params;
        StereoGains sent; ///< Gains last sent to the audio thread.
    };

    /// A clip whose voice was cut or replaced on the game side but may still be read by the audio thread.
    struct Retired
    {
        VoiceEnd voice;
        std::shared_ptr<const SoundClip> clip;
    };

    explicit Impl(const AudioEngineDesc& d)
        : desc(d)
        , limiter(d.maxVoices)
        , voices(d.maxVoices)
        , commands(d.commandCapacity)
        // Between two drains (every Play and Update) at most one Start is queued per call, so each live voice and
        // each retired one ends once: twice the voices plus the queued commands always fits.
        , ends(2 * std::size_t{d.maxVoices} + d.commandCapacity)
        , mixer(d.maxVoices)
    {
        mixed.reserve(d.maxVoices);
    }

    [[nodiscard]] StereoGains GainsFor(const PlayParams& params) const
    {
        if (!params.spatial)
        {
            const StereoGains centre = EqualPowerPan(0.0f);
            return {.left = params.volume * centre.left, .right = params.volume * centre.right};
        }
        return Spatialize(listener, params.position, params.attenuation, params.volume);
    }

    [[nodiscard]] Voice* Find(VoiceId id)
    {
        if (!id.IsValid() || id.slot >= voices.size())
        {
            return nullptr;
        }
        Voice& voice = voices[id.slot];
        return voice.clip != nullptr && voice.generation == id.generation ? &voice : nullptr;
    }

    /// Game thread: frees the voices the audio thread has finished with.
    void DrainEnds()
    {
        VoiceEnd end;
        while (ends.TryPop(end))
        {
            Voice& voice = voices[end.slot];
            if (voice.clip != nullptr && voice.generation == end.generation)
            {
                voice.clip.reset();
                limiter.Release(end.slot);
                continue;
            }
            const auto it =
                std::ranges::find_if(retired, [&](const Retired& r)
                                     { return r.voice.slot == end.slot && r.voice.generation == end.generation; });
            if (it != retired.end())
            {
                *it = std::move(retired.back());
                retired.pop_back();
            }
        }
    }

    /// Audio thread (or Render's caller): applies queued commands, then mixes.
    void Mix(std::span<float> out)
    {
        Command command;
        while (commands.TryPop(command))
        {
            switch (command.kind)
            {
            case CommandKind::Start:
                if (const std::optional<VoiceEnd> cut =
                        mixer.Start(command.slot, command.generation, command.clip, command.gains, command.loop))
                {
                    PushEnd(*cut);
                }
                break;
            case CommandKind::Gains:
                mixer.SetGains(command.slot, command.generation, command.gains);
                break;
            case CommandKind::Stop:
                mixer.Stop(command.slot, command.generation);
                break;
            }
        }
        mixed.clear();
        mixer.Mix(out, masterVolume.load(std::memory_order_relaxed), mixed);
        for (const VoiceEnd& end : mixed)
        {
            PushEnd(end);
        }
    }

    void PushEnd(const VoiceEnd& end)
    {
        const bool pushed = ends.TryPush(end);
        INTERI_ASSERT(pushed); // sized so it cannot fill (see the constructor)
    }

    static void DeviceCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frames)
    {
        auto* self = static_cast<Impl*>(device->pUserData);
        self->Mix({static_cast<float*>(output), std::size_t{frames} * 2});
    }

    AudioEngineDesc desc;
    Listener listener;
    VoiceLimiter limiter;
    std::vector<Voice> voices;
    std::vector<Retired> retired;
    std::uint32_t nextGeneration = 1;
    AudioStats stats;

    SPSCQueue<Command> commands; ///< Game -> audio.
    SPSCQueue<VoiceEnd> ends;    ///< Audio -> game.
    std::atomic<float> masterVolume{1.0f};

    // Audio thread only.
    Mixer mixer;
    std::vector<VoiceEnd> mixed;

    // Zero-initialised C structs, as miniaudio expects before *_init.
    ma_context context{}; // NOLINT(bugprone-invalid-enum-default-initialization)
    ma_device device{};   // NOLINT(bugprone-invalid-enum-default-initialization)
    bool hasContext = false;
    bool hasDevice = false;
};

Result<std::unique_ptr<AudioEngine>> AudioEngine::Create(const AudioEngineDesc& desc)
{
    if (desc.maxVoices == 0 || desc.sampleRate == 0 || desc.commandCapacity == 0)
    {
        return MakeError(ErrorCode::InvalidArgument, "audio engine needs voices, a sample rate and a command queue");
    }
    auto impl = std::make_unique<Impl>(desc);
    if (desc.backend != AudioBackend::Offline)
    {
        const ma_backend nullBackend[] = {ma_backend_null};
        const bool useNull = desc.backend == AudioBackend::Null;
        if (ma_context_init(useNull ? nullBackend : nullptr, useNull ? 1u : 0u, nullptr, &impl->context) != MA_SUCCESS)
        {
            return MakeError(ErrorCode::Unsupported, "no audio backend could be initialised");
        }
        impl->hasContext = true;
        ma_device_config config = ma_device_config_init(ma_device_type_playback);
        config.playback.format = ma_format_f32;
        config.playback.channels = 2;
        config.sampleRate = desc.sampleRate;
        config.periodSizeInFrames = desc.periodFrames;
        config.dataCallback = &Impl::DeviceCallback;
        config.pUserData = impl.get();
        if (ma_device_init(&impl->context, &config, &impl->device) != MA_SUCCESS)
        {
            ma_context_uninit(&impl->context);
            impl->hasContext = false;
            return MakeError(ErrorCode::Unsupported, "no audio playback device could be opened");
        }
        impl->hasDevice = true;
        if (ma_device_start(&impl->device) != MA_SUCCESS)
        {
            ma_device_uninit(&impl->device);
            ma_context_uninit(&impl->context);
            impl->hasDevice = false;
            impl->hasContext = false;
            return MakeError(ErrorCode::Unsupported, "the audio playback device could not be started");
        }
        INTERI_LOG_INFO("Audio", "audio device '{}' ({}), {} Hz, {} voices", impl->device.playback.name,
                        ma_get_backend_name(impl->context.backend), desc.sampleRate, desc.maxVoices);
    }
    return std::unique_ptr<AudioEngine>(new AudioEngine(std::move(impl)));
}

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl))
{
}

AudioEngine::~AudioEngine()
{
    if (m_impl->hasDevice)
    {
        ma_device_uninit(&m_impl->device); // stops the audio thread: no callback runs after this
    }
    if (m_impl->hasContext)
    {
        ma_context_uninit(&m_impl->context);
    }
}

VoiceId AudioEngine::Play(std::shared_ptr<const SoundClip> clip, const PlayParams& params)
{
    Impl& impl = *m_impl;
    impl.DrainEnds();
    if (clip == nullptr || clip->samples.empty())
    {
        return {};
    }
    INTERI_ASSERT(clip->sampleRate == impl.desc.sampleRate);
    if (clip->sampleRate != impl.desc.sampleRate)
    {
        return {};
    }
    const StereoGains gains = impl.GainsFor(params);
    const float audible = gains.Max();
    if (!(audible > 0.0f))
    {
        ++impl.stats.culled;
        return {};
    }
    const VoiceAdmission admission = impl.limiter.Acquire(std::max(params.priority, 0.0f) * audible);
    if (!admission.slot)
    {
        ++impl.stats.rejected;
        return {};
    }
    const std::uint32_t slot = *admission.slot;
    Impl::Voice& voice = impl.voices[slot];
    if (admission.evicted)
    {
        // The Start below cuts it on the audio thread, which then reports it ended; keep its clip until then.
        impl.retired.push_back(
            {.voice = {.slot = slot, .generation = voice.generation}, .clip = std::move(voice.clip)});
        ++impl.stats.evicted;
    }
    const std::uint32_t generation = impl.nextGeneration++;
    if (impl.nextGeneration == 0)
    {
        impl.nextGeneration = 1;
    }
    if (!impl.commands.TryPush({.kind = CommandKind::Start,
                                .loop = params.loop,
                                .slot = slot,
                                .generation = generation,
                                .clip = clip.get(),
                                .gains = gains}))
    {
        impl.limiter.Release(slot);
        ++impl.stats.dropped;
        return {};
    }
    voice = {.clip = std::move(clip), .generation = generation, .params = params, .sent = gains};
    ++impl.stats.started;
    return {.slot = slot, .generation = generation};
}

void AudioEngine::Stop(VoiceId id)
{
    if (const Impl::Voice* voice = m_impl->Find(id))
    {
        (void)m_impl->commands.TryPush({.kind = CommandKind::Stop,
                                        .loop = false,
                                        .slot = id.slot,
                                        .generation = voice->generation,
                                        .clip = nullptr,
                                        .gains = {}});
    }
}

void AudioEngine::SetPosition(VoiceId id, const Vec3& position)
{
    if (Impl::Voice* voice = m_impl->Find(id))
    {
        voice->params.position = position;
    }
}

void AudioEngine::SetVolume(VoiceId id, float volume)
{
    if (Impl::Voice* voice = m_impl->Find(id))
    {
        voice->params.volume = volume;
    }
}

bool AudioEngine::IsPlaying(VoiceId id) const noexcept
{
    return m_impl->Find(id) != nullptr;
}

void AudioEngine::SetListener(const Listener& listener) noexcept
{
    m_impl->listener = listener;
}

const Listener& AudioEngine::GetListener() const noexcept
{
    return m_impl->listener;
}

void AudioEngine::SetMasterVolume(float volume) noexcept
{
    m_impl->masterVolume.store(std::max(volume, 0.0f), std::memory_order_relaxed);
}

void AudioEngine::Update()
{
    Impl& impl = *m_impl;
    impl.DrainEnds();
    for (std::uint32_t slot = 0; slot < impl.voices.size(); ++slot)
    {
        Impl::Voice& voice = impl.voices[slot];
        if (voice.clip == nullptr)
        {
            continue;
        }
        const StereoGains gains = impl.GainsFor(voice.params);
        impl.limiter.SetPriority(slot, std::max(voice.params.priority, 0.0f) * gains.Max());
        if (std::abs(gains.left - voice.sent.left) > kGainEpsilon ||
            std::abs(gains.right - voice.sent.right) > kGainEpsilon)
        {
            if (impl.commands.TryPush({.kind = CommandKind::Gains,
                                       .loop = false,
                                       .slot = slot,
                                       .generation = voice.generation,
                                       .clip = nullptr,
                                       .gains = gains}))
            {
                voice.sent = gains;
            }
        }
    }
}

void AudioEngine::Render(std::span<float> out)
{
    INTERI_ASSERT(m_impl->desc.backend == AudioBackend::Offline);
    if (m_impl->desc.backend == AudioBackend::Offline)
    {
        m_impl->Mix(out);
    }
}

std::uint32_t AudioEngine::SampleRate() const noexcept
{
    return m_impl->desc.sampleRate;
}

AudioBackend AudioEngine::Backend() const noexcept
{
    return m_impl->desc.backend;
}

AudioStats AudioEngine::Stats() const noexcept
{
    AudioStats stats = m_impl->stats;
    stats.playing = m_impl->limiter.LiveCount();
    return stats;
}

} // namespace interi::audio
