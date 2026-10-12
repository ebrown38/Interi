// SPDX-License-Identifier: MIT
// Proves the toolchain, vcpkg GoogleTest and CTest discovery are wired up.
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <vector>

TEST(Smoke, Build_GTestRuns)
{
    EXPECT_EQ(1 + 1, 2);
}

TEST(Smoke, Build_GMockMatchersAvailable)
{
    EXPECT_THAT((std::vector<int>{1, 2, 3}), ::testing::ElementsAre(1, 2, 3));
}
