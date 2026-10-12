// SPDX-License-Identifier: MIT
#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>

struct XXH3_state_s; // from xxhash.h, kept out of the public header

namespace interi::testing
{

static_assert(std::endian::native == std::endian::little,
              "StateHasher hashes native byte order; all Interi targets are little-endian");

/// Types whose bytes fully determine their value: no padding (which would be indeterminate) and
/// trivially copyable. Floats are accepted and hashed by exact bit pattern.
template <typename T>
concept Hashable =
    std::is_trivially_copyable_v<T> && (std::has_unique_object_representations_v<T> || std::is_floating_point_v<T>);

/// Streaming XXH3-64 hash of simulation state, used by determinism tests to compare runs across
/// thread counts, builds and platforms. Values are hashed by exact bit pattern, so 0.0f and -0.0f
/// (or two floats one ULP apart) produce different hashes.
class StateHasher
{
public:
    StateHasher();

    void AddBytes(std::span<const std::byte> bytes);

    template <Hashable T>
    void Add(const T& value)
    {
        AddBytes(std::as_bytes(std::span<const T, 1>(&value, 1)));
    }

    template <Hashable T>
    void AddRange(std::span<const T> values)
    {
        AddBytes(std::as_bytes(values));
    }

    /// Hash of everything added so far. Does not reset; more data may be added afterwards.
    [[nodiscard]] std::uint64_t Finish() const;

private:
    struct StateDeleter
    {
        void operator()(XXH3_state_s* state) const noexcept;
    };

    std::unique_ptr<XXH3_state_s, StateDeleter> m_state;
};

} // namespace interi::testing
