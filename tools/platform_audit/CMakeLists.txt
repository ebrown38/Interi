# SPDX-License-Identifier: MIT
# Platform audit (M25.2): Vulkan, SDL, POSIX and Win32 names stay inside the modules that wrap them, and console or
# D3D12 headers never enter this repository (they belong in an INTERI_EXTERNAL_PLATFORM_DIR project).
add_test(NAME PlatformAudit_PythonUnitTests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_platform_audit
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)
add_test(NAME PlatformAudit_Repository_HasNoBackendTypesOutsideTheirModules
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/platform_audit.py --root ${PROJECT_SOURCE_DIR})
set_tests_properties(PlatformAudit_PythonUnitTests PlatformAudit_Repository_HasNoBackendTypesOutsideTheirModules
    PROPERTIES LABELS unit)
