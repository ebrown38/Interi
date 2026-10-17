# SPDX-License-Identifier: MIT
# Builds a gameplay module out of tree against the installed engine (find_package(Interi)) and runs its ctest (the
# module load test). With SCAFFOLD_KIND, the module is first scaffolded by tools/new_module/new_module.py.
#
#   cmake -DSOURCE=<module dir> | -DSCAFFOLD_KIND=<Module|Extension|Plugin> -DPYTHON=<python> -DNEW_MODULE=<script>
#         -DBINARY=<scratch dir> -DPREFIX=<prefixes separated by |> -DGENERATOR=<generator> -DCOMPILER=<c++ compiler>
#         -DBUILD_TYPE=<config> -DCTEST=<ctest> [-DLINKER_TYPE=<type>] -P BuildOutOfTree.cmake

function(run step)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${step} failed (${result})")
    endif()
endfunction()

file(REMOVE_RECURSE "${BINARY}")
file(MAKE_DIRECTORY "${BINARY}")

if(SCAFFOLD_KIND)
    string(TOLOWER "${SCAFFOLD_KIND}" kind)
    set(SOURCE "${BINARY}/src")
    run("scaffolding" "${PYTHON}" "${NEW_MODULE}" --kind ${SCAFFOLD_KIND} --id com.test.scaffold.${kind}
        --author "Interi tests" --output "${SOURCE}")
endif()

# The environment variable takes a native path list, which survives being passed through execute_process (a CMake
# list in a -D argument would be split into separate arguments).
if(CMAKE_HOST_WIN32)
    string(REPLACE "|" ";" prefix "${PREFIX}")
else()
    string(REPLACE "|" ":" prefix "${PREFIX}")
endif()
set(ENV{CMAKE_PREFIX_PATH} "${prefix}")
set(configure_args -S "${SOURCE}" -B "${BINARY}/build" -G "${GENERATOR}" "-DCMAKE_CXX_COMPILER=${COMPILER}"
                   "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
if(LINKER_TYPE)
    list(APPEND configure_args "-DCMAKE_LINKER_TYPE=${LINKER_TYPE}")
endif()
run("configure" "${CMAKE_COMMAND}" ${configure_args})
run("build" "${CMAKE_COMMAND}" --build "${BINARY}/build")
run("load test" "${CTEST}" --test-dir "${BINARY}/build" --output-on-failure --no-tests=error)
