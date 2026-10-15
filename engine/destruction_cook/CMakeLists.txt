# SPDX-License-Identifier: MIT
# Offline cooking of destructibles: glTF import (cgltf), sidecar fracture configs, meshoptimizer, .idest output.
# Tools and the editor link this; the runtime only needs interi::destruction.
set(_tests ${PROJECT_SOURCE_DIR}/tests/destruction_cook)

find_path(INTERI_CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)

interi_add_module(destruction_cook
    SOURCES
        src/CgltfImplementation.cpp
        src/CookConfig.cpp
        src/Cooker.cpp
        src/GltfImport.cpp
        src/HeaderCheck.cpp
    DEPS interi::destruction
    PRIVATE_DEPS meshoptimizer::meshoptimizer nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CookConfigTests.cpp
        ${_tests}/CookerTests.cpp
        ${_tests}/GltfImportTests.cpp)
target_include_directories(interi_destruction_cook SYSTEM PRIVATE ${INTERI_CGLTF_INCLUDE_DIR})

target_compile_definitions(interi_destruction_cook_tests PRIVATE
    INTERI_TEST_MATERIALS_FILE="${PROJECT_SOURCE_DIR}/data/destruction/materials.json")
