# SPDX-License-Identifier: MIT
# interi_player: the runtime of an exported game (the engine without the editor); see main.cpp.
if(NOT INTERI_BUILD_SHARED)
    return() # modules can't load into a static engine
endif()
add_executable(interi_player main.cpp)
target_link_libraries(interi_player PRIVATE interi::exporter interi::game interi::runtime interi::platform_sdl interi::platform interi::warnings)
