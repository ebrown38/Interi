// SPDX-License-Identifier: MIT
#include <interi/core/Version.h>

#include <gtest/gtest.h>

#include <string>

TEST(Version, GetEngineVersion_MatchesCMakeProjectVersion)
{
    const interi::Version version = interi::GetEngineVersion();
    EXPECT_EQ(version.major, INTERI_TEST_EXPECTED_VERSION_MAJOR);
    EXPECT_EQ(version.minor, INTERI_TEST_EXPECTED_VERSION_MINOR);
    EXPECT_EQ(version.patch, INTERI_TEST_EXPECTED_VERSION_PATCH);
}

TEST(Version, GetEngineVersionString_FormatsMajorMinorPatch)
{
    const interi::Version version = interi::GetEngineVersion();
    const std::string expected =
        std::to_string(version.major) + "." + std::to_string(version.minor) + "." + std::to_string(version.patch);
    EXPECT_EQ(interi::GetEngineVersionString(), expected);
}
