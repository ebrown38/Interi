# SPDX-License-Identifier: MIT
# Drawing destructibles: chunk meshes in the MeshPool, and the pieces, debris and rubble of a DestructionWorld turned
# into forward-renderer objects.
set(_tests ${PROJECT_SOURCE_DIR}/tests/destruction_render)

interi_add_module(destruction_render
    SOURCES
        src/ChunkGatherer.cpp
        src/ChunkMeshCache.cpp
        src/HeaderCheck.cpp
        src/InteriorMaterials.cpp
    DEPS interi::render interi::destruction_runtime
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/ChunkMaterialsTests.cpp
        ${_tests}/InteriorMaterialsTests.cpp)

# Cache uploads and gathering on a real device and real destruction: label `integration`.
interi_add_test_executable(interi_destruction_render_gpu_tests
    SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/ChunkGathererTests.cpp
    DEPS interi::destruction_render interi::rhi_vulkan interi::vulkan_testing interi::testing interi::physics_testing
    LABELS integration)

# Golden images (label `golden`): renders compared with tests/destruction_render/golden/*.ppm.
interi_add_test_executable(interi_destruction_render_golden_tests
    SOURCES
        ${PROJECT_SOURCE_DIR}/tests/physics/PhysicsTestEnvironment.cpp
        ${_tests}/FracturedWallGoldenTests.cpp
        ${_tests}/InteriorGoldenTests.cpp
    DEPS interi::destruction_render interi::rhi_vulkan interi::vulkan_testing interi::testing interi::physics_testing
    LABELS golden)
target_compile_definitions(interi_destruction_render_golden_tests PRIVATE
    INTERI_GOLDEN_DIR="${_tests}/golden"
    INTERI_GOLDEN_OUTPUT_DIR="${PROJECT_BINARY_DIR}/golden-output")
