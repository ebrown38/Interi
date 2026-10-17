# SPDX-License-Identifier: MIT
# level_editor: the level and destruction authoring editor (ImGui over the forward renderer). Built in every
# preset; CI builds it but does not run it.
add_executable(level_editor main.cpp EditorApp.cpp EditorRenderer.cpp)
target_link_libraries(level_editor PRIVATE
    interi::editor_ui interi::editor interi::platform_sdl interi::render interi::rhi_vulkan interi::destruction_render
    interi::jobs interi::warnings)
target_compile_definitions(level_editor PRIVATE INTERI_EDITOR_DATA_DIR="${PROJECT_SOURCE_DIR}/data")
