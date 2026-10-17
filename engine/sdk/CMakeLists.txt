# SPDX-License-Identifier: MIT
# Module SDK: module.json manifests, semantic versions, the toolchain fingerprint, the module graph and the layered
# contribution registry that resolves what each Module, Extension and Plugin provides.
set(_tests ${PROJECT_SOURCE_DIR}/tests/sdk)

interi_add_module(sdk SHARED
    SOURCES
        src/HeaderCheck.cpp
        src/Manifest.cpp
        src/SemVer.cpp
        src/Toolchain.cpp
    DEPS interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/ManifestTests.cpp
        ${_tests}/SemVerTests.cpp)
