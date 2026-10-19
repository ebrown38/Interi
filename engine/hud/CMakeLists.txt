# SPDX-License-Identifier: MIT
# HUD and menus: retained widgets (text, bar, image, button, list) in .hud.json documents, bound to Values, laid out
# into a draw list that any renderer (the ImGui layer, a game's own) draws, plus buttons, focus and a menu stack.
# No rendering and no windowing here: everything is testable headless.
set(_tests ${PROJECT_SOURCE_DIR}/tests/hud)

interi_add_module(hud SHARED
    SOURCES
        src/HeaderCheck.cpp
        src/HudController.cpp
        src/HudDocument.cpp
        src/HudLayout.cpp
        src/MenuStack.cpp
    DEPS interi::loc interi::chain interi::values interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/HudControllerTests.cpp
        ${_tests}/HudDocumentTests.cpp
        ${_tests}/HudLayoutTests.cpp
        ${_tests}/MenuStackTests.cpp)
