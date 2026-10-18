# SPDX-License-Identifier: MIT
# Modeling: constructive solid geometry on scene meshes through Manifold. Editor-only (static): CSG results are baked
# into mesh assets, so Manifold never reaches games.
set(_tests ${PROJECT_SOURCE_DIR}/tests/modeling)

find_package(manifold CONFIG REQUIRED)

interi_add_module(modeling
    SOURCES
        src/Csg.cpp
        src/HeaderCheck.cpp
    DEPS interi::scene interi::core
    PRIVATE_DEPS manifold::manifold
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/CsgTests.cpp)
