// SPDX-License-Identifier: MIT
#include <interi/platform/DynamicLibrary.h>

#include <gtest/gtest.h>

#include <utility>

using interi::DynamicLibrary;
using interi::ErrorCode;

TEST(DynamicLibrary, LoadMissing_ReturnsError)
{
    const auto library = DynamicLibrary::Open("interi_no_such_library_7f3a.so");
    ASSERT_FALSE(library.has_value());
    EXPECT_EQ(library.error().code, ErrorCode::NotFound);
    EXPECT_FALSE(library.error().message.empty());
}

TEST(DynamicLibrary, LoadTestLibrary_CallsExportedFunction)
{
    auto library = DynamicLibrary::Open(INTERI_TEST_DYNLIB_PATH);
    ASSERT_TRUE(library.has_value()) << library.error().message;
    EXPECT_TRUE(library->IsOpen());

    using AddFn = int (*)(int, int);
    const auto add = library->FindFunction<AddFn>("interi_test_dynlib_add");
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add(40, 2), 42);
}

TEST(DynamicLibrary, MissingSymbol_ReturnsNull)
{
    auto library = DynamicLibrary::Open(INTERI_TEST_DYNLIB_PATH);
    ASSERT_TRUE(library.has_value());
    EXPECT_EQ(library->FindSymbol("interi_test_dynlib_missing"), nullptr);
}

TEST(DynamicLibrary, Move_TransfersOwnership)
{
    auto library = DynamicLibrary::Open(INTERI_TEST_DYNLIB_PATH);
    ASSERT_TRUE(library.has_value());
    DynamicLibrary moved = std::move(*library);
    EXPECT_TRUE(moved.IsOpen());
    EXPECT_FALSE(library->IsOpen()); // NOLINT(bugprone-use-after-move): moved-from state is specified
    EXPECT_NE(moved.FindSymbol("interi_test_dynlib_add"), nullptr);
    moved.Close();
    EXPECT_FALSE(moved.IsOpen());
}
