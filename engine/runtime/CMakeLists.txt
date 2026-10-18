# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/runtime)

interi_add_module(runtime SHARED
    SOURCES
        src/CameraRig.cpp
        src/FixedStepClock.cpp
        src/FollowCamera.cpp
        src/SaveGame.cpp
        src/TriggerVolumes.cpp
        src/HeaderCheck.cpp
    DEPS interi::core interi::jobs interi::platform
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CameraRigTests.cpp
        ${_tests}/FixedStepClockTests.cpp
        ${_tests}/FollowCameraTests.cpp
        ${_tests}/SaveGameTests.cpp
        ${_tests}/TriggerVolumesTests.cpp
        ${_tests}/FramePipelineTests.cpp)

# Concurrency stress (run under the tsan preset): label `stress`.
interi_add_test_executable(interi_runtime_stress_tests
    SOURCES ${_tests}/stress/FramePipelineStressTests.cpp
    DEPS interi::runtime interi::testing
    LABELS stress)
