# SPDX-License-Identifier: MIT
# Gameplay on top of physics and destruction: the character, weapons and tools, levels, saving destruction state.
set(_tests ${PROJECT_SOURCE_DIR}/tests/gameplay)

interi_add_module(gameplay
    SOURCES
        src/Character.cpp
        src/FollowCamera.cpp
        src/HeaderCheck.cpp
    DEPS interi::destruction_runtime interi::physics interi::ecs interi::platform
    TEST_DEPS interi::testing interi::physics_testing
    TEST_SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/CharacterTests.cpp
        ${_tests}/FollowCameraTests.cpp)
