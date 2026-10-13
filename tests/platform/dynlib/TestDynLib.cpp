// SPDX-License-Identifier: MIT
// Loaded at runtime by DynamicLibraryTests.

#if defined(_WIN32)
#define INTERI_TEST_DYNLIB_EXPORT extern "C" __declspec(dllexport)
#else
#define INTERI_TEST_DYNLIB_EXPORT extern "C" __attribute__((visibility("default")))
#endif

INTERI_TEST_DYNLIB_EXPORT int interi_test_dynlib_add(int a, int b)
{
    return a + b;
}
