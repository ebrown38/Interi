# SPDX-License-Identifier: MIT
# Overlay port: ufbx is not in the vendored vcpkg baseline. It is one C source file (MIT or public domain; the
# license is declared as MIT), built here as a static library exporting the CMake target ufbx::ufbx.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO ufbx/ufbx
    REF "v${VERSION}"
    SHA512 783733311503b01854505342ad4d77cfdc8fb191b8c7a664c02b090e054420c391c28f63416f824bfb64ba1ffef6a7f2f5cadd6e185d851d45c708fab2f24b51
    HEAD_REF master
)

file(COPY "${SOURCE_PATH}/ufbx.c" "${SOURCE_PATH}/ufbx.h" DESTINATION "${CURRENT_BUILDTREES_DIR}/ufbx-project")
file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" "${CMAKE_CURRENT_LIST_DIR}/ufbxConfig.cmake.in"
     DESTINATION "${CURRENT_BUILDTREES_DIR}/ufbx-project")

vcpkg_cmake_configure(SOURCE_PATH "${CURRENT_BUILDTREES_DIR}/ufbx-project")
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME ufbx CONFIG_PATH "lib/cmake/ufbx")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
