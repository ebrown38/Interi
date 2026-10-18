# SPDX-License-Identifier: MIT
# Scenes: the object hierarchy (stable ids, names, parents, local transforms and their propagation to world
# transforms), the scene v2 file format, parametric primitives and mesh assets. Runtime-safe: games load scenes too.
set(_tests ${PROJECT_SOURCE_DIR}/tests/scene)

interi_add_module(scene SHARED
    SOURCES
        src/HeaderCheck.cpp
        src/Hierarchy.cpp
        src/Mesh.cpp
        src/Prefab.cpp
        src/Primitives.cpp
        src/SceneComponents.cpp
        src/SceneFile.cpp
        src/SceneRuntime.cpp
    DEPS interi::chain interi::values interi::ecs interi::jobs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/HierarchyTests.cpp
        ${_tests}/PrefabTests.cpp
        ${_tests}/PrimitivesTests.cpp
        ${_tests}/SceneFileTests.cpp
        ${_tests}/SceneRuntimeTests.cpp)
