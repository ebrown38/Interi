// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstdlib>

namespace interi::testing
{

/// The positive integer in environment variable `name`, or `fallback` when it is unset, not a number or not positive.
/// (std::getenv is deprecated on MSVC, which builds with warnings as errors.)
[[nodiscard]] inline std::uint32_t EnvUint(const char* name, std::uint32_t fallback)
{
    long value = 0;
#if defined(_WIN32)
    char* text = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&text, &length, name) == 0 && text != nullptr)
    {
        value = std::strtol(text, nullptr, 10);
        std::free(text);
    }
#else
    if (const char* text = std::getenv(name); text != nullptr)
    {
        value = std::strtol(text, nullptr, 10);
    }
#endif
    return value > 0 ? static_cast<std::uint32_t>(value) : fallback;
}

} // namespace interi::testing
