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

# Whole collapses on Jolt (seconds of simulated time each): label `integration`.
interi_add_test_executable(interi_destruction_runtime_scenario_tests
    SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/ScenarioTests.cpp
    DEPS interi::destruction_runtime interi::testing interi::physics_testing
    LABELS integration)

# Collapse determinism across worker counts and platforms: label `stress`.
interi_add_test_executable(interi_destruction_runtime_stress_tests
    SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/stress/CollapseDeterminismTests.cpp
    DEPS interi::destruction_runtime interi::testing interi::physics_testing
    LABELS stress)
