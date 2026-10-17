// SPDX-License-Identifier: MIT
#include <interi/sdk/SemVer.h>

#include <gtest/gtest.h>

#include <string_view>

using interi::sdk::ParseSemVer;
using interi::sdk::ParseVersionRange;
using interi::sdk::SemVer;

namespace
{

SemVer V(std::string_view text)
{
    auto version = ParseSemVer(text);
    EXPECT_TRUE(version.has_value()) << text;
    return version.value_or(SemVer{});
}

bool Matches(std::string_view range, std::string_view version)
{
    auto parsed = ParseVersionRange(range);
    EXPECT_TRUE(parsed.has_value()) << range << ": " << (parsed ? "" : parsed.error().message);
    return parsed.has_value() && parsed->Matches(V(version));
}

} // namespace

TEST(SemVer, Parse_FullVersion_AllFields)
{
    const SemVer version = V("1.22.333-rc.1+build.7");
    EXPECT_EQ(version.major, 1u);
    EXPECT_EQ(version.minor, 22u);
    EXPECT_EQ(version.patch, 333u);
    EXPECT_EQ(version.prerelease, "rc.1");
    EXPECT_EQ(version.build, "build.7");
    EXPECT_EQ(version.ToString(), "1.22.333-rc.1+build.7");
}

TEST(SemVer, Parse_Malformed_Error)
{
    for (const std::string_view text : {"", "1", "1.2", "v1.2.3", "01.2.3", "1.2.3-", "1.2.3+", "1.2.3-a..b", "1.2.x",
                                        "1.2.3 ", "-1.2.3", "1.2.3-01", "99999999999.0.0"})
    {
        EXPECT_FALSE(ParseSemVer(text).has_value()) << text;
    }
}

TEST(SemVer, Compare_FollowsSemverPrecedence)
{
    // The example ordering from semver.org, section 11.
    const std::string_view ordered[] = {"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta",
                                        "1.0.0-beta",  "1.0.0-beta.2",  "1.0.0-beta.11",
                                        "1.0.0-rc.1",  "1.0.0",         "1.0.1",
                                        "1.1.0",       "2.0.0"};
    for (std::size_t i = 0; i + 1 < std::size(ordered); ++i)
    {
        EXPECT_LT(V(ordered[i]), V(ordered[i + 1])) << ordered[i] << " < " << ordered[i + 1];
    }
    EXPECT_EQ(V("1.0.0+a"), V("1.0.0+b")); // build metadata is ignored
}

TEST(VersionRange, Caret_MatchesMinor)
{
    EXPECT_TRUE(Matches("^1.2.3", "1.2.3"));
    EXPECT_TRUE(Matches("^1.2.3", "1.9.0"));
    EXPECT_FALSE(Matches("^1.2.3", "1.2.2"));
    EXPECT_FALSE(Matches("^1.2.3", "2.0.0"));
    EXPECT_TRUE(Matches("^1.2", "1.2.0"));
    EXPECT_TRUE(Matches("^1", "1.99.99"));
    // Below 1.0 the leftmost non-zero field is the compatibility boundary.
    EXPECT_TRUE(Matches("^0.2.3", "0.2.9"));
    EXPECT_FALSE(Matches("^0.2.3", "0.3.0"));
    EXPECT_FALSE(Matches("^0.0.3", "0.0.4"));
}

TEST(VersionRange, Tilde_MatchesPatch)
{
    EXPECT_TRUE(Matches("~1.2.3", "1.2.9"));
    EXPECT_FALSE(Matches("~1.2.3", "1.3.0"));
    EXPECT_TRUE(Matches("~1", "1.5.0"));
    EXPECT_FALSE(Matches("~1", "2.0.0"));
}

TEST(VersionRange, ComparatorsAndAlternatives)
{
    EXPECT_TRUE(Matches(">=1.2 <2", "1.5.0"));
    EXPECT_FALSE(Matches(">=1.2 <2", "2.0.0"));
    EXPECT_TRUE(Matches("<=1.0.0 || >=3", "3.1.0"));
    EXPECT_FALSE(Matches("<=1.0.0 || >=3", "2.0.0"));
    EXPECT_TRUE(Matches("1.2.3", "1.2.3"));
    EXPECT_FALSE(Matches("=1.2.3", "1.2.4"));
    EXPECT_TRUE(Matches("*", "0.0.1"));
    EXPECT_TRUE(Matches("", "7.0.0"));
    EXPECT_FALSE(Matches(">1", "1.0.1")); // npm: ">1" is ">=2.0.0"
    EXPECT_TRUE(Matches(">1", "2.0.0"));
    EXPECT_TRUE(Matches(">1.2.3", "1.2.4"));
}

TEST(VersionRange, Prerelease_OnlyMatchedWhenNamed)
{
    EXPECT_FALSE(Matches("^1.0.0", "1.1.0-beta"));
    EXPECT_TRUE(Matches("^1.1.0-beta", "1.1.0-beta.2"));
    EXPECT_FALSE(Matches("^1.1.0-beta", "1.2.0-beta")); // other tuple
    EXPECT_TRUE(Matches("^1.1.0-beta", "1.2.0"));
}

TEST(VersionRange, Malformed_Error)
{
    for (const std::string_view text : {"^", "~x", ">=", "1.2.3.4", "^1.2.3 ||", "<<1", "1.x", "^v1"})
    {
        EXPECT_FALSE(ParseVersionRange(text).has_value()) << text;
    }
}
