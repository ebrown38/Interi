# SPDX-License-Identifier: MIT
#
# interi_add_shaders(<target>
#     SOURCES      <files...>   # HLSL, one entry point `main` per file; stage from the suffix: .cs/.vs/.ps.hlsl
#     INCLUDE_DIRS <dirs...>)   # extra #include directories (engine/shaders/include is always searched)
#
# Compiles each HLSL file to SPIR-V with DXC at build time (shader model 6.6, Vulkan 1.3 target, scalar layout)
# and embeds it in a generated header that <target> can include as <interi/shaders/<File>.h>, e.g.
# PrefixSum.cs.hlsl -> <interi/shaders/PrefixSum.cs.h> declaring `inline constexpr std::uint32_t kPrefixSumCs[]`.
#
# DXC comes from vcpkg as a host tool (port directx-dxc); it is only run at build time and never linked or shipped.

set(_dxc_root "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}")
find_program(INTERI_DXC NAMES dxc PATHS "${_dxc_root}/tools/directx-dxc" NO_DEFAULT_PATH REQUIRED)
set(INTERI_DXC_LIBRARY_DIR "${_dxc_root}/lib")
set(INTERI_SHADER_INCLUDE_DIR "${CMAKE_CURRENT_LIST_DIR}/../engine/shaders/include")
unset(_dxc_root)

function(interi_add_shaders target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "" "SOURCES;INCLUDE_DIRS")
    set(out_root "${CMAKE_CURRENT_BINARY_DIR}/${target}_shaders")
    set(include_args -I "${INTERI_SHADER_INCLUDE_DIR}")
    file(GLOB_RECURSE common_headers CONFIGURE_DEPENDS "${INTERI_SHADER_INCLUDE_DIR}/*.hlsli")
    foreach(dir IN LISTS ARG_INCLUDE_DIRS)
        list(APPEND include_args -I "${dir}")
        file(GLOB_RECURSE dir_headers CONFIGURE_DEPENDS "${dir}/*.hlsli")
        list(APPEND common_headers ${dir_headers})
    endforeach()

    set(headers)
    foreach(source IN LISTS ARG_SOURCES)
        get_filename_component(source "${source}" ABSOLUTE)
        get_filename_component(file_name "${source}" NAME) # PrefixSum.cs.hlsl
        if(NOT file_name MATCHES "^([A-Za-z0-9_]+)\\.(cs|vs|ps)\\.hlsl$")
            message(FATAL_ERROR "interi_add_shaders: '${file_name}' must be named <Name>.<cs|vs|ps>.hlsl")
        endif()
        set(name "${CMAKE_MATCH_1}")
        set(stage "${CMAKE_MATCH_2}")
        string(SUBSTRING "${stage}" 0 1 first)
        string(TOUPPER "${first}" first)
        string(SUBSTRING "${stage}" 1 -1 rest)
        set(symbol "k${name}${first}${rest}") # kPrefixSumCs

        set(spirv "${out_root}/${name}.${stage}.spv")
        set(header "${out_root}/include/interi/shaders/${name}.${stage}.h")
        add_custom_command(
            OUTPUT "${spirv}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${out_root}"
            COMMAND ${CMAKE_COMMAND} -E env "LD_LIBRARY_PATH=${INTERI_DXC_LIBRARY_DIR}"
                    "${INTERI_DXC}" -spirv -T ${stage}_6_6 -E main -HV 2021 -fspv-target-env=vulkan1.3
                    -fvk-use-scalar-layout -O3 -WX ${include_args} -Fo "${spirv}" "${source}"
            DEPENDS "${source}" ${common_headers}
            COMMENT "DXC ${file_name}"
            VERBATIM)
        add_custom_command(
            OUTPUT "${header}"
            COMMAND ${CMAKE_COMMAND} -DINPUT=${spirv} -DOUTPUT=${header} -DSYMBOL=${symbol}
                    -P "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            DEPENDS "${spirv}" "${PROJECT_SOURCE_DIR}/cmake/EmbedSpirv.cmake"
            COMMENT "Embedding ${name}.${stage}.spv"
            VERBATIM)
        list(APPEND headers "${header}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${headers})
    add_dependencies(${target} ${target}_shaders)
    target_include_directories(${target} PRIVATE "${out_root}/include")
endfunction()
