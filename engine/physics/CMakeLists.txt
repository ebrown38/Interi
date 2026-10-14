# SPDX-License-Identifier: MIT
# Jolt physics driven by the engine: world, layers, ECS sync, contact events and queries.
set(_tests ${PROJECT_SOURCE_DIR}/tests/physics)

interi_add_module(physics
    SOURCES
        src/HeaderCheck.cpp
        src/JoltRuntime.cpp
    DEPS interi::core interi::jobs_jolt interi::ecs Jolt::Jolt
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/PhysicsTestEnvironment.cpp
        ${_tests}/JoltMathTests.cpp
        ${_tests}/JoltRuntimeTests.cpp)
