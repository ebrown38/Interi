# SPDX-License-Identifier: MIT
# Localization: string tables per language (.strings.json), a language switch with a fallback, and the lookup the HUD
# binds `{@key}` text to. No UI and no windowing: everything is testable headless.
set(_tests ${PROJECT_SOURCE_DIR}/tests/loc)

interi_add_module(loc SHARED
    SOURCES
        src/HeaderCheck.cpp
        src/Localization.cpp
    DEPS interi::sdk interi::platform interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/LocalizationTests.cpp)
