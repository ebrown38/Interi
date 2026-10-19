# SPDX-License-Identifier: MIT
# Asset formats a game loads: guids, textures (mips, block compression), PBR materials and skeletal data, each a cooked
# binary container (magic, version, payload hash) or authored JSON. No importers here (asset_import is editor-only), so
# nothing third-party reaches games.
set(_tests ${PROJECT_SOURCE_DIR}/tests/assets)

interi_add_module(assets
    SOURCES
        src/Guid.cpp
        src/HeaderCheck.cpp
        src/Material.cpp
        src/Skeleton.cpp
        src/Texture.cpp
    DEPS interi::core interi::platform
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/GuidTests.cpp
        ${_tests}/MaterialTests.cpp
        ${_tests}/SkeletonTests.cpp
        ${_tests}/TextureTests.cpp)
