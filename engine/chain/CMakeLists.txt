# SPDX-License-Identifier: MIT
# Chain Reaction: the visual programming runtime. Chains (.chain.json) of events and conditional links compile against
# the project's registries (so module overrides apply) and run deterministically on the fixed step.
set(_tests ${PROJECT_SOURCE_DIR}/tests/chain)

interi_add_module(chain SHARED
    SOURCES
        src/ChainAsset.cpp
        src/CoreLibrary.cpp
        src/Event.cpp
        src/Expression.cpp
        src/Functions.cpp
        src/HeaderCheck.cpp
    DEPS interi::values interi::sdk interi::ecs interi::jobs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/ChainAssetTests.cpp
        ${_tests}/EventTests.cpp
        ${_tests}/ExpressionTests.cpp)
