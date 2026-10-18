// SPDX-License-Identifier: MIT
#pragma once

// Hashes what a chain run produced (Values, the trace) from binary data, for determinism tests.

#include <interi/chain/Executor.h>
#include <interi/testing/StateHasher.h>

#include <cstdint>
#include <string_view>
#include <variant>

namespace interi::chain::testing
{

inline void HashString(interi::testing::StateHasher& hasher, std::string_view text)
{
    hasher.Add(static_cast<std::uint64_t>(text.size()));
    hasher.AddBytes(std::as_bytes(std::span(text.data(), text.size())));
}

inline void HashValue(interi::testing::StateHasher& hasher, const values::Value& value)
{
    hasher.Add(static_cast<std::uint8_t>(value.index()));
    std::visit(
        [&](const auto& v)
        {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, bool> || std::is_same_v<T, std::uint32_t> ||
                          std::is_same_v<T, std::int32_t> || std::is_same_v<T, float>)
            {
                hasher.Add(v);
            }
            else if constexpr (std::is_same_v<T, Vec3>)
            {
                hasher.Add(v.x);
                hasher.Add(v.y);
                hasher.Add(v.z);
            }
            else if constexpr (std::is_same_v<T, values::EntityRef>)
            {
                hasher.Add(v.id);
            }
            else if constexpr (std::is_same_v<T, std::string>)
            {
                HashString(hasher, v);
            }
        },
        value.Variant());
}

/// The executor's run-time Values and last trace.
inline void HashExecutor(interi::testing::StateHasher& hasher, const ChainExecutor& executor)
{
    for (const auto& [id, value] : executor.Values().Overrides())
    {
        HashString(hasher, id);
        HashValue(hasher, value);
    }
    for (const TraceRecord& record : executor.Trace())
    {
        hasher.Add(static_cast<std::uint8_t>(record.kind));
        hasher.Add(record.step);
        hasher.Add(record.chain);
        HashString(hasher, record.node);
        HashString(hasher, record.output);
        hasher.Add(static_cast<std::uint64_t>(record.link));
        hasher.Add(record.entity);
        hasher.Add(record.passed);
        for (const values::Value& value : record.payload)
        {
            HashValue(hasher, value);
        }
    }
    hasher.Add(executor.LastStats().activations);
    hasher.Add(executor.LastStats().eventsRun);
}

} // namespace interi::chain::testing
