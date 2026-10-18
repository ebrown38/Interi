// SPDX-License-Identifier: MIT
#include <interi/chain/Compiler.h>

#include "ChainTestUtil.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <string>

using namespace interi;
using namespace interi::chain;
using namespace interi::chain::testing;

namespace
{

bool HasError(const CompileResult& result, std::string_view node, std::string_view input)
{
    return std::ranges::any_of(
        result.diagnostics, [&](const Diagnostic& d)
        { return d.severity == Diagnostic::Severity::Error && d.node == node && d.input == input; });
}

/// Start -> Shoot -> (Hit) ModifyLike: a second Shoot whose range is bound by `binding`.
std::string TwoShots(std::string_view binding, std::string_view condition = "")
{
    std::string links = R"({"from": "start", "to": "shoot"}, {"from": "shoot.Hit", "to": "again")";
    if (!condition.empty())
    {
        links += std::string(R"(, "condition": )") + std::string(condition);
    }
    links += "}";
    return std::string(R"({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "shoot", "event": "com.test.a:Shoot"},
        {"id": "again", "event": "com.test.a:Shoot", "params": {"range": )") +
           std::string(binding) + R"(}}], "links": [)" + links + "]}";
}

} // namespace

TEST(ChainCompile, ValidChain_FlatProgram)
{
    TestProject project;
    const CompileResult result =
        CompileChain(Chain(TwoShots(R"({"expr": "payload.damage / 2"})", R"("payload.damage > 1")")), *project.chains,
                     project.values.get());
    ASSERT_NE(result.program, nullptr) << (result.diagnostics.empty() ? "" : result.diagnostics[0].Text());
    const ChainProgram& program = *result.program;
    ASSERT_EQ(program.nodes.size(), 3u);
    ASSERT_EQ(program.links.size(), 2u);
    const ProgramNode& shoot = program.nodes[program.NodeIndex("shoot")];
    ASSERT_EQ(shoot.outputLinks.size(), 2u);
    EXPECT_EQ(shoot.outputLinks[0], (std::pair<std::uint32_t, std::uint32_t>{1, 2}));
    EXPECT_EQ(shoot.outputLinks[1], (std::pair<std::uint32_t, std::uint32_t>{2, 2}));
    EXPECT_TRUE(program.links[1].condition.Valid());
    EXPECT_FALSE(program.links[0].condition.Valid());
    ASSERT_EQ(program.links[0].inputs.size(), 1u);
    // An unbound input is its default.
    EXPECT_EQ(EvaluateExpression(program.links[0].inputs[0], {}), values::Value(10.0f));
}

TEST(ChainCompile, BindTypeMismatch_Error)
{
    TestProject project;
    const CompileResult result =
        CompileChain(Chain(TwoShots(R"({"payload": "target"})")), *project.chains, project.values.get());
    EXPECT_EQ(result.program, nullptr);
    ASSERT_TRUE(HasError(result, "again", "range"));
    EXPECT_NE(result.diagnostics[0].message.find("entity"), std::string::npos);
    EXPECT_EQ(result.diagnostics[0].link, std::optional<std::size_t>(1));
}

TEST(ChainCompile, IntBindingToFloatInput_Promoted)
{
    TestProject project;
    const CompileResult result =
        CompileChain(Chain(TwoShots(R"({"value": "com.test.a:Score"})")), *project.chains, project.values.get());
    ASSERT_NE(result.program, nullptr);
    EXPECT_EQ(result.program->links[1].inputs[0].Type(), values::ValueType::Float);
}

TEST(ChainCompile, ConstantOutOfSchema_Error)
{
    TestProject project;
    EXPECT_TRUE(HasError(CompileChain(Chain(TwoShots(R"({"const": "far"})")), *project.chains, project.values.get()),
                         "again", "range"));
}

TEST(ChainCompile, ConditionErrors_LocatedWithSpan)
{
    TestProject project;
    const CompileResult notBool =
        CompileChain(Chain(TwoShots(R"({"const": 1})", R"("payload.damage")")), *project.chains, project.values.get());
    ASSERT_TRUE(HasError(notBool, "shoot", "condition"));

    //                                         0123456789012345678901
    const CompileResult typo = CompileChain(Chain(TwoShots(R"({"const": 1})", R"("payload.damage > true")")),
                                            *project.chains, project.values.get());
    ASSERT_TRUE(HasError(typo, "shoot", "condition"));
    EXPECT_EQ(typo.diagnostics[0].span, (SourceSpan{.begin = 0, .end = 21}));
}

TEST(ChainCompile, PayloadCheckedPerIncomingLink)
{
    TestProject project;
    // `again` is reached from Shoot.Hit (has `damage`) and from Start (no payload): the second link fails.
    const CompileResult result = CompileChain(Chain(R"({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "shoot", "event": "com.test.a:Shoot"},
        {"id": "again", "event": "com.test.a:Shoot", "params": {"range": {"payload": "damage"}}}],
        "links": [{"from": "start", "to": "shoot"}, {"from": "shoot.Hit", "to": "again"},
                  {"from": "start", "to": "again"}]})"),
                                              *project.chains, project.values.get());
    EXPECT_EQ(result.program, nullptr);
    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].link, std::optional<std::size_t>(2));
}

