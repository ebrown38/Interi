// SPDX-License-Identifier: MIT
#include <interi/chain/Expression.h>
#include <interi/chain/Functions.h>
#include <interi/sdk/ContributionRegistry.h>
#include <interi/values/ValueRegistry.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

using namespace interi;
using namespace interi::chain;
using values::Value;
using values::ValueType;

namespace
{

class MapReader final : public IValueReader
{
public:
    std::map<std::string, Value, std::less<>> values;

    [[nodiscard]] const Value* Read(std::string_view id) const override
    {
        const auto it = values.find(id);
        return it == values.end() ? nullptr : &it->second;
    }
};

Value Eval(std::string_view source, const ExprEnvironment& environment = {}, const ExprContext& context = {})
{
    auto compiled = CompileExpression(source, environment);
    EXPECT_TRUE(compiled.has_value()) << source << ": " << (compiled ? "" : compiled.error().message);
    if (!compiled)
    {
        return Value{};
    }
    return EvaluateExpression(*compiled, context);
}

ExprError CompileError(std::string_view source, const ExprEnvironment& environment = {})
{
    auto compiled = CompileExpression(source, environment);
    EXPECT_FALSE(compiled.has_value()) << source;
    return compiled ? ExprError{} : compiled.error();
}

} // namespace

TEST(Expr, TypeMismatch_ErrorSpan)
{
    //                                 0123456789
    const ExprError error = CompileError("2 * (1 + true)");
    EXPECT_EQ(error.span, (SourceSpan{.begin = 5, .end = 13}));
    EXPECT_NE(error.message.find("int"), std::string::npos);
    EXPECT_NE(error.message.find("bool"), std::string::npos);
}

TEST(Expr, Precedence_FollowsConvention)
{
    EXPECT_EQ(Eval("1 + 2 * 3"), Value(std::int32_t{7}));
    EXPECT_EQ(Eval("(1 + 2) * 3"), Value(std::int32_t{9}));
    EXPECT_EQ(Eval("10 - 4 - 3"), Value(std::int32_t{3}));
    EXPECT_EQ(Eval("-2 * 3"), Value(std::int32_t{-6}));
    EXPECT_EQ(Eval("1 + 2 < 4 && 3 == 3 || false"), Value(true));
    EXPECT_EQ(Eval("!true || 7 % 4 == 3"), Value(true));
}

TEST(Expr, MixedIntFloat_PromotesToFloat)
{
    EXPECT_EQ(Eval("1 + 0.5"), Value(1.5f));
    EXPECT_EQ(Eval("3 / 2"), Value(std::int32_t{1}));
    EXPECT_EQ(Eval("3 / 2.0"), Value(1.5f));
    EXPECT_EQ(Eval("2 < 2.5"), Value(true));
}

TEST(Expr, IntegerEdgeCases_NoUndefinedBehaviour)
{
    EXPECT_EQ(Eval("7 / 0"), Value(std::int32_t{0}));
    EXPECT_EQ(Eval("7 % 0"), Value(std::int32_t{0}));
    EXPECT_EQ(Eval("2147483647 + 1"), Value(std::numeric_limits<std::int32_t>::min()));
    EXPECT_EQ(Eval("(-2147483647 - 1) / -1"), Value(std::numeric_limits<std::int32_t>::min()));
    EXPECT_EQ(Eval("(-2147483647 - 1) % -1"), Value(std::int32_t{0}));
    EXPECT_EQ(Eval("abs(-2147483647 - 1)"), Value(std::numeric_limits<std::int32_t>::min()));
    EXPECT_EQ(Eval("int(1e30)"), Value(std::numeric_limits<std::int32_t>::max()));
    EXPECT_EQ(Eval("int(-1e30)"), Value(std::numeric_limits<std::int32_t>::min()));
    EXPECT_EQ(Eval("int(sqrt(-1.0))"), Value(std::int32_t{0}));
    EXPECT_EQ(Eval("int(-2.7)"), Value(std::int32_t{-2}));
}

TEST(Expr, IntLiteralOutOfRange_Error)
{
    const ExprError error = CompileError("1 + 2147483648");
    EXPECT_EQ(error.span, (SourceSpan{.begin = 4, .end = 14}));
}

