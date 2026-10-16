# SPDX-License-Identifier: MIT
# Demo of the platform, ECS, runtime, physics, destruction and rendering layers. Built in every preset; not run by CI.
add_executable(interi_sandbox main.cpp SandboxRenderer.cpp)
target_link_libraries(interi_sandbox PRIVATE
    interi::platform_sdl interi::ecs interi::physics interi::runtime interi::render interi::rhi_vulkan
    interi::destruction_render interi::warnings)
