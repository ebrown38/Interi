# SPDX-License-Identifier: MIT
# interi_export: cooks a project into a package folder; see main.cpp.
add_executable(interi_export main.cpp)
target_link_libraries(interi_export PRIVATE interi::exporter interi::warnings)
