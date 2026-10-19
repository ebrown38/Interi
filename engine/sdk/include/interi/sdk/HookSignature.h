// SPDX-License-Identifier: MIT
#pragma once

// Reading a hook's function type back out of the compiler's spelling (ErasedHook::signature), so tools can show it
// and generate code against it.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace interi::sdk
{

/// A function type "R(A, B)" split into its parts.
struct FunctionType
{
    std::string result;
    std::vector<std::string> arguments; ///< Empty for "R()" and "R(void)".

    /// "R(A, B)".
    [[nodiscard]] std::string Text() const;
};

/// Splits "R (A, B)" (spaces optional; nested templates and parentheses in the types are fine); nullopt when it
/// isn't a function type.
[[nodiscard]] std::optional<FunctionType> ParseFunctionType(std::string_view signature);

/// The hook signature type ("int(int)") out of the compiler's spelling of `SignatureName<T>()` (Clang `... [T = int
/// (int)]`, GCC `... [with T = int(int); ...]`, MSVC `...SignatureName<int __cdecl(int)>(void)`), or nullopt.
[[nodiscard]] std::optional<std::string> ExtractHookSignature(std::string_view spelled);

} // namespace interi::sdk
