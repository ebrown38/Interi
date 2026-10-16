// SPDX-License-Identifier: MIT
#include <interi/audio/SoundClip.h>
#include <interi/platform/FileSystem.h>

#include "Miniaudio.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace interi::audio
{

Result<SoundClip> DecodeClip(std::span<const std::byte> encoded, std::uint32_t sampleRate)
{
    const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, sampleRate);
    ma_decoder decoder;
    if (ma_decoder_init_memory(encoded.data(), encoded.size(), &config, &decoder) != MA_SUCCESS)
    {
        return MakeError(ErrorCode::InvalidArgument, "not a WAV, FLAC or MP3 file");
    }
    SoundClip clip;
    clip.sampleRate = sampleRate;
    constexpr ma_uint64 kChunk = 4096;
    for (;;)
    {
        const std::size_t offset = clip.samples.size();
        clip.samples.resize(offset + kChunk);
        ma_uint64 read = 0;
        const ma_result result = ma_decoder_read_pcm_frames(&decoder, clip.samples.data() + offset, kChunk, &read);
        clip.samples.resize(offset + static_cast<std::size_t>(read));
        if (result != MA_SUCCESS || read < kChunk)
        {
            break;
        }
    }
    ma_decoder_uninit(&decoder);
    if (clip.samples.empty())
    {
        return MakeError(ErrorCode::InvalidArgument, "sound file holds no samples");
    }
    return clip;
}

Result<SoundClip> LoadClip(const IFileSystem& files, std::string_view path, std::uint32_t sampleRate)
{
    INTERI_TRY_ASSIGN(bytes, files.ReadFile(path));
    auto clip = DecodeClip(bytes, sampleRate);
    if (!clip)
    {
        return MakeError(clip.error().code, std::format("'{}': {}", path, clip.error().message));
    }
    return clip;
}

std::vector<std::byte> EncodeWav16(const SoundClip& clip)
{
    std::vector<std::byte> out;
    const auto put = [&](std::uint32_t value, int bytes)
    {
        for (int i = 0; i < bytes; ++i)
        {
            out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xffu));
        }
    };
    const auto tag = [&](const char (&text)[5])
    {
        for (int i = 0; i < 4; ++i)
        {
            out.push_back(static_cast<std::byte>(text[i]));
        }
    };
    const auto dataBytes = static_cast<std::uint32_t>(clip.samples.size() * 2);
    tag("RIFF");
    put(36 + dataBytes, 4);
    tag("WAVE");
    tag("fmt ");
    put(16, 4);
    put(1, 2); // PCM
    put(1, 2); // mono
    put(clip.sampleRate, 4);
    put(clip.sampleRate * 2, 4); // byte rate
    put(2, 2);                   // block align
    put(16, 2);                  // bits per sample
    tag("data");
    put(dataBytes, 4);
    for (const float sample : clip.samples)
    {
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
        put(static_cast<std::uint16_t>(value), 2);
    }
    return out;
}

} // namespace interi::audio
