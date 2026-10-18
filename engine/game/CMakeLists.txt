# SPDX-License-Identifier: MIT
# A game as a project: its modules (hot-reloadable libraries and their data), the startup scene running in a
# SceneRuntime on the modules' World, input actions, and save slots. The editor reuses the module/data part for
# play-in-editor; a shipped game and CI's headless validation use GameSession.
set(_tests ${PROJECT_SOURCE_DIR}/tests/game)

interi_add_module(game
    SOURCES
        src/GameSession.cpp
        src/HeaderCheck.cpp
        src/ProjectModules.cpp
    DEPS interi::scene interi::hotreload interi::runtime interi::sdk interi::values interi::chain interi::ecs
         interi::jobs interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/GameSessionTests.cpp)
target_compile_definitions(interi_game_tests PRIVATE
    INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules")
