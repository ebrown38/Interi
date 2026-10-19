# SPDX-License-Identifier: MIT
# Seeded mutation fuzzing of the manifest, project, chain, expression, scene and HUD parsers, and the starter sample
# played headless with random input (soak). Both read the committed starter sample.
interi_add_test_executable(interi_fuzz_tests
    SOURCES FuzzTests.cpp
    DEPS interi::sdk interi::chain interi::scene interi::hud interi::testing
    LABELS unit)
target_compile_definitions(interi_fuzz_tests PRIVATE
    INTERI_STARTER_SOURCE_DIR="${PROJECT_SOURCE_DIR}/samples/modules/starter")
