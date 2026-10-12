# SPDX-License-Identifier: MIT
# interi_testing: shared helpers for all Interi test suites (temp dirs, seeded RNG, near-equality, state hashing).
interi_add_module(testing
    SOURCES
        src/ScopedTempDir.cpp
        src/DeterministicRng.cpp
        src/StateHasher.cpp
    DEPS GTest::gtest
    PRIVATE_DEPS xxHash::xxhash
    TEST_SOURCES
        ${PROJECT_SOURCE_DIR}/tests/testing/ScopedTempDirTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/testing/DeterministicRngTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/testing/ExpectNearTests.cpp
        ${PROJECT_SOURCE_DIR}/tests/testing/StateHasherTests.cpp)
