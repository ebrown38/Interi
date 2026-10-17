# SPDX-License-Identifier: MIT
# Values: the typed data layer shared by the editor and (from M15) Chain Reaction. Value types and schemas, component
# reflection, module Values with project overrides, character attributes and data tables.
set(_tests ${PROJECT_SOURCE_DIR}/tests/values)

interi_add_module(values SHARED
    SOURCES
        src/HeaderCheck.cpp
        src/Reflection.cpp
        src/Value.cpp
    DEPS interi::sdk interi::ecs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/ReflectionTests.cpp
        ${_tests}/ValueTests.cpp)
