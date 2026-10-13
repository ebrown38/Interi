# SPDX-License-Identifier: MIT
# Minimal demo of the platform, ECS and runtime layers. Built in every preset; not run by CI.
add_executable(interi_sandbox main.cpp)
target_link_libraries(interi_sandbox PRIVATE interi::platform_sdl interi::ecs interi::runtime interi::warnings)
