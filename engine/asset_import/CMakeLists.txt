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
# stb_image copies zero bytes from/to a null pointer (an empty PNG chunk): harmless, but UBSan's nonnull-attribute check
# aborts a fuzz run on it. Only this file, only that check.
set_source_files_properties(src/StbImplementation.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:Clang>:-fno-sanitize=nonnull-attribute>")
# Third-party headers are system includes, so the engine's warnings and clang-tidy skip them.
target_include_directories(interi_asset_import SYSTEM PRIVATE ${INTERI_CGLTF_INCLUDE_DIR} ${INTERI_STB_INCLUDE_DIR})

# Fuzzing: every parser that reads a file from outside. The deterministic mutation test runs everywhere (label `stress`).
set(_fuzz_sources ${_tests}/fuzz/FuzzTargets.cpp)
interi_add_test_executable(interi_asset_import_stress_tests
    SOURCES ${_tests}/fuzz/FuzzStressTests.cpp ${_fuzz_sources}
    DEPS interi::asset_import interi::testing nlohmann_json::nlohmann_json
    LABELS stress)
target_include_directories(interi_asset_import_stress_tests PRIVATE ${_tests})

# The libFuzzer harness (Clang, not under TSan, which libFuzzer doesn't combine with): built where it links, and smoke-run
# on the committed corpus (label `stress`) so it keeps working.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT INTERI_SANITIZER STREQUAL "tsan")
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=fuzzer")
    check_cxx_source_compiles("#include <cstddef>\n#include <cstdint>\nextern \"C\" int LLVMFuzzerTestOneInput(const std::uint8_t*, std::size_t) { return 0; }" INTERI_HAVE_LIBFUZZER)
    unset(CMAKE_REQUIRED_FLAGS)
    if(INTERI_HAVE_LIBFUZZER)
        add_executable(interi_import_fuzzer ${_tests}/fuzz/ImportFuzzer.cpp ${_fuzz_sources})
        target_link_libraries(interi_import_fuzzer PRIVATE interi::asset_import nlohmann_json::nlohmann_json interi::warnings)
        target_include_directories(interi_import_fuzzer PRIVATE ${_tests})
        target_compile_options(interi_import_fuzzer PRIVATE -fsanitize=fuzzer-no-link)
        target_link_options(interi_import_fuzzer PRIVATE -fsanitize=fuzzer)
        # libFuzzer writes the inputs it finds into the first directory: keep that in the build tree.
        file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/fuzz-smoke)
        add_test(NAME ImportFuzzer_CorpusSmoke
            COMMAND interi_import_fuzzer ${CMAKE_CURRENT_BINARY_DIR}/fuzz-smoke ${_tests}/fuzz/corpus
                    -runs=2000 -seed=1 -max_len=65536 -timeout=20)
        set_tests_properties(ImportFuzzer_CorpusSmoke PROPERTIES LABELS stress)
    endif()
endif()
