# SPDX-License-Identifier: MIT
# Animation: skeletons and clips (JSON assets), sampling, blending and skinning matrices, animation state graphs per
# archetype and the Chain Reaction events (anim.Play, anim.Trigger) that drive them. Pure CPU, tested headless.
set(_tests ${PROJECT_SOURCE_DIR}/tests/anim)

interi_add_module(anim SHARED
    SOURCES
        src/AnimEvents.cpp
        src/Animation.cpp
        src/Animator.cpp
        src/HeaderCheck.cpp
    DEPS interi::chain interi::ecs interi::sdk interi::values interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/AnimationTests.cpp
        ${_tests}/AnimatorTests.cpp)
