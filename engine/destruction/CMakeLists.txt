# SPDX-License-Identifier: MIT
# Destruction data and offline fracture: materials, mesh cutting, Voronoi, chunk hierarchy, bonds, .idest assets.
set(_tests ${PROJECT_SOURCE_DIR}/tests/destruction)

interi_add_module(destruction
    SOURCES
        src/HeaderCheck.cpp
        src/Material.cpp
    DEPS interi::core interi::jobs
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/MaterialTests.cpp)

target_compile_definitions(interi_destruction_tests PRIVATE
    INTERI_TEST_MATERIALS_FILE="${PROJECT_SOURCE_DIR}/data/destruction/materials.json")