TEST(Expr, LogicalOperators_ShortCircuit)
{
    // The right side would read a missing payload field if it were evaluated: it reads as zero, but the result
    // proves the side is skipped since false && x is false regardless; use a function that counts calls instead.
    sdk::ModuleSet modules;
    modules.DeclareContract("fn.Count");
    FunctionRegistry functions(modules);
    auto calls = std::make_shared<int>(0);
    ASSERT_TRUE(functions
                    .Provide(sdk::kEngineProviderId, "fn.Count",
                             {.parameters = {},
                              .result = ValueType::Bool,
                              .tooltip = "",
                              .implementation = std::make_shared<const FunctionImpl>(
                                  [calls](std::span<const Value>)
                                  {
                                      ++*calls;
                                      return Value(true);
                                  })})
                    .has_value());
    const ExprEnvironment environment{.payload = {}, .values = nullptr, .functions = &functions};
    EXPECT_EQ(Eval("false && fn.Count()", environment), Value(false));
    EXPECT_EQ(Eval("true || fn.Count()", environment), Value(true));
    EXPECT_EQ(*calls, 0);
    EXPECT_EQ(Eval("true && fn.Count()", environment), Value(true));
    EXPECT_EQ(*calls, 1);
}

TEST(Expr, Vec3Arithmetic_ComponentWise)
{
    EXPECT_EQ(Eval("vec3(1, 2, 3) + vec3(1, 1, 1) * 2"), Value(Vec3(3.0f, 4.0f, 5.0f)));
    EXPECT_EQ(Eval("-vec3(1, 2, 3) / 2"), Value(Vec3(-0.5f, -1.0f, -1.5f)));
    EXPECT_EQ(Eval("vec3(1, 2, 3).y"), Value(2.0f));
    EXPECT_EQ(Eval("length(vec3(3, 4, 0))"), Value(5.0f));
    EXPECT_EQ(Eval("dot(vec3(1, 2, 3), vec3(4, 5, 6))"), Value(32.0f));
    EXPECT_EQ(Eval("vec3(1, 2, 3) == vec3(1, 2, 3)"), Value(true));
}

TEST(Expr, Builtins_Evaluate)
{
    EXPECT_EQ(Eval("min(3, 2)"), Value(std::int32_t{2}));
    EXPECT_EQ(Eval("max(3, 2.5)"), Value(3.0f));
    EXPECT_EQ(Eval("clamp(12, 0, 10)"), Value(std::int32_t{10}));
    EXPECT_EQ(Eval("floor(-1.5)"), Value(-2.0f));
    EXPECT_EQ(Eval("float(3) / 2"), Value(1.5f));
    EXPECT_EQ(Eval("\"ab\" + \"cd\" == \"abcd\""), Value(true));
}

TEST(Expr, FloatLiteral_ParsesExactly)
{
    EXPECT_EQ(Eval("0.1"), Value(0.1f));
    EXPECT_EQ(Eval("1.5e3"), Value(1500.0f));
    EXPECT_EQ(Eval("2e-1"), Value(0.2f));
}

TEST(Expr, PayloadReference_ReadsByField)
{
    const std::vector<PayloadField> fields{{.name = "damage", .type = ValueType::Float},
                                           {.name = "position", .type = ValueType::Vec3},
                                           {.name = "headshot", .type = ValueType::Bool}};
    const std::vector<Value> payload{12.5f, Vec3(1.0f, 2.0f, 3.0f), true};
    const ExprEnvironment environment{.payload = fields, .values = nullptr, .functions = nullptr};
    const ExprContext context{.payload = payload, .values = nullptr, .self = 0};
    EXPECT_EQ(Eval("payload.damage * 2", environment, context), Value(25.0f));
    EXPECT_EQ(Eval("payload.position.z", environment, context), Value(3.0f));
    EXPECT_EQ(Eval("payload.headshot && payload.damage > 10", environment, context), Value(true));
}

TEST(Expr, UnknownPayloadField_ErrorSpan)
{
    const std::vector<PayloadField> fields{{.name = "damage", .type = ValueType::Float}};
    const ExprEnvironment environment{.payload = fields, .values = nullptr, .functions = nullptr};
    //                                     0123456789012345678
    const ExprError error = CompileError("1 + payload.dmage", environment);
    EXPECT_EQ(error.span, (SourceSpan{.begin = 4, .end = 17}));
    EXPECT_NE(error.message.find("dmage"), std::string::npos);
}

TEST(Expr, ValueReference_TypedByDefinitionAndReadAtEvaluation)
{
    sdk::ModuleSet modules;
    modules.AddProvider(
        {.id = "com.test.a", .kind = sdk::ModuleKind::Module, .version = {}, .rank = 1, .overrides = {}});
    values::ValueRegistry registry(modules);
    values::ValueDefinition armor;
    armor.id = "com.test.a:Armor";
    armor.schema.type = ValueType::Int;
    armor.schema.defaultValue = std::int32_t{3};
    ASSERT_TRUE(registry.Add("com.test.a", armor).has_value());

    const ExprEnvironment environment{.payload = {}, .values = &registry, .functions = nullptr};
    auto compiled = CompileExpression("value(\"com.test.a:Armor\") * 2", environment);
    ASSERT_TRUE(compiled.has_value()) << compiled.error().message;
    EXPECT_EQ(compiled->Type(), ValueType::Int);
    ASSERT_EQ(compiled->ValueReads().size(), 1u);
    EXPECT_EQ(compiled->ValueReads()[0], "com.test.a:Armor");

    MapReader reader;
    reader.values["com.test.a:Armor"] = std::int32_t{5};
    EXPECT_EQ(EvaluateExpression(*compiled, {.payload = {}, .values = &reader, .self = 0}), Value(std::int32_t{10}));

    //                                      012345678901234
    const ExprError error = CompileError("value(\"nope\") + 1", environment);
    EXPECT_EQ(error.span, (SourceSpan{.begin = 0, .end = 13}));
}

