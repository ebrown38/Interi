# SPDX-License-Identifier: MIT
# Runtime destructibles on Jolt bodies and ECS entities: damage, stress, splitting into falling pieces.
set(_tests ${PROJECT_SOURCE_DIR}/tests/destruction_runtime)

interi_add_module(destruction_runtime
    SOURCES
        src/DestructionWorld.cpp
        src/HeaderCheck.cpp
    DEPS interi::destruction interi::physics interi::ecs interi::platform
    TEST_DEPS interi::testing interi::physics_testing
    TEST_SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/DestructionWorldTests.cpp)
