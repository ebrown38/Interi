# SPDX-License-Identifier: MIT
# Navigation: navmesh bake (Recast) and queries (Detour), navigation agents and the Chain Reaction events and triggers
# that let AI behaviours find paths and walk. Recast/Detour stay private: no public header includes them.
set(_tests ${PROJECT_SOURCE_DIR}/tests/nav)

find_package(recastnavigation CONFIG REQUIRED)

interi_add_module(nav SHARED
    SOURCES
        src/HeaderCheck.cpp
        src/NavAgents.cpp
        src/NavEvents.cpp
        src/NavMesh.cpp
    DEPS interi::chain interi::ecs interi::sdk interi::values interi::core
    PRIVATE_DEPS RecastNavigation::Recast RecastNavigation::Detour
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/NavAgentTests.cpp
        ${_tests}/NavMeshTests.cpp)
