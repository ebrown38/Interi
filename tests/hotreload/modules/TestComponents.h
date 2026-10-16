// SPDX-License-Identifier: MIT
#pragma once

// Components shared by the hot-reload test modules and the tests (same names and layouts, so the same ids).

#include <cstdint>

namespace interi::testing
{

struct HrCounter
{
    static constexpr const char* kComponentName = "hotreload_test.Counter";
    std::int32_t value = 0;
};

/// Added (through command buffers, from a parallel system) once a counter reaches kMarkThreshold.
struct HrMarked
{
    static constexpr const char* kComponentName = "hotreload_test.Marked";
};

inline constexpr std::int32_t kMarkThreshold = 3;

struct HrHealthV1
{
    static constexpr const char* kComponentName = "hotreload_test.Health";
    float hp = 0.0f;
};

struct HrHealthV2
{
    static constexpr const char* kComponentName = "hotreload_test.Health";
    static constexpr std::uint32_t kComponentVersion = 2;
    float hp = 0.0f;
    float maxHp = 0.0f;
};

/// HrHealthV2::maxHp given to migrated values.
inline constexpr float kMigratedMaxHp = 100.0f;

} // namespace interi::testing
