// SPDX-License-Identifier: MIT
#include <interi/core/Profiling.h>

#include <gtest/gtest.h>

#include <thread>

namespace
{
int InstrumentedWork(int n)
{
    INTERI_ZONE();
    int sum = 0;
    for (int i = 0; i < n; ++i)
    {
        INTERI_ZONE_NAMED("Inner");
        sum += i;
    }
    INTERI_PROFILE_PLOT("InstrumentedWork.sum", static_cast<double>(sum));
    return sum;
}
} // namespace

TEST(Profiling, Macros_RunInThisBuildConfiguration)
{
    // In *-profile builds this exercises the real Tracy client (on demand: nothing is recorded without a viewer).
    std::thread worker(
        []
        {
            INTERI_PROFILE_THREAD_NAME("Profiling test worker");
            EXPECT_EQ(InstrumentedWork(10), 45);
        });
    worker.join();
    EXPECT_EQ(InstrumentedWork(4), 6);
    INTERI_FRAME_MARK();
}
