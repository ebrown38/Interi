// SPDX-License-Identifier: MIT
#pragma once

// Asset identity: a 128-bit id that survives renames and moves (the `.meta` sidecar next to a source file holds it).

#include <compare>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace interi::assets
{

struct Guid
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] bool IsNull() const noexcept { return high == 0 && low == 0; }

    friend constexpr auto operator<=>(const Guid&, const Guid&) noexcept = default;
};

template <typename Archive>
void Serialize(Archive& archive, Guid& guid)
{
    archive(guid.high, guid.low);
}

/// 32 lowercase hex digits; the null guid is 32 zeros.
[[nodiscard]] std::string ToString(const Guid& guid);
/// Exactly 32 hex digits (either case), or nothing.
[[nodiscard]] std::optional<Guid> ParseGuid(std::string_view text);

/// Produces ids for new assets. Tests pass a counter for deterministic metas; the default is random.
using GuidGenerator = std::function<Guid()>;
/// A random, non-null guid.
[[nodiscard]] Guid NewGuid();

} // namespace interi::assets
