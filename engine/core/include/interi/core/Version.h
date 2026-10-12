// SPDX-License-Identifier: MIT
#pragma once

#include <string_view>

namespace interi
{

/// Semantic version of the engine, taken from the CMake `project(Interi VERSION ...)` declaration.
struct Version
{
    int major;
    int minor;
    int patch;
};

/// Returns the engine version this binary was built from.
[[nodiscard]] Version GetEngineVersion() noexcept;

/// Returns the engine version as "major.minor.patch".
[[nodiscard]] std::string_view GetEngineVersionString() noexcept;

} // namespace interi
