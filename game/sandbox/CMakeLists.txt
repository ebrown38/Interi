# SPDX-License-Identifier: MIT
# Demo of the platform, ECS, runtime, physics, destruction, gameplay, rendering and audio layers, with the gameplay rules in
# a hot-reloadable module (../sandbox_module). Built in every preset; not run by CI.
add_executable(interi_sandbox main.cpp SandboxRenderer.cpp)
target_link_libraries(interi_sandbox PRIVATE
    interi::platform_sdl interi::ecs interi::physics interi::runtime interi::render interi::rhi_vulkan
    interi::destruction_render interi::gameplay interi::hotreload interi::audio interi::destruction_audio interi::warnings)
target_include_directories(interi_sandbox PRIVATE ${PROJECT_SOURCE_DIR}/game/sandbox_module)
add_dependencies(interi_sandbox interi_sandbox_module)
target_compile_definitions(interi_sandbox PRIVATE
    INTERI_SANDBOX_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    INTERI_SANDBOX_MODULE_PATH="$<TARGET_FILE:interi_sandbox_module>"
    INTERI_SANDBOX_MODULE_SOURCE_DIR="${PROJECT_SOURCE_DIR}/game/sandbox_module"
    INTERI_SANDBOX_BUILD_DIR="${CMAKE_BINARY_DIR}")