TEST(Expr, ModuleFunction_OverrideReplacesImplementation)
{
    sdk::ModuleSet modules;
    sdk::Provider owner;
    owner.id = "com.test.a";
    owner.version.major = 1;
    owner.rank = 1;
    modules.AddProvider(owner);
    modules.AddProvider({.id = "com.test.plugin",
                         .kind = sdk::ModuleKind::Plugin,
                         .version = {},
                         .rank = 2,
                         .overrides = {{.target = "com.test.a:Double", .version = {}}}});
    FunctionRegistry functions(modules);
    const auto make = [](float factor)
    {
        return FunctionDefinition{.parameters = {ValueType::Float},
                                  .result = ValueType::Float,
                                  .tooltip = "",
                                  .implementation = std::make_shared<const FunctionImpl>(
                                      [factor](std::span<const Value> arguments)
                                      { return Value(std::get<float>(arguments[0]) * factor); })};
    };
    ASSERT_TRUE(functions.Provide("com.test.a", "com.test.a:Double", make(2.0f)).has_value());
    const ExprEnvironment environment{.payload = {}, .values = nullptr, .functions = &functions};
    EXPECT_EQ(Eval("com.test.a:Double(1.5)", environment), Value(3.0f));

    // A different signature can't override.
    FunctionDefinition wrong = make(3.0f);
    wrong.parameters = {ValueType::Int};
    EXPECT_FALSE(functions.Override("com.test.plugin", "com.test.a:Double", wrong).has_value());

    ASSERT_TRUE(functions.Override("com.test.plugin", "com.test.a:Double", make(3.0f)).has_value());
    EXPECT_EQ(Eval("com.test.a:Double(1.5)", environment), Value(4.5f));

    // Arity and argument types are checked.
    EXPECT_FALSE(CompileExpression("com.test.a:Double(1.5, 2)", environment).has_value());
    EXPECT_FALSE(CompileExpression("com.test.a:Double(true)", environment).has_value());
    // An int argument is promoted to a float parameter.
    EXPECT_EQ(Eval("com.test.a:Double(2)", environment), Value(6.0f));
}

TEST(Expr, UnknownFunction_ErrorSpan)
{
    //                                     0123456789012
    const ExprError error = CompileError("1 + nope(2)");
    EXPECT_EQ(error.span, (SourceSpan{.begin = 4, .end = 8}));
}

TEST(Expr, SyntaxErrors_ReportPosition)
{
    EXPECT_EQ(CompileError("1 +").span.begin, 3u);
    EXPECT_EQ(CompileError("(1 + 2").span.begin, 6u);
    EXPECT_EQ(CompileError("1 $ 2").span, (SourceSpan{.begin = 2, .end = 3}));
    EXPECT_EQ(CompileError("\"open").span.begin, 0u);
    EXPECT_EQ(CompileError("1 2").span.begin, 2u);
    EXPECT_FALSE(CompileExpression("", {}).has_value());
}

TEST(Expr, NonBoolLogic_Error)
{
    EXPECT_FALSE(CompileExpression("1 && true", {}).has_value());
    EXPECT_FALSE(CompileExpression("!1", {}).has_value());
    EXPECT_FALSE(CompileExpression("1 + 2u", {}).has_value());
    EXPECT_FALSE(CompileExpression("vec3(1, 2, 3) < vec3(1, 2, 3)", {}).has_value());
    EXPECT_FALSE(CompileExpression("\"a\" == 1", {}).has_value());
}

TEST(Expr, Self_IsActivationEntity)
{
    const ExprContext context{.payload = {}, .values = nullptr, .self = 42};
    EXPECT_EQ(Eval("self", {}, context), Value(values::EntityRef{.id = 42}));
}

TEST(Expr, Constant_HasOwnType)
{
    const CompiledExpression constant = ConstantExpression(Value(values::EnumValue{.name = "Auto"}));
    EXPECT_EQ(constant.Type(), ValueType::Enum);
    EXPECT_EQ(EvaluateExpression(constant, {}), Value(values::EnumValue{.name = "Auto"}));
}
