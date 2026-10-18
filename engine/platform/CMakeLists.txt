# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/platform)

interi_add_module(platform SHARED
    SOURCES
        src/AsyncFileReader.cpp
        src/DynamicLibrary.cpp
        src/FileSystem.cpp
        src/HeaderCheck.cpp
        src/InputConfig.cpp
        src/InputMapper.cpp
    DEPS interi::core interi::jobs
    PRIVATE_DEPS ${CMAKE_DL_LIBS} nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/AsyncFileReaderTests.cpp
        ${_tests}/DynamicLibraryTests.cpp
        ${_tests}/FileSystemTests.cpp
        ${_tests}/InputConfigTests.cpp
        ${_tests}/InputMapperTests.cpp)

# A real shared library for DynamicLibraryTests to load; its path is baked into the test executable.
add_library(interi_test_dynlib MODULE ${_tests}/dynlib/TestDynLib.cpp)
target_link_libraries(interi_test_dynlib PRIVATE interi::warnings)
add_dependencies(interi_platform_tests interi_test_dynlib)
target_compile_definitions(interi_platform_tests PRIVATE
    INTERI_TEST_DYNLIB_PATH="$<TARGET_FILE:interi_test_dynlib>")
target_compile_definitions(interi_platform_tests PRIVATE
    INTERI_TEST_INPUT_FILE="${PROJECT_SOURCE_DIR}/data/gameplay/input.json")
