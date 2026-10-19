# SPDX-License-Identifier: MIT
# CI helpers (toolchain pin check, OSV scan of the vcpkg SBOM, JUnit summary). The scan itself needs the network and
# runs as a CI step; these tests are offline.
add_test(NAME CiTools_PythonUnitTests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_ci_tools
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
set_tests_properties(CiTools_PythonUnitTests PROPERTIES LABELS "unit")
