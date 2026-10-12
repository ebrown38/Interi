// SPDX-License-Identifier: MIT
// INTERI_SHIPPING must switch profiling off even when the build enables it. Like ProfilingDisabledTests.cpp, this
// TU includes only Profiling.h, so the different configuration cannot cause an ODR violation.
#undef INTERI_ENABLE_PROFILING
#define INTERI_ENABLE_PROFILING 1
#define INTERI_SHIPPING 1
#include <interi/core/Profiling.h>

#include <gtest/gtest.h>

static_assert(INTERI_PROFILING_ACTIVE == 0, "shipping builds never profile");

TEST(Profiling, ShippingBuild_DisablesProfilingEvenWhenRequested)
{
    SUCCEED();
}
