// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>

#include <gtest/gtest.h>

#include <string>

namespace
{
struct Captured
{
    int count = 0;
    std::string expression;
    std::string message;
    std::string file;
    int line = 0;
};

Captured g_captured;

interi::AssertAction RecordAndContinue(const interi::AssertInfo& info)
{
    ++g_captured.count;
    g_captured.expression = info.expression;
    g_captured.message = info.message != nullptr ? info.message : "";
    g_captured.file = info.file;
    g_captured.line = info.line;
    return interi::AssertAction::Continue;
}

class AssertTest : public ::testing::Test
{
protected:
    void SetUp() override { g_captured = {}; }

    interi::ScopedAssertHandler m_handler{&RecordAndContinue};
};

int g_sideEffects = 0;

bool CountedTrue()
{
    ++g_sideEffects;
    return true;
}
} // namespace

TEST_F(AssertTest, Failure_InvokesCustomHandler)
{
    if constexpr (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    const int line = __LINE__ + 1;
    INTERI_ASSERT(1 + 1 == 3);

    EXPECT_EQ(g_captured.count, 1);
    EXPECT_EQ(g_captured.expression, "1 + 1 == 3");
    EXPECT_EQ(g_captured.message, "");
    EXPECT_NE(g_captured.file.find("AssertTests.cpp"), std::string::npos);
    EXPECT_EQ(g_captured.line, line);
}

TEST_F(AssertTest, FailureWithMessage_PassesMessageToHandler)
{
    if constexpr (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    INTERI_ASSERT(false, "chunk index out of range");

    EXPECT_EQ(g_captured.count, 1);
    EXPECT_EQ(g_captured.message, "chunk index out of range");
}

TEST_F(AssertTest, Success_DoesNotInvokeHandler)
{
    INTERI_ASSERT(2 + 2 == 4);
    INTERI_VERIFY(true, "never fires");

    EXPECT_EQ(g_captured.count, 0);
}

TEST_F(AssertTest, Verify_AlwaysEvaluatesExpression)
{
    g_sideEffects = 0;
    INTERI_VERIFY(CountedTrue());
    EXPECT_EQ(g_sideEffects, 1);
}

TEST_F(AssertTest, ScopedHandler_RestoresPreviousOnDestruction)
{
    const interi::AssertHandler outer = interi::GetAssertHandler();
    {
        const interi::ScopedAssertHandler inner{[](const interi::AssertInfo&)
                                                { return interi::AssertAction::Continue; }};
        EXPECT_NE(interi::GetAssertHandler(), outer);
    }
    EXPECT_EQ(interi::GetAssertHandler(), outer);
}

TEST(AssertDeathTest, DefaultHandler_AbortsWithExpression)
{
    if constexpr (!interi::kAssertsEnabled)
    {
        GTEST_SKIP() << "asserts are compiled out in this configuration";
    }
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            interi::SetAssertHandler(nullptr); // nullptr selects the default handler
            INTERI_ASSERT(sizeof(int) == 0, "boom");
        },
        "sizeof\\(int\\) == 0");
}
