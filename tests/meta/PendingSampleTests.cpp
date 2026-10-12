// SPDX-License-Identifier: MIT
// Sample of a TDD "Red" test committed before its implementation exists. It carries the CTest
// label `pending`, which every test preset and CI blocking job excludes. The meta test
// Ci_PendingLabelFilter_ExcludesSample verifies that exclusion keeps working.
#include <gtest/gtest.h>

TEST(PendingSample, DeliberatelyFails_ExcludedFromDefaultRuns)
{
    FAIL() << "Intentional failure: this test must only run when explicitly selected with `ctest -L pending`.";
}
