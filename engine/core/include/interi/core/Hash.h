// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace interi
{

/// XXH3 64-bit hash of a byte range. Stable across platforms and builds (safe to persist).
[[nodiscard]] std::uint64_t HashBytes(const void* data, std::size_t size, std::uint64_t seed = 0) noexcept;

/// XXH3 64-bit hash of a string's bytes.
[[nodiscard]] inline std::uint64_t Hash64(std::string_view text, std::uint64_t seed = 0) noexcept
{
    return HashBytes(text.data(), text.size(), seed);
}

/// SplitMix64 finalizer: a bijective mix that spreads every input bit across the result. Used to hash integers
/// for open-addressing tables, which index with the low bits.
[[nodiscard]] constexpr std::uint64_t MixBits(std::uint64_t value) noexcept
{
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

/// Combines a running hash with another value. Order-dependent: HashCombine(a, b) != HashCombine(b, a).
[[nodiscard]] constexpr std::uint64_t HashCombine(std::uint64_t seed, std::uint64_t value) noexcept
{
    return MixBits(seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2)));
}

/// Engine hash functor (used by HashMap). Specialize it for your own key types.
template <typename T, typename Enable = void>
struct Hasher;

template <typename T>
struct Hasher<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>>
{
    [[nodiscard]] constexpr std::uint64_t operator()(T value) const noexcept
    {
        return MixBits(static_cast<std::uint64_t>(value));
    }
};

template <typename T>
struct Hasher<T*>
{
    [[nodiscard]] std::uint64_t operator()(const T* value) const noexcept
    {
        return MixBits(reinterpret_cast<std::uintptr_t>(value));
    }
};

/// Strings hash their contents; transparent, so string_view keys can look up std::string entries.
struct StringHasher
{
    using is_transparent = void;
    [[nodiscard]] std::uint64_t operator()(std::string_view text) const noexcept { return Hash64(text); }
};

template <>
struct Hasher<std::string> : StringHasher
{
};

template <>
struct Hasher<std::string_view> : StringHasher
{
};

} // namespace interi
