# SPDX-License-Identifier: MIT
# The editor's Dear ImGui layer: input feeding from platform events, an RHI renderer for ImGui's draw data, and the
# panels (dockspace, menus, outliner, inspector, tools). Unit tests run ImGui headless; the renderer is tested on
# lavapipe.
find_package(imgui CONFIG REQUIRED)
set(_tests ${PROJECT_SOURCE_DIR}/tests/editor_ui)

# The extension API (EditorExtensionsApi.h) alone, for modules: installed with the package, links nothing of the editor.
add_library(interi_editor_api INTERFACE)
add_library(interi::editor_api ALIAS interi_editor_api)
set_target_properties(interi_editor_api PROPERTIES EXPORT_NAME editor_api)
target_include_directories(interi_editor_api INTERFACE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_link_libraries(interi_editor_api INTERFACE interi::hotreload interi::render interi::rhi interi::ecs interi::core)

interi_add_module(editor_ui
    SOURCES
        src/AssetPanels.cpp
        src/ChainEditor.cpp
        src/DataPanels.cpp
        src/EditorExtensions.cpp
        src/EditorPanels.cpp
        src/HeaderCheck.cpp
        src/HudDesigner.cpp
        src/ImGuiInput.cpp
        src/ImGuiRenderer.cpp
        src/ModuleManagerPanel.cpp
        src/ServicePanels.cpp
        src/ValueWidgets.cpp
        src/ViewportOverlay.cpp
    DEPS interi::asset_import interi::editor interi::hotreload interi::hud interi::render interi::rhi interi::platform imgui::imgui
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/AssetPanelsTests.cpp
        ${_tests}/ChainEditorTests.cpp
        ${_tests}/DataPanelsTests.cpp
        ${_tests}/EditorExtensionsTests.cpp
        ${_tests}/EditorPanelsTests.cpp
        ${_tests}/HudDesignerTests.cpp
        ${_tests}/ImGuiInputTests.cpp
        ${_tests}/ModuleManagerPanelTests.cpp
        ${_tests}/ServicePanelsTests.cpp)
target_compile_definitions(interi_editor_ui_tests PRIVATE
    INTERI_SDK_TEST_MODULES_DIR="${PROJECT_SOURCE_DIR}/tests/sdk/modules")
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

# A module library that extends the editor through the abstract API only (header-only, no editor symbols linked).
if(INTERI_BUILD_SHARED)
    add_library(interi_editor_extension_test_module MODULE ${_tests}/modules/ExtensionTestModule.cpp)
    target_link_libraries(interi_editor_extension_test_module PRIVATE interi::hotreload interi::render interi::warnings)
    target_include_directories(interi_editor_extension_test_module PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    set_target_properties(interi_editor_extension_test_module PROPERTIES PREFIX "")
    add_dependencies(interi_editor_ui_tests interi_editor_extension_test_module)
    target_compile_definitions(interi_editor_ui_tests PRIVATE
        INTERI_EDITOR_EXTENSION_TEST_MODULE="$<TARGET_FILE:interi_editor_extension_test_module>")
endif()
