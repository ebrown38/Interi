// SPDX-License-Identifier: MIT
// Compiles every public audio header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy.
#include <interi/audio/AudioEngine.h>
#include <interi/audio/Mixer.h>
#include <interi/audio/SoundClip.h>
#include <interi/audio/Spatializer.h>
#include <interi/audio/Synth.h>
#include <interi/audio/VoiceLimiter.h>
