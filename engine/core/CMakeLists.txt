# SPDX-License-Identifier: MIT
configure_file(src/Version.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp @ONLY)

set(_tests ${PROJECT_SOURCE_DIR}/tests/core)

interi_add_module(core
    SOURCES
        ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.cpp
        src/Archive.cpp
        src/Assert.cpp
        src/Hash.cpp
        src/HeaderCheck.cpp
        src/Log.cpp
        src/StringId.cpp
        src/memory/FrameAllocator.cpp
        src/memory/HeapAllocator.cpp
        src/memory/LinearAllocator.cpp
        src/memory/MemoryTracking.cpp
        src/memory/PoolAllocator.cpp
        src/memory/TlsfAllocator.cpp
    DEPS Threads::Threads
    PRIVATE_DEPS xxHash::xxhash
    TEST_DEPS interi::testing
    TEST_SOURCES
        ${_tests}/VersionTests.cpp
        ${_tests}/BuildConfigTests.cpp
        ${_tests}/AssertTests.cpp
        ${_tests}/ResultTests.cpp
        ${_tests}/LoggerTests.cpp
        ${_tests}/AllocatorTests.cpp
        ${_tests}/TlsfAllocatorTests.cpp
        ${_tests}/HashTests.cpp
        ${_tests}/ArchiveTests.cpp
        ${_tests}/SlotMapTests.cpp
        ${_tests}/VectorTests.cpp
        ${_tests}/HashMapTests.cpp
        ${_tests}/QueueTests.cpp)

target_compile_definitions(interi_core_tests PRIVATE
    INTERI_TEST_EXPECTED_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    INTERI_TEST_EXPECTED_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    INTERI_TEST_EXPECTED_VERSION_PATCH=${PROJECT_VERSION_PATCH}
    INTERI_TEST_SIMD_REQUESTED=$<BOOL:${INTERI_SIMD}>)

# Long-running and randomized concurrency tests: label `stress`, run nightly (and under TSan).
interi_add_test_executable(interi_core_stress_tests
    SOURCES
        ${_tests}/stress/LoggerStressTests.cpp
        ${_tests}/stress/PoolAllocatorStressTests.cpp
        ${_tests}/stress/QueueStressTests.cpp
    DEPS interi::core interi::testing
    LABELS stress)
