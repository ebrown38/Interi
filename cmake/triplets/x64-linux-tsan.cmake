# SPDX-License-Identifier: MIT
# x64-linux with every dependency built by Clang under ThreadSanitizer (used by the linux-clang-tsan preset).
# TSan needs all code that synchronises threads to be instrumented: an uninstrumented Jolt hides its semaphores and
# barriers from TSan, while Jolt's inline header code compiled into Interi is instrumented, which produces false
# race reports. Clang is forced because GCC's and Clang's TSan runtimes cannot be mixed.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)

set(VCPKG_C_FLAGS "-fsanitize=thread")
set(VCPKG_CXX_FLAGS "-fsanitize=thread")
set(VCPKG_LINKER_FLAGS "-fsanitize=thread")
set(VCPKG_CMAKE_CONFIGURE_OPTIONS -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
