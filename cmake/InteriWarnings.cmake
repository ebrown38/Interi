# SPDX-License-Identifier: MIT
# Warnings-as-errors settings, applied only to Interi's own targets (never to third-party code).

add_library(interi_warnings INTERFACE)
add_library(interi::warnings ALIAS interi_warnings)

if(MSVC)
    target_compile_options(interi_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
    target_compile_options(interi_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Werror
        -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Wold-style-cast
        -Woverloaded-virtual -Wnull-dereference -Wdouble-promotion -Wimplicit-fallthrough)
endif()
