// SPDX-License-Identifier: MIT
#include <interi/sdk/SemVer.h>

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <optional>

namespace interi::sdk
{
namespace
{

std::unexpected<Error> Invalid(std::string message)
{
    return MakeError(ErrorCode::InvalidArgument, std::move(message));
}

bool IsDigits(std::string_view text) noexcept
{
    return !text.empty() && std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}

/// A numeric field without leading zeros that fits in 32 bits.
std::optional<std::uint32_t> ParseNumber(std::string_view text) noexcept
{
    if (!IsDigits(text) || (text.size() > 1 && text.front() == '0'))
    {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size())
    {
        return std::nullopt;
    }
    return value;
}

/// Dot-separated identifiers of [0-9A-Za-z-]; numeric pre-release identifiers have no leading zeros.
bool IsValidIdentifiers(std::string_view text, bool numericLeadingZeroAllowed) noexcept
{
    if (text.empty())
    {
        return false;
    }
    std::size_t begin = 0;
    while (true)
    {
        const std::size_t dot = text.find('.', begin);
        const std::string_view part = text.substr(begin, dot == std::string_view::npos ? text.npos : dot - begin);
        if (part.empty())
        {
            return false;
        }
        const bool valid = std::ranges::all_of(
            part, [](char c)
            { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-'; });
        if (!valid || (!numericLeadingZeroAllowed && IsDigits(part) && part.size() > 1 && part.front() == '0'))
        {
            return false;
        }
        if (dot == std::string_view::npos)
        {
            return true;
        }
        begin = dot + 1;
    }
}

std::vector<std::string_view> Split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t begin = 0;
    while (true)
    {
        const std::size_t at = text.find(separator, begin);
        parts.push_back(text.substr(begin, at == std::string_view::npos ? text.npos : at - begin));
        if (at == std::string_view::npos)
        {
            return parts;
        }
        begin = at + 1;
    }
}

std::strong_ordering ComparePrerelease(std::string_view a, std::string_view b) noexcept
{
    if (a.empty() || b.empty())
    {
        // A release ranks above its pre-releases.
        return a.empty() <=> b.empty();
    }
    const std::vector<std::string_view> left = Split(a, '.');
    const std::vector<std::string_view> right = Split(b, '.');
    const std::size_t count = std::min(left.size(), right.size());
    for (std::size_t i = 0; i < count; ++i)
    {
        const bool leftNumeric = IsDigits(left[i]);
        const bool rightNumeric = IsDigits(right[i]);
        if (leftNumeric && rightNumeric)
        {
            // Compare by length first so numbers of any size compare correctly without parsing.
            if (left[i].size() != right[i].size())
            {
                return left[i].size() <=> right[i].size();
            }
        }
        else if (leftNumeric != rightNumeric)
        {
            return leftNumeric ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        if (const auto order = left[i].compare(right[i]); order != 0)
        {
            return order <=> 0;
        }
    }
    return left.size() <=> right.size();
}

/// A version in a range: "1", "1.2" or "1.2.3[-pre]"; missing fields are zero. `fields` reports how many were given.
Result<SemVer> ParsePartial(std::string_view text, int& fields)
{
    const std::size_t plus = text.find('+');
    std::string_view core = text.substr(0, plus);
    std::string_view prerelease;
    if (const std::size_t dash = core.find('-'); dash != std::string_view::npos)
    {
        prerelease = core.substr(dash + 1);
        core = core.substr(0, dash);
        if (!IsValidIdentifiers(prerelease, false))
        {
            return Invalid(std::format("invalid pre-release in '{}'", text));
        }
    }
    const std::vector<std::string_view> parts = Split(core, '.');
    if (parts.size() > 3)
    {
        return Invalid(std::format("too many version fields in '{}'", text));
    }
    SemVer version;
    std::uint32_t* const targets[] = {&version.major, &version.minor, &version.patch};
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        const std::optional<std::uint32_t> number = ParseNumber(parts[i]);
        if (!number)
        {
            return Invalid(std::format("invalid version '{}'", text));
        }
        *targets[i] = *number;
    }
    if (!prerelease.empty() && parts.size() != 3)
    {
        return Invalid(std::format("a pre-release needs a full version: '{}'", text));
    }
    if (plus != std::string_view::npos)
    {
        return Invalid(std::format("build metadata is not allowed in a range: '{}'", text));
    }
    version.prerelease = std::string(prerelease);
    fields = static_cast<int>(parts.size());
    return version;
}

SemVer Release(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
{
    return SemVer{.major = major, .minor = minor, .patch = patch, .prerelease = {}, .build = {}};
}

/// The lowest version of major.minor.patch (its "0" pre-release): an exclusive upper bound.
SemVer Bump(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
{
    return SemVer{.major = major, .minor = minor, .patch = patch, .prerelease = "0", .build = {}};
}

} // namespace

std::string SemVer::ToString() const
{
    std::string text = std::format("{}.{}.{}", major, minor, patch);
    if (!prerelease.empty())
    {
        text += '-';
        text += prerelease;
    }
    if (!build.empty())
    {
        text += '+';
        text += build;
    }
    return text;
}

std::strong_ordering Compare(const SemVer& a, const SemVer& b) noexcept
{
    if (const auto order = a.major <=> b.major; std::is_neq(order))
    {
        return order;
    }
    if (const auto order = a.minor <=> b.minor; std::is_neq(order))
    {
        return order;
    }
    if (const auto order = a.patch <=> b.patch; std::is_neq(order))
    {
        return order;
    }
    return ComparePrerelease(a.prerelease, b.prerelease);
}

Result<SemVer> ParseSemVer(std::string_view text)
{
    std::string_view rest = text;
    std::string_view build;
    if (const std::size_t plus = rest.find('+'); plus != std::string_view::npos)
    {
        build = rest.substr(plus + 1);
        rest = rest.substr(0, plus);
        if (!IsValidIdentifiers(build, true))
        {
            return Invalid(std::format("invalid build metadata in '{}'", text));
        }
    }
    int fields = 0;
    INTERI_TRY_ASSIGN(version, ParsePartial(rest, fields));
    if (fields != 3)
    {
        return Invalid(std::format("a version needs major.minor.patch: '{}'", text));
    }
    version.build = std::string(build);
    return version;
}

bool VersionRange::Matches(const SemVer& version) const noexcept
{
    if (m_sets.empty())
    {
        return true;
    }
    for (const ComparatorSet& set : m_sets)
    {
        bool all = true;
        bool namesPrerelease = false;
        for (const Comparator& comparator : set)
        {
            const std::strong_ordering order = Compare(version, comparator.version);
            bool holds = false;
            switch (comparator.op)
            {
            case Op::Equal:
                holds = std::is_eq(order);
                break;
            case Op::Less:
                holds = std::is_lt(order);
                break;
            case Op::LessEqual:
                holds = std::is_lteq(order);
                break;
            case Op::Greater:
                holds = std::is_gt(order);
                break;
            case Op::GreaterEqual:
                holds = std::is_gteq(order);
                break;
            }
            all = all && holds;
            // Upper bounds made by ^ and ~ carry the "0" pre-release; they don't opt in to pre-releases.
            const SemVer& bound = comparator.version;
            if (bound.IsPrerelease() && (comparator.op != Op::Less || bound.prerelease != "0") &&
                bound.major == version.major && bound.minor == version.minor && bound.patch == version.patch)
            {
                namesPrerelease = true;
            }
        }
        if (all && (!version.IsPrerelease() || namesPrerelease))
        {
            return true;
        }
    }
    return false;
}

Result<VersionRange> ParseVersionRange(std::string_view text)
{
    VersionRange range;
    range.m_text = std::string(text);
    const auto isSpace = [](char c) { return c == ' ' || c == '\t'; };
    const auto trim = [&](std::string_view s)
    {
        while (!s.empty() && isSpace(s.front()))
        {
            s.remove_prefix(1);
        }
        while (!s.empty() && isSpace(s.back()))
        {
            s.remove_suffix(1);
        }
        return s;
    };

    const std::string_view whole = trim(text);
    if (whole.empty() || whole == "*")
    {
        return range;
    }

    std::size_t begin = 0;
    while (true)
    {
        const std::size_t bar = whole.find("||", begin);
        const std::string_view alternative =
            trim(whole.substr(begin, bar == std::string_view::npos ? whole.npos : bar - begin));
        if (alternative.empty())
        {
            return Invalid(std::format("empty alternative in range '{}'", text));
        }

        VersionRange::ComparatorSet set;
        std::size_t at = 0;
        while (at < alternative.size())
        {
            while (at < alternative.size() && isSpace(alternative[at]))
            {
                ++at;
            }
            std::size_t end = at;
            while (end < alternative.size() && !isSpace(alternative[end]))
            {
                ++end;
            }
            std::string_view token = alternative.substr(at, end - at);
            at = end;
            if (token.empty())
            {
                continue;
            }
            if (token == "*")
            {
                set.push_back({.op = VersionRange::Op::GreaterEqual, .version = SemVer{}});
                continue;
            }

            enum class Kind : std::uint8_t
            {
                Caret,
                Tilde,
                Plain
            };
            Kind kind = Kind::Plain;
            VersionRange::Op op = VersionRange::Op::Equal;
            if (token.starts_with('^'))
            {
                kind = Kind::Caret;
                token.remove_prefix(1);
            }
            else if (token.starts_with('~'))
            {
                kind = Kind::Tilde;
                token.remove_prefix(1);
            }
            else if (token.starts_with(">="))
            {
                op = VersionRange::Op::GreaterEqual;
                token.remove_prefix(2);
            }
            else if (token.starts_with("<="))
            {
                op = VersionRange::Op::LessEqual;
                token.remove_prefix(2);
            }
            else if (token.starts_with('>'))
            {
                op = VersionRange::Op::Greater;
                token.remove_prefix(1);
            }
            else if (token.starts_with('<'))
            {
                op = VersionRange::Op::Less;
                token.remove_prefix(1);
            }
            else if (token.starts_with('='))
            {
                token.remove_prefix(1);
            }

            int fields = 0;
            INTERI_TRY_ASSIGN(version, ParsePartial(token, fields));
            if (token.empty())
            {
                return Invalid(std::format("missing version in range '{}'", text));
            }
            const std::uint32_t major = version.major;
            const std::uint32_t minor = version.minor;
            const std::uint32_t patch = version.patch;
            constexpr std::uint32_t kMax = std::numeric_limits<std::uint32_t>::max();
            const auto checkedNext = [&](std::uint32_t value) -> Result<std::uint32_t>
            {
                if (value == kMax)
                {
                    return Invalid(std::format("version out of range in '{}'", text));
                }
                return value + 1;
            };

            if (kind == Kind::Caret)
            {
                SemVer upper;
                if (major != 0 || fields == 1)
                {
                    INTERI_TRY_ASSIGN(next, checkedNext(major));
                    upper = Bump(next, 0, 0);
                }
                else if (minor != 0 || fields == 2)
                {
                    INTERI_TRY_ASSIGN(next, checkedNext(minor));
                    upper = Bump(0, next, 0);
                }
                else
                {
                    INTERI_TRY_ASSIGN(next, checkedNext(patch));
                    upper = Bump(0, 0, next);
                }
                set.push_back({.op = VersionRange::Op::GreaterEqual, .version = version});
                set.push_back({.op = VersionRange::Op::Less, .version = upper});
            }
            else if (kind == Kind::Tilde)
            {
                SemVer upper;
                if (fields == 1)
                {
                    INTERI_TRY_ASSIGN(next, checkedNext(major));
                    upper = Bump(next, 0, 0);
                }
                else
                {
                    INTERI_TRY_ASSIGN(next, checkedNext(minor));
                    upper = Bump(major, next, 0);
                }
                set.push_back({.op = VersionRange::Op::GreaterEqual, .version = version});
                set.push_back({.op = VersionRange::Op::Less, .version = upper});
            }
            else if (op == VersionRange::Op::Equal && fields < 3)
            {
                // "=1.2" means any 1.2.x: [1.2.0, 1.3.0).
                INTERI_TRY_ASSIGN(next, checkedNext(fields == 1 ? major : minor));
                set.push_back({.op = VersionRange::Op::GreaterEqual, .version = version});
                set.push_back(
                    {.op = VersionRange::Op::Less, .version = fields == 1 ? Bump(next, 0, 0) : Bump(major, next, 0)});
            }
            else if (op == VersionRange::Op::Greater && fields < 3)
            {
                // ">1" means >=2.0.0; ">1.2" means >=1.3.0.
                INTERI_TRY_ASSIGN(next, checkedNext(fields == 1 ? major : minor));
                set.push_back({.op = VersionRange::Op::GreaterEqual,
                               .version = fields == 1 ? Release(next, 0, 0) : Release(major, next, 0)});
            }
            else if (op == VersionRange::Op::LessEqual && fields < 3)
            {
                // "<=1.2" means <1.3.0-0.
                INTERI_TRY_ASSIGN(next, checkedNext(fields == 1 ? major : minor));
                set.push_back(
                    {.op = VersionRange::Op::Less, .version = fields == 1 ? Bump(next, 0, 0) : Bump(major, next, 0)});
            }
            else
            {
                if (op == VersionRange::Op::Less && fields < 3)
                {
                    version.prerelease = "0"; // "<2" excludes 2.0.0's pre-releases as well
                }
                set.push_back({.op = op, .version = std::move(version)});
            }
        }
        range.m_sets.push_back(std::move(set));

        if (bar == std::string_view::npos)
        {
            break;
        }
        begin = bar + 2;
    }
    return range;
}

} // namespace interi::sdk
