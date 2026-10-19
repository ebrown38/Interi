# SPDX-License-Identifier: MIT
# Game export: cooks a project and its modules into a self-contained package folder (project content, module folders
# and libraries, cooked data, launchers, a hash manifest) and verifies one. No windowing or rendering here: the
# `interi_export` tool and the `interi_player` runtime (tools/player) use it.
set(_tests ${PROJECT_SOURCE_DIR}/tests/exporter)

interi_add_module(exporter
    SOURCES
        src/HeaderCheck.cpp
        src/ProjectExport.cpp
    DEPS interi::game interi::scene interi::values interi::sdk interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/ProjectExportTests.cpp)
target_compile_definitions(interi_exporter_tests PRIVATE
    INTERI_STARTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/samples/modules/starter")
