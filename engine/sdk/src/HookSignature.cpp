// SPDX-License-Identifier: MIT
#include <interi/sdk/HookSignature.h>

#include <algorithm>
#include <cctype>

namespace interi::sdk
{

namespace
{

std::string Trim(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0)
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0)
    {
        text.remove_suffix(1);
    }
    return std::string(text);
}

/// Removes MSVC's calling convention keywords and collapses the doubled spaces that leaves.
std::string StripCallingConvention(std::string text)
{
    for (const std::string_view keyword : {"__cdecl", "__stdcall", "__fastcall", "__vectorcall"})
    {
        for (std::size_t at = text.find(keyword); at != std::string::npos; at = text.find(keyword, at))
        {
            text.erase(at, keyword.size());
        }
    }
    std::string result;
    for (const char c : text)
    {
        if (c == ' ' && !result.empty() && result.back() == ' ')
        {
            continue;
        }
        result += c;
    }
    return Trim(result);
}

bool Opens(char c) noexcept
{
    return c == '(' || c == '<' || c == '[' || c == '{';
}

bool Closes(char c) noexcept
{
    return c == ')' || c == '>' || c == ']' || c == '}';
}

} // namespace

std::optional<FunctionType> ParseFunctionType(std::string_view signature)
{
    const std::string text = Trim(signature);
    if (text.empty() || text.back() != ')')
    {
        return std::nullopt;
    }
    int depth = 0;
    std::size_t open = std::string::npos;
    for (std::size_t i = text.size(); i-- > 0;)
    {
        if (text[i] == ')')
        {
            ++depth;
        }
        else if (text[i] == '(' && --depth == 0)
        {
            open = i;
            break;
        }
    }
    if (open == std::string::npos || open == 0)
    {
        return std::nullopt;
    }
    FunctionType split;
    split.result = Trim(std::string_view(text).substr(0, open));
    const std::string inner = text.substr(open + 1, text.size() - open - 2);
    depth = 0;
    std::string current;
    for (const char c : inner)
    {
        if (Opens(c))
        {
            ++depth;
        }
        else if (Closes(c))
        {
            --depth;
        }
        if (c == ',' && depth == 0)
        {
            split.arguments.push_back(Trim(current));
            current.clear();
            continue;
        }
        current += c;
    }
    if (const std::string last = Trim(current); !last.empty())
    {
        split.arguments.push_back(last);
    }
    if (split.arguments.size() == 1 && split.arguments[0] == "void")
    {
        split.arguments.clear();
    }
    if (split.result.empty() || std::ranges::any_of(split.arguments, [](const std::string& a) { return a.empty(); }))
    {
        return std::nullopt;
    }
    return split;
}

std::string FunctionType::Text() const
{
    std::string text = result + "(";
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        text += (i == 0 ? "" : ", ") + arguments[i];
    }
    return text + ")";
}

std::optional<std::string> ExtractHookSignature(std::string_view spelled)
{
    std::string type;
    if (const std::size_t at = spelled.find("SignatureName<"); at != std::string_view::npos)
    {
        // MSVC: ...SignatureName<int __cdecl(int)>(void) noexcept
        int depth = 1;
        const std::size_t start = at + std::string_view("SignatureName<").size();
        for (std::size_t i = start; i < spelled.size(); ++i)
        {
            if (spelled[i] == '<')
            {
                ++depth;
            }
            else if (spelled[i] == '>' && --depth == 0)
            {
                type = std::string(spelled.substr(start, i - start));
                break;
            }
        }
    }
    else if (const std::size_t eq = spelled.find("T = "); eq != std::string_view::npos)
    {
        // Clang: [T = int (int)]; GCC: [with T = int(int); std::string_view = ...]
        int depth = 0;
        const std::size_t start = eq + 4;
        std::size_t end = spelled.size();
        for (std::size_t i = start; i < spelled.size(); ++i)
        {
            const char c = spelled[i];
            if (Opens(c))
            {
                ++depth;
            }
            else if (depth == 0 && (c == ']' || c == ';'))
            {
                end = i;
                break;
            }
            else if (Closes(c))
            {
                --depth;
            }
        }
        type = std::string(spelled.substr(start, end - start));
    }
    type = StripCallingConvention(std::move(type));
    if (const std::optional<FunctionType> split = ParseFunctionType(type))
    {
        return split->Text();
    }
    return std::nullopt;
}

} // namespace interi::sdk
