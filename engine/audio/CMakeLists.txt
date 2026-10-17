# SPDX-License-Identifier: MIT
# Audio: miniaudio drives the device and decodes files; mixing, 3D spatialization and voice limiting are our own.
set(_tests ${PROJECT_SOURCE_DIR}/tests/audio)

find_path(INTERI_MINIAUDIO_INCLUDE_DIR miniaudio.h REQUIRED)
find_package(Threads REQUIRED)

interi_add_module(audio SHARED
    SOURCES
        src/AudioEngine.cpp
        src/HeaderCheck.cpp
        src/MiniaudioImplementation.cpp
        src/Mixer.cpp
        src/SoundClip.cpp
        src/Spatializer.cpp
        src/Synth.cpp
        src/VoiceLimiter.cpp
    DEPS interi::core interi::platform
    # miniaudio loads the system audio libraries (ALSA, PulseAudio, ...) at run time, so nothing else is linked.
    PRIVATE_DEPS Threads::Threads ${CMAKE_DL_LIBS} $<$<PLATFORM_ID:Linux>:m>
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/AudioEngineTests.cpp
        ${_tests}/MixerTests.cpp
        ${_tests}/SoundClipTests.cpp
        ${_tests}/SpatializerTests.cpp
        ${_tests}/SynthTests.cpp
        ${_tests}/VoiceLimiterTests.cpp)
target_include_directories(interi_audio SYSTEM PRIVATE ${INTERI_MINIAUDIO_INCLUDE_DIR})

# The audio thread against the game thread on miniaudio's null device (label `stress`; run under TSan).
interi_add_test_executable(interi_audio_stress_tests
    SOURCES ${_tests}/AudioEngineStressTests.cpp
    DEPS interi::audio interi::testing
    LABELS stress)
