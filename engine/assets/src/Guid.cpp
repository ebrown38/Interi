// SPDX-License-Identifier: MIT
#include <interi/assets/Guid.h>

#include <random>

namespace interi::assets
{

namespace
{

constexpr std::string_view kDigits = "0123456789abcdef";

std::optional<std::uint64_t> ParseHex16(std::string_view text)
{
    std::uint64_t value = 0;
    for (const char c : text)
    {
        std::uint64_t digit = 0;
        if (c >= '0' && c <= '9')
        {
            digit = static_cast<std::uint64_t>(c - '0');
        }
        else if (c >= 'a' && c <= 'f')
        {
            digit = static_cast<std::uint64_t>(c - 'a') + 10;
        }
        else if (c >= 'A' && c <= 'F')
        {
            digit = static_cast<std::uint64_t>(c - 'A') + 10;
        }
        else
        {
            return std::nullopt;
        }
        value = (value << 4) | digit;
    }
    return value;
}

} // namespace

std::string ToString(const Guid& guid)
{
    std::string text(32, '0');
    for (std::size_t i = 0; i < 16; ++i)
    {
        const std::size_t shift = 60 - 4 * i;
        text[i] = kDigits[(guid.high >> shift) & 0xF];
        text[16 + i] = kDigits[(guid.low >> shift) & 0xF];
    }
    return text;
}

std::optional<Guid> ParseGuid(std::string_view text)
{
    if (text.size() != 32)
    {
        return std::nullopt;
    }
    const auto high = ParseHex16(text.substr(0, 16));
    const auto low = ParseHex16(text.substr(16));
    if (!high || !low)
    {
        return std::nullopt;
    }
    return Guid{.high = *high, .low = *low};
}

Guid NewGuid()
{
    std::random_device device;
    const auto next = [&device] { return (std::uint64_t{device()} << 32) | device(); };
    Guid guid{.high = next(), .low = next()};
    if (guid.IsNull())
    {
        guid.low = 1;
    }
    return guid;
}

} // namespace interi::assets
