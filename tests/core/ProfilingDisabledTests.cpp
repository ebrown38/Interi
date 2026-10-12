// SPDX-License-Identifier: MIT
// Compiled with profiling forced off, whatever the build's setting, to prove the macros vanish. This TU includes
// only Profiling.h (macros, no inline variables), so the different setting cannot cause an ODR violation.
#undef INTERI_ENABLE_PROFILING
#define INTERI_ENABLE_PROFILING 0
#include <interi/core/Profiling.h>

#include <gtest/gtest.h>

static_assert(INTERI_PROFILING_ACTIVE == 0);

namespace
{
/// A Tracy zone is a runtime object with a non-constexpr constructor, so constant evaluation only succeeds if
/// every macro expanded to nothing.
constexpr int EveryMacroInConstantExpression()
{
    INTERI_ZONE();
    INTERI_ZONE_NAMED("Named");
    INTERI_ZONE_TEXT("text", 4);
    INTERI_FRAME_MARK();
    INTERI_PROFILE_PLOT("plot", 1.0);
    INTERI_PROFILE_THREAD_NAME("thread");
    return 42;
}
static_assert(EveryMacroInConstantExpression() == 42);

/// Capturing lambdas grow by their captures; a zone must not add any state.
auto MakeInstrumentedLambda()
{
    return []
    {
        INTERI_ZONE();
        return 1;
    };
}
static_assert(sizeof(decltype(MakeInstrumentedLambda())) == 1);
} // namespace

TEST(Profiling, ProfilingDisabled_Macro_CompilesToNothing)
{
    EXPECT_EQ(EveryMacroInConstantExpression(), 42);
    EXPECT_EQ(MakeInstrumentedLambda()(), 1);
}
