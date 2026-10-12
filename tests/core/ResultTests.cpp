// SPDX-License-Identifier: MIT
#include <interi/core/Result.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
interi::Result<int> ParsePositive(int value)
{
    if (value <= 0)
    {
        return interi::MakeError(interi::ErrorCode::InvalidArgument, "value must be positive");
    }
    return value;
}

interi::Result<int> DoubledPositive(int value)
{
    INTERI_TRY_ASSIGN(parsed, ParsePositive(value));
    return parsed * 2;
}

interi::Result<> CheckAll(int a, int b)
{
    INTERI_TRY(ParsePositive(a));
    INTERI_TRY(ParsePositive(b));
    return {};
}
} // namespace

TEST(Result, SuccessPath_HoldsValue)
{
    const interi::Result<int> result = DoubledPositive(21);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 42);
}

TEST(Result, ErrorPath_PropagatesError)
{
    const interi::Result<int> result = DoubledPositive(-1);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, interi::ErrorCode::InvalidArgument);
    EXPECT_EQ(result.error().message, "value must be positive");
}

TEST(Result, VoidResult_StopsAtFirstError)
{
    EXPECT_TRUE(CheckAll(1, 2).has_value());
    const interi::Result<> failed = CheckAll(1, 0);
    ASSERT_FALSE(failed.has_value());
    EXPECT_EQ(failed.error().code, interi::ErrorCode::InvalidArgument);
}

TEST(Result, ErrorCodeToString_NamesEveryCode)
{
    EXPECT_EQ(interi::ToString(interi::ErrorCode::InvalidArgument), "InvalidArgument");
    EXPECT_EQ(interi::ToString(interi::ErrorCode::OutOfMemory), "OutOfMemory");
    EXPECT_EQ(interi::ToString(interi::ErrorCode::Corrupt), "Corrupt");
}
