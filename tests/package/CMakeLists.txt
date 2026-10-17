# SPDX-License-Identifier: MIT
# Installs the engine into the build tree, then builds the starter sample and a freshly scaffolded Module, Extension
# and Plugin out of tree with find_package(Interi), and runs their load tests (label `integration`).
if(NOT INTERI_BUILD_SHARED)
    return()
endif()

set(_root ${PROJECT_BINARY_DIR}/package-test)
add_test(NAME Package_Install
    COMMAND ${CMAKE_COMMAND} --install ${PROJECT_BINARY_DIR} --prefix ${_root}/install)
set_tests_properties(Package_Install PROPERTIES LABELS integration FIXTURES_SETUP interi_package)

# The engine's own dependencies (Tracy in profile builds) come from its vcpkg tree.
set(_common
    "-DPREFIX=${_root}/install|${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}"
    "-DGENERATOR=${CMAKE_GENERATOR}"
    "-DCOMPILER=${CMAKE_CXX_COMPILER}"
    "-DBUILD_TYPE=${CMAKE_BUILD_TYPE}"
    "-DCTEST=${CMAKE_CTEST_COMMAND}"
    "-DLINKER_TYPE=${CMAKE_LINKER_TYPE}")

add_test(NAME Package_Starter_BuildsOutOfTree
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${PROJECT_SOURCE_DIR}/samples/modules/starter -DBINARY=${_root}/starter
            ${_common} -P ${CMAKE_CURRENT_SOURCE_DIR}/BuildOutOfTree.cmake)
set(_tests Package_Starter_BuildsOutOfTree)
foreach(kind Module Extension Plugin)
    add_test(NAME Package_Scaffold${kind}_BuildsOutOfTree
        COMMAND ${CMAKE_COMMAND} -DSCAFFOLD_KIND=${kind} -DPYTHON=${Python3_EXECUTABLE}
                -DNEW_MODULE=${PROJECT_SOURCE_DIR}/tools/new_module/new_module.py -DBINARY=${_root}/scaffold_${kind}
                ${_common} -P ${CMAKE_CURRENT_SOURCE_DIR}/BuildOutOfTree.cmake)
    list(APPEND _tests Package_Scaffold${kind}_BuildsOutOfTree)
endforeach()
set_tests_properties(${_tests} PROPERTIES LABELS integration FIXTURES_REQUIRED interi_package)
