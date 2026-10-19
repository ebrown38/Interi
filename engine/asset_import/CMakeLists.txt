# SPDX-License-Identifier: MIT
# Asset import: models (glTF on cgltf, OBJ on tinyobjloader, FBX on ufbx), images (stb_image), meshoptimizer, and the
# asset database that cooks sources into the formats of interi::assets. Editor-only (static): the importers and their
# third-party code never reach a game, which loads the cooked files.
set(_tests ${PROJECT_SOURCE_DIR}/tests/asset_import)

find_package(tinyobjloader CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(ufbx CONFIG REQUIRED)
find_path(INTERI_CGLTF_INCLUDE_DIR cgltf.h REQUIRED)
find_path(INTERI_STB_INCLUDE_DIR stb_image.h REQUIRED)

interi_add_module(asset_import
    SOURCES
        src/AssetDatabase.cpp
        src/CgltfImplementation.cpp
        src/FbxImport.cpp
        src/GltfImport.cpp
        src/HeaderCheck.cpp
        src/ImportUtil.cpp
        src/ModelImport.cpp
        src/ModelManifest.cpp
        src/ModelProcess.cpp
        src/ObjImport.cpp
        src/StbImplementation.cpp
        src/TextureImport.cpp
    DEPS interi::assets interi::scene interi::hotreload interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json tinyobjloader::tinyobjloader meshoptimizer::meshoptimizer ufbx::ufbx
    TEST_DEPS interi::testing nlohmann_json::nlohmann_json
    TEST_SOURCES
        ${_tests}/AssetDatabaseTests.cpp
        ${_tests}/ModelImportTests.cpp
        ${_tests}/TextureImportTests.cpp)
# Third-party headers are system includes, so the engine's warnings and clang-tidy skip them.
target_include_directories(interi_asset_import SYSTEM PRIVATE ${INTERI_CGLTF_INCLUDE_DIR} ${INTERI_STB_INCLUDE_DIR})
