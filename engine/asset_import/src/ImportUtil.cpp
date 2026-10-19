// SPDX-License-Identifier: MIT
#include "ImportUtil.h"

#include <interi/platform/FileSystem.h>

#include <algorithm>
#include <cctype>

namespace interi::asset_import
{

std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text)
{
    while (!text.empty() && text.back() == '=')
    {
        text.remove_suffix(1);
    }
    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3 + 3);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char c : text)
    {
        std::uint32_t value = 0;
        if (c >= 'A' && c <= 'Z')
        {
            value = static_cast<std::uint32_t>(c - 'A');
        }
        else if (c >= 'a' && c <= 'z')
        {
            value = static_cast<std::uint32_t>(c - 'a') + 26;
        }
        else if (c >= '0' && c <= '9')
        {
            value = static_cast<std::uint32_t>(c - '0') + 52;
        }
        else if (c == '+')
        {
            value = 62;
        }
        else if (c == '/')
        {
            value = 63;
        }
        else
        {
            return std::nullopt;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            out.push_back(static_cast<std::byte>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

std::string DirectoryOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
}

std::string DecodeUri(std::string_view uri)
{
    std::string out;
    out.reserve(uri.size());
    const auto hex = [](char c) -> int
    {
        if (c >= '0' && c <= '9')
        {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f')
        {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F')
        {
            return c - 'A' + 10;
        }
        return -1;
    };
    for (std::size_t i = 0; i < uri.size(); ++i)
    {
        if (uri[i] == '%' && i + 2 < uri.size() && hex(uri[i + 1]) >= 0 && hex(uri[i + 2]) >= 0)
        {
            out.push_back(static_cast<char>(hex(uri[i + 1]) * 16 + hex(uri[i + 2])));
            i += 2;
        }
        else
        {
            out.push_back(uri[i]);
        }
    }
    return out;
}

Result<std::string> ResolveReference(std::string_view directory, std::string_view uri)
{
    if (uri.empty() || uri.find("://") != std::string_view::npos || uri.starts_with("/") || uri.starts_with("\\"))
    {
        return MakeError(ErrorCode::InvalidArgument, "reference is empty or absolute");
    }
    std::string joined(directory);
    if (!joined.empty())
    {
        joined.push_back('/');
    }
    joined += DecodeUri(uri);
    return NormalizeVirtualPath(joined);
}

std::string ExtensionOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos)
    {
        return {};
    }
    std::string extension(name.substr(dot));
    std::ranges::transform(extension, extension.begin(),
                           [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return extension;
}

std::string StemOf(std::string_view path)
{
    const std::size_t slash = path.find_last_of("/\\");
    const std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    return std::string(dot == std::string_view::npos ? name : name.substr(0, dot));
}

} // namespace interi::asset_import
