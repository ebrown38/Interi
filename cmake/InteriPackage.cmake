# SPDX-License-Identifier: MIT
# `cmake --install`: the shared engine libraries with their headers, interi_module_check, and the CMake package
# (find_package(Interi) -> interi::<module> targets plus InteriGameplayModule.cmake), so gameplay modules build out of
# tree. Only the shared engine is installable: a static engine can't host modules.

if(NOT INTERI_BUILD_SHARED)
    return()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(INTERI_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/Interi")

get_property(_libraries GLOBAL PROPERTY INTERI_SHARED_LIBRARIES)
foreach(_library IN LISTS _libraries)
    get_target_property(_source_dir ${_library} SOURCE_DIR)
    get_target_property(_binary_dir ${_library} BINARY_DIR)
    install(DIRECTORY "${_source_dir}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY "${_binary_dir}/generated/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "Export.h")
    # Installed libraries find each other next to themselves.
    if(NOT WIN32)
        set_target_properties(${_library} PROPERTIES INSTALL_RPATH "$ORIGIN")
    endif()
endforeach()

if(NOT WIN32)
    set_target_properties(interi_module_check PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

install(TARGETS ${_libraries} interi_build_settings interi_module_check
    EXPORT InteriTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# Windows profile builds load Tracy from a DLL (see cmake/triplets/x64-windows-static-md.cmake).
if(WIN32 AND INTERI_PROFILING)
    find_package(Tracy CONFIG REQUIRED) # engine/core's import is directory-scoped
    install(IMPORTED_RUNTIME_ARTIFACTS Tracy::TracyClient RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(EXPORT InteriTargets NAMESPACE interi:: DESTINATION ${INTERI_INSTALL_CMAKEDIR})

# The editor extension API: header-only, so a module can add panels, tools, overlays and render features to the
# editor without linking any editor library.
install(TARGETS interi_editor_api EXPORT InteriTargets INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${PROJECT_SOURCE_DIR}/engine/editor_ui/include/interi/editor_ui/EditorExtensionsApi.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/interi/editor_ui)

# Component `Vulkan` (find_package(Interi COMPONENTS Vulkan)): the Vulkan backend, the SDL3 platform and the helpers
# for GPU tests, for modules that draw or open windows. Static libraries; their third-party dependencies (volk, VMA,
# SPIRV-Reflect, SDL3) are found by the config.
get_property(_vulkan_libraries GLOBAL PROPERTY INTERI_COMPONENT_Vulkan)
foreach(_library IN LISTS _vulkan_libraries)
    get_target_property(_source_dir ${_library} SOURCE_DIR)
    get_target_property(_binary_dir ${_library} BINARY_DIR)
    install(DIRECTORY "${_source_dir}/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY "${_binary_dir}/generated/include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "Export.h")
endforeach()
if(_vulkan_libraries)
    install(TARGETS ${_vulkan_libraries} EXPORT InteriVulkanTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(EXPORT InteriVulkanTargets NAMESPACE interi:: DESTINATION ${INTERI_INSTALL_CMAKEDIR})
    # GPU test helpers (interi::vulkan_testing): the fixture, the golden-image and offscreen helpers, and the source
    # that selects lavapipe and carries the sanitizer suppressions.
    install(FILES
            ${PROJECT_SOURCE_DIR}/tests/rhi_vulkan/VulkanTestDevice.h
            ${PROJECT_SOURCE_DIR}/tests/render/gpu/GoldenImage.h
            ${PROJECT_SOURCE_DIR}/tests/render/gpu/OffscreenTarget.h
            ${PROJECT_SOURCE_DIR}/tests/render/gpu/RenderScene.h
        DESTINATION ${CMAKE_INSTALL_DATADIR}/interi/gpu_testing)
    install(FILES ${PROJECT_SOURCE_DIR}/tests/rhi_vulkan/VulkanTestSupport.cpp
        DESTINATION ${CMAKE_INSTALL_DATADIR}/interi/gpu_testing)
    install(FILES ${CMAKE_CURRENT_LIST_DIR}/InteriVulkanTesting.cmake DESTINATION ${INTERI_INSTALL_CMAKEDIR})
endif()

configure_package_config_file(${CMAKE_CURRENT_LIST_DIR}/InteriConfig.cmake.in
    ${PROJECT_BINARY_DIR}/package/InteriConfig.cmake
    INSTALL_DESTINATION ${INTERI_INSTALL_CMAKEDIR})
# Pre-1.0, a minor version may break the module ABI; the toolchain fingerprint guards the rest.
write_basic_package_version_file(${PROJECT_BINARY_DIR}/package/InteriConfigVersion.cmake
    COMPATIBILITY SameMinorVersion)
install(FILES
        ${PROJECT_BINARY_DIR}/package/InteriConfig.cmake
        ${PROJECT_BINARY_DIR}/package/InteriConfigVersion.cmake
        ${CMAKE_CURRENT_LIST_DIR}/InteriGameplayModule.cmake
    DESTINATION ${INTERI_INSTALL_CMAKEDIR})
