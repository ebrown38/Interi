# SPDX-License-Identifier: MIT
# The editor's Dear ImGui layer: input feeding from platform events, an RHI renderer for ImGui's draw data, and the
# panels (dockspace, menus, outliner, inspector, tools). Unit tests run ImGui headless; the renderer is tested on
# lavapipe.
find_package(imgui CONFIG REQUIRED)
set(_tests ${PROJECT_SOURCE_DIR}/tests/editor_ui)

interi_add_module(editor_ui
    SOURCES
        src/EditorPanels.cpp
        src/HeaderCheck.cpp
        src/ImGuiInput.cpp
        src/ImGuiRenderer.cpp
        src/ViewportOverlay.cpp
    DEPS interi::editor interi::render interi::rhi interi::platform imgui::imgui
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/EditorPanelsTests.cpp
        ${_tests}/ImGuiInputTests.cpp)
interi_add_shaders(interi_editor_ui
    SOURCES
        shaders/ImGui.ps.hlsl
        shaders/ImGui.vs.hlsl
    INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

interi_add_test_executable(interi_editor_ui_gpu_tests
    SOURCES ${_tests}/gpu/ImGuiRendererGpuTests.cpp
    DEPS interi::editor_ui interi::rhi_vulkan interi::vulkan_testing interi::testing
    LABELS integration)
target_include_directories(interi_editor_ui_gpu_tests PRIVATE ${PROJECT_SOURCE_DIR}/tests/render/gpu ${_tests})
