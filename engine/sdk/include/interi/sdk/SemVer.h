// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

/// A semantic version (semver.org 2.0): major.minor.patch, an optional pre-release ("-beta.2") and optional build
/// metadata ("+git.abc"). Build metadata is kept but ignored by comparisons.
struct SemVer
{
    std::uint32_t major = 0;
    std::uint32_t minor = 0;
    std::uint32_t patch = 0;
    std::string prerelease; ///< Dot-separated identifiers without the '-', empty for a release.
    std::string build;      ///< Without the '+'.

    [[nodiscard]] bool IsPrerelease() const noexcept { return !prerelease.empty(); }
    /// "major.minor.patch[-prerelease][+build]".
    [[nodiscard]] std::string ToString() const;
};

/// Semver precedence: numeric fields, then a release ranks above its pre-releases, whose identifiers compare
/// numerically when both are numeric, else in ASCII order, and a longer list wins a tie.
[[nodiscard]] std::strong_ordering Compare(const SemVer& a, const SemVer& b) noexcept;

[[nodiscard]] inline bool operator==(const SemVer& a, const SemVer& b) noexcept
{
    return Compare(a, b) == std::strong_ordering::equal;
}
[[nodiscard]] inline std::strong_ordering operator<=>(const SemVer& a, const SemVer& b) noexcept
{
    return Compare(a, b);
}

/// Parses a full version "1.2.3", "1.2.3-rc.1+build". InvalidArgument otherwise (no leading zeros, no 'v').
[[nodiscard]] Result<SemVer> ParseSemVer(std::string_view text);

/// A set of versions, written like npm ranges:
///   "1.2.3" or "=1.2.3"  exactly that version
///   "^1.2.3"             >=1.2.3 <2.0.0 (^0.2.3: <0.3.0, ^0.0.3: <0.0.4); "^1.2" = "^1.2.0", "^1" = >=1.0.0 <2.0.0
///   "~1.2.3"             >=1.2.3 <1.3.0; "~1" = >=1.0.0 <2.0.0
///   ">=1.2 <2", ">1", "<=3.0.1"  comparators, separated by spaces, all of which must hold
///   "a || b"             either
///   "*" or ""            any version
/// A pre-release version matches only a comparator set that names a pre-release of the same major.minor.patch
/// (npm's rule), so "^1.0.0" does not pull in "1.1.0-beta".
class VersionRange
{
public:
    /// Matches every version.
    VersionRange() = default;

    [[nodiscard]] bool Matches(const SemVer& version) const noexcept;
    [[nodiscard]] bool MatchesAny() const noexcept { return m_sets.empty(); }
    /// The text it was parsed from.
    [[nodiscard]] const std::string& Text() const noexcept { return m_text; }

private:
    friend Result<VersionRange> ParseVersionRange(std::string_view text);

    enum class Op : std::uint8_t
    {
        Equal,
        Less,
        LessEqual,
        Greater,
        GreaterEqual
    };
    struct Comparator
    {
        Op op = Op::Equal;
        SemVer version;
    };
    using ComparatorSet = std::vector<Comparator>; // all must hold

    std::vector<ComparatorSet> m_sets; // any may hold; empty matches everything
    std::string m_text;
};

/// Parses a range (see VersionRange). InvalidArgument for anything else.
[[nodiscard]] Result<VersionRange> ParseVersionRange(std::string_view text);

} // namespace interi::sdk
