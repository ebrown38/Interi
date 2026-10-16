# SPDX-License-Identifier: MIT
# vcpkg's x64-windows-static-md (static libraries, dynamic CRT), except Tracy, which is a DLL: a process must have one
# Tracy instance. Hot-reloaded gameplay modules (DLLs, see engine/hotreload) link the engine's static libraries; with
# a static TracyClient each module would carry its own profiler, whose thread start-up and shutdown run inside DllMain
# and deadlock on the loader lock (Tracy's manual, "Setup for multi-DLL projects").
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

if(PORT STREQUAL "tracy")
    set(VCPKG_LIBRARY_LINKAGE dynamic)
endif()
