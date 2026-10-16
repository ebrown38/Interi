// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace interi
{
class IFileSystem;
}

namespace interi::audio
{

/// Mono 32-bit float PCM, played at its own sample rate (the AudioEngine only plays clips at its rate).
struct SoundClip
{
    std::uint32_t sampleRate = 48000;
    std::vector<float> samples;

    [[nodiscard]] float Duration() const noexcept
    {
        return static_cast<float>(samples.size()) / static_cast<float>(sampleRate);
    }
};

/// Decodes a WAV, FLAC or MP3 file held in memory (miniaudio), downmixed to mono and resampled to `sampleRate`.
/// InvalidArgument for data that is not one of these formats or holds no samples.
[[nodiscard]] Result<SoundClip> DecodeClip(std::span<const std::byte> encoded, std::uint32_t sampleRate);

/// Reads and decodes a sound file through the engine's file system.
[[nodiscard]] Result<SoundClip> LoadClip(const IFileSystem& files, std::string_view path, std::uint32_t sampleRate);

/// Encodes a clip as a 16-bit PCM mono WAV file (tests, tools).
[[nodiscard]] std::vector<std::byte> EncodeWav16(const SoundClip& clip);

} // namespace interi::audio