TEST(ChainCompile, Unreachable_Warns)
{
    TestProject project;
    const CompileResult result = CompileChain(Chain(R"({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start"},
        {"id": "shoot", "event": "com.test.a:Shoot"},
        {"id": "orphan", "event": "com.test.a:Shoot"},
        {"id": "child", "event": "com.test.a:Shoot"}],
        "links": [{"from": "start", "to": "shoot"}, {"from": "orphan.Miss", "to": "child"}]})"),
                                              *project.chains, project.values.get());
    ASSERT_NE(result.program, nullptr);
    EXPECT_EQ(result.program->nodes.size(), 2u);
    EXPECT_TRUE(result.program->links.size() == 1u);
    ASSERT_EQ(result.diagnostics.size(), 2u);
    EXPECT_EQ(result.diagnostics[0].severity, Diagnostic::Severity::Warning);
    EXPECT_EQ(result.diagnostics[0].node, "orphan");
    EXPECT_EQ(result.diagnostics[1].node, "child");
}

TEST(ChainCompile, UnknownIds_Error)
{
    TestProject project;
    const CompileResult result = CompileChain(Chain(R"({"version": 1, "nodes": [
        {"id": "start", "event": "trigger.Start", "params": {"bogus": {"const": 1}}},
        {"id": "x", "event": "com.test.a:Nope"},
        {"id": "shoot", "event": "com.test.a:Shoot"}],
        "links": [{"from": "start.Nope", "to": "shoot"}, {"from": "shoot", "to": "start"}]})"),
                                              *project.chains, project.values.get());
    EXPECT_EQ(result.program, nullptr);
    EXPECT_TRUE(HasError(result, "start", "bogus"));
    EXPECT_TRUE(HasError(result, "x", ""));
    EXPECT_EQ(std::ranges::count_if(result.diagnostics, [](const Diagnostic& d) { return d.link.has_value(); }), 2);
}

TEST(ChainCompile, TriggerInputs_Constants)
{
    TestProject project;
    const CompileResult result = CompileChain(Chain(R"({"version": 1, "nodes": [
        {"id": "fire", "event": "trigger.InputAction", "params": {"key": {"const": "input.Fire"}}},
        {"id": "shoot", "event": "com.test.a:Shoot"}],
        "links": [{"from": "fire", "to": "shoot"}]})"),
                                              *project.chains, project.values.get());
    ASSERT_NE(result.program, nullptr);
    const ProgramNode& fire = result.program->nodes[0];
    ASSERT_EQ(fire.triggerInputs.size(), 1u);
    EXPECT_EQ(fire.keyInput, 0u);
    EXPECT_EQ(fire.triggerInputs[0], values::Value(std::string("input.Fire")));

    const CompileResult notConstant = CompileChain(Chain(R"({"version": 1, "nodes": [
        {"id": "fire", "event": "trigger.InputAction", "params": {"key": {"expr": "\"a\" + \"b\""}}}]})"),
                                                   *project.chains, project.values.get());
    EXPECT_TRUE(HasError(notConstant, "fire", "key"));
}

TEST(ChainCompile, Override_ResolvedAtCompile)
{
    TestProject project;
    const ChainAsset asset = Chain(TwoShots(R"({"const": 1})"));
    const CompileResult before = CompileChain(asset, *project.chains, project.values.get());
    ASSERT_NE(before.program, nullptr);
    // Copied: an override reallocates the registry's entries.
    const auto original = project.chains->Events().Resolve("com.test.a:Shoot")->implementation;
    EXPECT_EQ(before.program->nodes[1].implementation, original);

    ASSERT_TRUE(project.chains->Events().Override("com.test.plugin", "com.test.a:Shoot", ShootEvent(3.0f)).has_value());
    const CompileResult after = CompileChain(asset, *project.chains, project.values.get());
    ASSERT_NE(after.program, nullptr);
    EXPECT_NE(after.program->nodes[1].implementation, original);
    EXPECT_EQ(after.program->nodes[1].implementation,
              project.chains->Events().Resolve("com.test.a:Shoot")->implementation);
}
