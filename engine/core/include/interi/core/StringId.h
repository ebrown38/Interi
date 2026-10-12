// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>
#include <interi/core/Hash.h>

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

#if !defined(INTERI_ENABLE_STRINGID_NAMES)
#define INTERI_ENABLE_STRINGID_NAMES INTERI_ENABLE_ASSERTS
#endif

namespace interi
{

/// True when StringIds remember their names (for DebugName() and collision detection).
inline constexpr bool kStringIdNamesEnabled = INTERI_ENABLE_STRINGID_NAMES != 0;

/// A name reduced to its 64-bit XXH3 hash: compared, hashed and copied as one integer. The value is stable
/// across runs and platforms, so it can be stored in cooked assets.
///
/// When kStringIdNamesEnabled, every constructed id registers its name in a global table, which provides
/// DebugName() and asserts if two different names ever produce the same hash.
class StringId
{
public:
    /// The invalid id (value 0). StringId("") is equal to it.
    constexpr StringId() noexcept = default;

    /// Hashes `name`. Thread-safe.
    explicit StringId(std::string_view name);

    /// Rebuilds an id from a stored Value().
    [[nodiscard]] static constexpr StringId FromValue(std::uint64_t value) noexcept
    {
        StringId id;
        id.m_value = value;
        return id;
    }

    [[nodiscard]] constexpr std::uint64_t Value() const noexcept { return m_value; }
    [[nodiscard]] constexpr bool IsValid() const noexcept { return m_value != 0; }

    /// The original name, or "" if names are compiled out or the id was made with FromValue() for an unseen name.
    /// The view stays valid for the life of the process. Thread-safe.
    [[nodiscard]] std::string_view DebugName() const;

    friend constexpr bool operator==(StringId, StringId) noexcept = default;
    friend constexpr auto operator<=>(StringId, StringId) noexcept = default;

private:
    std::uint64_t m_value = 0;
};

namespace detail
{
/// Registers `name` under an explicit hash and returns the id. Asserts if a different name already owns the hash.
/// Used by StringId's constructor; exposed so tests can force a collision.
StringId RegisterStringId(std::string_view name, std::uint64_t hash);
} // namespace detail

/// The value is already a well-mixed hash.
template <>
struct Hasher<StringId>
{
    [[nodiscard]] constexpr std::uint64_t operator()(StringId id) const noexcept { return id.Value(); }
};

} // namespace interi

template <>
struct std::hash<interi::StringId>
{
    [[nodiscard]] std::size_t operator()(interi::StringId id) const noexcept
    {
        return static_cast<std::size_t>(id.Value());
    }
};
