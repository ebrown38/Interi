# SPDX-License-Identifier: MIT
set(_tests ${PROJECT_SOURCE_DIR}/tests/render)

# Renderer built on the backend-neutral RHI: frame graph, passes, materials. Unit tests use tests/rhi/MockDevice.h;
# interi_render_gpu_tests runs the same code on a real Vulkan device.
interi_add_module(render
    SOURCES
        src/Camera.cpp
        src/Clusters.cpp
        src/Decals.cpp
        src/ForwardRenderer.cpp
        src/FrameGraph.cpp
        src/HeaderCheck.cpp
        src/Mesh.cpp
        src/MeshPool.cpp
        src/Particles.cpp
        src/Shadows.cpp
        src/Texture.cpp
    DEPS interi::rhi
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CameraTests.cpp
        ${_tests}/ClustersTests.cpp
        ${_tests}/DecalsTests.cpp
        ${_tests}/FrameGraphTests.cpp
        ${_tests}/MeshTests.cpp
        ${_tests}/ParticlesTests.cpp
        ${_tests}/ShadowsTests.cpp
        ${_tests}/TextureTests.cpp)
interi_add_shaders(interi_render
    SOURCES
        shaders/Cull.cs.hlsl
        shaders/Decal.ps.hlsl
        shaders/Decal.vs.hlsl
        shaders/DepthOnly.vs.hlsl
        shaders/Forward.ps.hlsl
        shaders/Forward.vs.hlsl
        shaders/HiZ.cs.hlsl
        shaders/LightCulling.cs.hlsl
        shaders/Particle.ps.hlsl
        shaders/Particle.vs.hlsl
        shaders/Particles.cs.hlsl
        shaders/Shadow.ps.hlsl
        shaders/Shadow.vs.hlsl
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

interi_add_test_executable(interi_render_gpu_tests
    SOURCES
        ${_tests}/gpu/ClusteredLightingGpuTests.cpp
        ${_tests}/gpu/ConventionTests.cpp
        ${_tests}/gpu/FrameGraphGpuTests.cpp
        ${_tests}/gpu/GpuCullingTests.cpp
        ${_tests}/gpu/ParticleGpuTests.cpp
    DEPS interi::render interi::rhi_vulkan interi::vulkan_testing interi::testing
    LABELS integration)
interi_add_shaders(interi_render_gpu_tests
    SOURCES
        ${_tests}/shaders/ClipSpace.vs.hlsl
        ${_tests}/shaders/Solid.ps.hlsl)

# Golden image tests (label `golden`): renders compared with tests/render/golden/*.ppm. Mismatches write the actual
# image and a diff to build/<preset>/golden-output (uploaded by CI on failure).
interi_add_test_executable(interi_render_golden_tests
    SOURCES
        ${_tests}/gpu/LitCubeGoldenTests.cpp
        ${_tests}/gpu/ParticleGoldenTests.cpp
    DEPS interi::render interi::rhi_vulkan interi::vulkan_testing
    LABELS golden)
foreach(_target interi_render_golden_tests interi_render_gpu_tests)
    target_compile_definitions(${_target} PRIVATE
        INTERI_GOLDEN_DIR="${_tests}/golden"
        INTERI_GOLDEN_OUTPUT_DIR="${PROJECT_BINARY_DIR}/golden-output")
endforeach()
