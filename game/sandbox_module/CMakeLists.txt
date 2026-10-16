# SPDX-License-Identifier: MIT
# The sandbox's hot-reloadable gameplay module: a shared library the sandbox loads with interi::hotreload.
add_library(interi_sandbox_module MODULE SandboxModule.cpp)
target_link_libraries(interi_sandbox_module PRIVATE interi::hotreload interi::warnings)
target_include_directories(interi_sandbox_module INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
