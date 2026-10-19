# SPDX-License-Identifier: MIT
# Module SDK: module.json manifests, semantic versions, the toolchain fingerprint, the module graph and the layered
# contribution registry that resolves what each Module, Extension and Plugin provides.
set(_tests ${PROJECT_SOURCE_DIR}/tests/sdk)

interi_add_module(sdk SHARED
    SOURCES
        src/Ai.cpp
        src/ContributionRegistry.cpp
        src/HeaderCheck.cpp
        src/HookSignature.cpp
        src/Manifest.cpp
        src/ModuleGraph.cpp
        src/Project.cpp
        src/SemVer.cpp
        src/Toolchain.cpp
    DEPS interi::core
    PRIVATE_DEPS nlohmann_json::nlohmann_json
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/AiTests.cpp
        ${_tests}/ContributionRegistryTests.cpp
        ${_tests}/HookSignatureTests.cpp
        ${_tests}/HooksTests.cpp
        ${_tests}/ManifestTests.cpp
        ${_tests}/ModuleGraphTests.cpp
        ${_tests}/PhysicsEngineTests.cpp
        ${_tests}/PhysicsQueriesTests.cpp
        ${_tests}/ProjectTests.cpp
        ${_tests}/SemVerTests.cpp)

# The shared test project's manifests (tests/sdk/modules); the hotreload tests build their libraries.
target_compile_definitions(interi_sdk_tests PRIVATE INTERI_SDK_TEST_MODULES_DIR="${_tests}/modules")
