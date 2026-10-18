# SPDX-License-Identifier: MIT
# Jolt physics driven by the engine: world, layers, ECS sync, contact events and queries.
set(_tests ${PROJECT_SOURCE_DIR}/tests/physics)

# Sanitizer settings for executables that run Jolt simulations (tests and benchmarks).
add_library(interi_physics_testing INTERFACE)
add_library(interi::physics_testing ALIAS interi_physics_testing)
target_sources(interi_physics_testing INTERFACE ${_tests}/JoltSanitizerSupport.cpp)
# Executables that also link interi::vulkan_testing get this suppression in its (single) list.
target_compile_definitions(interi_physics_testing INTERFACE INTERI_EXTRA_TSAN_SUPPRESSIONS="race:JPH::Mutex::try_lock")

interi_add_module(physics
    SOURCES
        src/Components.cpp
        src/ContactEvents.cpp
        src/HeaderCheck.cpp
        src/JoltRuntime.cpp
        src/PhysicsLayers.cpp
        src/PhysicsSync.cpp
        src/PhysicsWorld.cpp
        src/Queries.cpp
    DEPS interi::core interi::jobs_jolt interi::ecs Jolt::Jolt
    TEST_DEPS interi::testing interi::physics_testing
    TEST_SOURCES
        ${_tests}/PhysicsTestEnvironment.cpp
        ${_tests}/JoltMathTests.cpp
        ${_tests}/JoltRuntimeTests.cpp
        ${_tests}/ColliderTests.cpp
        ${_tests}/ContactEventTests.cpp
        ${_tests}/PhysicsLayersTests.cpp
        ${_tests}/PhysicsSyncTests.cpp
        ${_tests}/PhysicsWorldTests.cpp
        ${_tests}/QueryTests.cpp)

# Whole simulations compared across thread counts (seconds each): label `stress`.
interi_add_test_executable(interi_physics_stress_tests
    SOURCES
        ${_tests}/PhysicsTestEnvironment.cpp
        ${_tests}/stress/ContactEventStressTests.cpp
        ${_tests}/stress/PhysicsDeterminismTests.cpp
        ${_tests}/stress/PhysicsSyncDeterminismTests.cpp
    DEPS interi::physics interi::testing interi::physics_testing
    LABELS stress)
