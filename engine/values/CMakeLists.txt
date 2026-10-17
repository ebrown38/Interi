# SPDX-License-Identifier: MIT
# Values: the typed data layer shared by the editor and (from M15) Chain Reaction. Value types and schemas, component
# reflection, module Values with project overrides, character attributes and data tables.
set(_tests ${PROJECT_SOURCE_DIR}/tests/values)

interi_add_module(values SHARED
    SOURCES
        src/Characters.cpp
        src/DataRegistry.cpp
        src/DataTables.cpp
        src/HeaderCheck.cpp
        src/JsonSchema.cpp
        src/ModuleContent.cpp
        src/ProjectData.cpp
        src/Reflection.cpp
        src/Value.cpp
        src/ValueRegistry.cpp
    DEPS interi::sdk interi::ecs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CharacterTests.cpp
        ${_tests}/DataTableTests.cpp
        ${_tests}/ModuleContentTests.cpp
        ${_tests}/ProjectDataTests.cpp
        ${_tests}/ReflectionTests.cpp
        ${_tests}/ValueRegistryTests.cpp
        ${_tests}/ValueTests.cpp)

# The shared SDK test project's manifests (tests/sdk/modules) carry data sections.
target_compile_definitions(interi_values_tests PRIVATE INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules")
