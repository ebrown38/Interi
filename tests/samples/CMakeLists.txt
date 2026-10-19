# SPDX-License-Identifier: MIT
# The committed starter sample (samples/modules/starter) played headless: its module loads, the "Coin Run" project's
# chains collect every coin, the HUD and menus lay out, input rebinds and a save round-trips.
if(NOT INTERI_BUILD_SHARED)
    return()
endif()

interi_add_test_executable(interi_starter_sample_tests
    SOURCES StarterSampleTests.cpp StarterSoakTests.cpp StarterExportTests.cpp
    DEPS interi::exporter interi::game interi::hud interi::runtime interi::scene interi::platform interi::testing
    LABELS integration)
add_dependencies(interi_starter_sample_tests com_interi_starter interi_player interi_export)
target_compile_definitions(interi_starter_sample_tests PRIVATE
    INTERI_PLAYER_EXECUTABLE="$<TARGET_FILE:interi_player>"
    INTERI_EXPORT_EXECUTABLE="$<TARGET_FILE:interi_export>"
    INTERI_STARTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/samples/modules/starter"
    INTERI_STARTER_LIBRARY_DIR="$<TARGET_FILE_DIR:com_interi_starter>")
