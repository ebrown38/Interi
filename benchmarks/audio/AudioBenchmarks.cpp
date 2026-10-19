// SPDX-License-Identifier: MIT
// The audio hot path: the mixer (audio thread).
#include <interi/audio/Mixer.h>
#include <interi/core/Random.h>

#include <benchmark/benchmark.h>

#include <vector>

namespace
{

/// A 512-frame stereo block (~10.7 ms at 48 kHz) with `voices` playing.
void BM_Mixer_Block(benchmark::State& state)
{
    const auto voices = static_cast<std::uint32_t>(state.range(0));
    interi::audio::SoundClip clip;
    clip.samples.assign(48000, 0.1f);
    interi::audio::Mixer mixer(voices);
    for (std::uint32_t slot = 0; slot < voices; ++slot)
    {
        (void)mixer.Start(slot, slot + 1, &clip, {.left = 0.5f, .right = 0.3f}, true);
    }
    std::vector<float> out(2 * 512);
    std::vector<interi::audio::VoiceEnd> ended;
    ended.reserve(voices);
    for (auto _ : state)
    {
        for (std::uint32_t slot = 0; slot < voices; ++slot)
        {
            mixer.SetGains(slot, slot + 1, {.left = 0.4f, .right = 0.4f}); // every voice ramps
        }
        mixer.Mix(out, 1.0f, ended);
        benchmark::DoNotOptimize(out.data());
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(voices) * 512);
}
BENCHMARK(BM_Mixer_Block)->Arg(16)->Arg(64)->Arg(256);

} // namespace
