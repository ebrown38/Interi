# SPDX-License-Identifier: MIT
# Destruction sounds: impacts by material pair, batched per region; stress creaks; collapse rumble; explosions.
set(_tests ${PROJECT_SOURCE_DIR}/tests/destruction_audio)

interi_add_module(destruction_audio
    SOURCES
        src/AudioConfig.cpp
        src/DestructionAudio.cpp
        src/HeaderCheck.cpp
        src/ImpactBatcher.cpp
        src/StructureAudio.cpp
    DEPS interi::audio interi::destruction_runtime interi::physics interi::ecs
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing interi::physics_testing
    TEST_SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/AudioConfigTests.cpp
        ${_tests}/DestructionAudioTests.cpp
        ${_tests}/ImpactBatcherTests.cpp
        ${_tests}/StructureAudioTests.cpp)
target_compile_definitions(interi_destruction_audio_tests PRIVATE
    INTERI_TEST_DESTRUCTION_AUDIO_FILE="${PROJECT_SOURCE_DIR}/data/audio/destruction_audio.json")
