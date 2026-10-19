// SPDX-License-Identifier: MIT
#include <interi/sdk/HookSignature.h>

#include <gtest/gtest.h>

namespace interi::sdk
{
namespace
{

TEST(HookSignature, Clang_Extracted)
{
    EXPECT_EQ(ExtractHookSignature("std::string_view interi::sdk::detail::SignatureName() [T = int (int)]"),
              "int(int)");
    EXPECT_EQ(ExtractHookSignature("... SignatureName() [T = std::vector<int> (const std::map<int, float> &, bool)]"),
              "std::vector<int>(const std::map<int, float> &, bool)");
}

TEST(HookSignature, Gcc_Extracted)
{
    EXPECT_EQ(ExtractHookSignature("constexpr std::string_view interi::sdk::detail::SignatureName() [with T = float(); "
                                   "std::string_view = std::basic_string_view<char>]"),
              "float()");
}

TEST(HookSignature, Msvc_ExtractedWithoutCallingConvention)
{
    EXPECT_EQ(ExtractHookSignature("class std::basic_string_view<char,struct std::char_traits<char> > __cdecl "
                                   "interi::sdk::detail::SignatureName<int __cdecl(int,bool)>(void) noexcept"),
              "int(int, bool)");
    EXPECT_EQ(ExtractHookSignature("SignatureName<void __cdecl(void)>(void)"), "void()");
}

TEST(HookSignature, NotAFunction_Nullopt)
{
    EXPECT_FALSE(ExtractHookSignature("nonsense").has_value());
    EXPECT_FALSE(ExtractHookSignature("[T = int]").has_value());
}

TEST(HookSignature, ParseFunctionType_SplitsTopLevelArguments)
{
    const auto type = ParseFunctionType("std::map<int, float> (const std::pair<int, int>&, void (*)(int))");
    ASSERT_TRUE(type.has_value());
    EXPECT_EQ(type->result, "std::map<int, float>");
    ASSERT_EQ(type->arguments.size(), 2u);
    EXPECT_EQ(type->arguments[1], "void (*)(int)");
    EXPECT_TRUE(ParseFunctionType("void(void)")->arguments.empty());
}

} // namespace
} // namespace interi::sdk
