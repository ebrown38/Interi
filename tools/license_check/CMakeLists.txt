# SPDX-License-Identifier: MIT
# License compliance gates (Interi is MIT open source; every dependency must be permissive).

set(_installed_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")

add_test(NAME LicenseTools_PythonUnitTests
    COMMAND ${Python3_EXECUTABLE} -m unittest -v test_license_tools
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

add_test(NAME LicenseCheck_GplDepInjected_Fails
    COMMAND ${Python3_EXECUTABLE} -m unittest test_license_tools.CheckLicensesTests.test_gpl_dep_injected_fails
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tests)

# The real dependency set of this build.
add_test(NAME LicenseCheck_AllDepsOnAllowlist_Passes
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_licenses.py --installed-dir ${_installed_dir})

# Build tools (e.g. DXC) are installed for the host triplet; check those too when it differs from the target.
if(NOT VCPKG_HOST_TRIPLET STREQUAL VCPKG_TARGET_TRIPLET)
    add_test(NAME LicenseCheck_HostTools_OnAllowlist
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_licenses.py
                --installed-dir ${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET})
    set_tests_properties(LicenseCheck_HostTools_OnAllowlist PROPERTIES LABELS "unit;license")
endif()

add_test(NAME SpdxHeader_AllSourceFiles_Present
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_spdx_headers.py --root ${PROJECT_SOURCE_DIR})

add_test(NAME ThirdPartyNotices_UpToDate
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/gen_notices.py --check
            --installed-dir ${_installed_dir} --output ${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md)

set_tests_properties(
    LicenseTools_PythonUnitTests LicenseCheck_GplDepInjected_Fails LicenseCheck_AllDepsOnAllowlist_Passes
    SpdxHeader_AllSourceFiles_Present ThirdPartyNotices_UpToDate
    PROPERTIES LABELS "unit;license")
