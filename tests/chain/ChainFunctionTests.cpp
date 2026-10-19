// SPDX-License-Identifier: MIT
#include <interi/chain/ChainFunction.h>
#include <interi/ecs/CommandBuffer.h>
#include <interi/ecs/World.h>
#include <interi/jobs/JobSystem.h>

#include "ChainTestUtil.h"
#include <gtest/gtest.h>

#include <string>
#include <string_view>

using namespace interi;
using namespace interi::chain;
using namespace interi::chain::testing;

namespace
{

constexpr std::string_view kDouble = R"json({
  "version": 1,
  "name": "Double",
  "inputs": [{ "name": "n", "type": "int" }],
  "outputs": [{ "name": "Done", "payload": [{ "name": "total", "type": "int" }] }],
  "nodes": [
    { "id": "in", "event": "com.test.a:Double.Entry" },
    { "id": "out", "event": "com.test.a:Double.Return.Done", "params": { "total": { "expr": "payload.n * 2" } } }
  ],
  "links": [{ "from": "in", "to": "out" }]
})json";

constexpr std::string_view kCaller = R"json({
  "version": 1,
  "nodes": [
    { "id": "start", "event": "trigger.Start" },
    { "id": "call", "event": "com.test.a:Double", "params": { "n": { "const": 5 } } },
    { "id": "add", "event": "com.test.a:AddScore", "params": { "delta": { "payload": "total" } } }
  ],
  "links": [{ "from": "start", "to": "call" }, { "from": "call.Done", "to": "add" }]
})json";

ChainFunction Function()
{
    auto function = ParseChainFunction(kDouble);
    EXPECT_TRUE(function.has_value()) << (function ? "" : function.error().message);
    return function ? std::move(*function) : ChainFunction{};
}

FunctionLookup LookupOf(const ChainFunction& function)
{
    return [&function](std::string_view event) -> const ChainFunction*
    { return event == FunctionCallEvent("com.test.a", function.name) ? &function : nullptr; };
}

} // namespace

TEST(ChainFunction, Parse_Write_RoundTrips)
{
    const ChainFunction function = Function();
    EXPECT_EQ(function.inputs.size(), 1u);
    ASSERT_EQ(function.outputs.size(), 1u);
    EXPECT_EQ(function.outputs[0].payload[0].name, "total");
    auto again = ParseChainFunction(WriteChainFunction(function));
    ASSERT_TRUE(again.has_value()) << again.error().message;
    EXPECT_EQ(*again, function);
}

TEST(ChainFunction, Parse_Invalid_Rejected)
{
    EXPECT_FALSE(ParseChainFunction(R"({"name": "x y", "outputs": [{"name": "A"}]})").has_value());
    EXPECT_FALSE(ParseChainFunction(R"({"name": "f"})").has_value()); // no outputs
    EXPECT_FALSE(ParseChainFunction(R"({"name": "f", "inputs": [{"name": "a", "type": "nope"}],
                                        "outputs": [{"name": "A"}]})")
                     .has_value());
    EXPECT_FALSE(ParseChainFunction(R"({"name": "f", "outputs": [{"name": "A"}, {"name": "A"}]})").has_value());
}

TEST(ChainFunction, Register_ProvidesCallEntryAndReturnEvents)
{
    TestProject project;
    const ChainFunction function = Function();
    ASSERT_TRUE(RegisterChainFunction(*project.chains, "com.test.a", function).has_value());
    const EventDefinition* call = project.chains->Events().Resolve("com.test.a:Double");
    ASSERT_NE(call, nullptr);
    ASSERT_EQ(call->outputs.size(), 1u);
    EXPECT_EQ(call->outputs[0].payload[0].type, values::ValueType::Int);
    ASSERT_NE(project.chains->Events().Resolve("com.test.a:Double.Entry"), nullptr);
    ASSERT_NE(project.chains->Events().Resolve("com.test.a:Double.Return.Done"), nullptr);
}

TEST(ChainFunction, Expand_InlinesBodyAndRewiresLinks)
{
    const ChainFunction function = Function();
    auto expanded = ExpandChainFunctions(Chain(kCaller), "com.test.a", LookupOf(function));
    ASSERT_TRUE(expanded.has_value()) << expanded.error().message;
    EXPECT_EQ(expanded->nodes.size(), 4u); // start, call__in, call__out, add
    for (const ChainNode& node : expanded->nodes)
    {
        EXPECT_NE(node.event, "com.test.a:Double");
    }
    const auto in = std::ranges::find(expanded->nodes, "call__in", &ChainNode::id);
    ASSERT_NE(in, expanded->nodes.end());
    ASSERT_EQ(in->params.size(), 1u); // the call's argument moved to Entry
    EXPECT_EQ(in->params[0].first, "n");
    EXPECT_TRUE(std::ranges::any_of(expanded->links, [](const ChainLink& l)
                                    { return l.fromNode == "start" && l.toNode == "call__in"; }));
    EXPECT_TRUE(
        std::ranges::any_of(expanded->links, [](const ChainLink& l)
                            { return l.fromNode == "call__out" && l.fromOutput == "Out" && l.toNode == "add"; }));
}

TEST(ChainFunction, Expand_Executes_ReturnsTypedOutputs)
{
    TestProject project;
    const ChainFunction function = Function();
    ASSERT_TRUE(RegisterChainFunction(*project.chains, "com.test.a", function).has_value());
    auto expanded = ExpandChainFunctions(Chain(kCaller), "com.test.a", LookupOf(function));
    ASSERT_TRUE(expanded.has_value());
    World world;
    JobSystem jobs{JobSystemDesc{.workerCount = 2, .pinThreads = false}};
    CommandBuffers commands{jobs};
    ChainExecutor executor(*project.chains, project.values.get());
    const auto id = executor.AddChain(std::move(*expanded));
    ASSERT_TRUE(executor.IsActive(id)) << (executor.Diagnostics(id).empty() ? "" : executor.Diagnostics(id)[0].Text());
    executor.Step(world, jobs, commands);
    EXPECT_EQ(std::get<std::int32_t>(*executor.Values().Read("com.test.a:Score")), 10);
}

TEST(ChainFunction, Expand_NoEntryNode_Fails)
{
    ChainFunction function = Function();
    function.body.nodes.erase(function.body.nodes.begin());
    function.body.links.clear();
    EXPECT_FALSE(ExpandChainFunctions(Chain(kCaller), "com.test.a", LookupOf(function)).has_value());
}

TEST(ChainFunction, Expand_Recursion_Fails)
{
    ChainFunction function = Function();
    function.body.nodes.push_back({.id = "again", .event = "com.test.a:Double", .params = {}});
    EXPECT_FALSE(ExpandChainFunctions(Chain(kCaller), "com.test.a", LookupOf(function)).has_value());
}

TEST(ChainFunction, Expand_NestedFunctions_Inline)
{
    // Outer calls Double from its body.
    ChainFunction inner = Function();
    ChainFunction outer;
    outer.name = "Outer";
    outer.outputs = {{.name = "Done", .payload = {{.name = "total", .type = values::ValueType::Int}}}};
    outer.body = Chain(R"json({"version": 1, "nodes": [
        {"id": "in", "event": "com.test.a:Outer.Entry"},
        {"id": "d", "event": "com.test.a:Double", "params": {"n": {"const": 4}}},
        {"id": "out", "event": "com.test.a:Outer.Return.Done", "params": {"total": {"payload": "total"}}}],
        "links": [{"from": "in", "to": "d"}, {"from": "d.Done", "to": "out"}]})json");
    const auto lookup = [&](std::string_view event) -> const ChainFunction*
    {
        if (event == "com.test.a:Double")
        {
            return &inner;
        }
        return event == "com.test.a:Outer" ? &outer : nullptr;
    };
    auto expanded = ExpandChainFunctions(Chain(R"json({"version": 1, "nodes": [
        {"id": "s", "event": "trigger.Start"}, {"id": "o", "event": "com.test.a:Outer"}],
        "links": [{"from": "s", "to": "o"}]})json"),
                                         "com.test.a", lookup);
    ASSERT_TRUE(expanded.has_value()) << expanded.error().message;
    for (const ChainNode& node : expanded->nodes)
    {
        EXPECT_NE(node.event, "com.test.a:Double");
        EXPECT_NE(node.event, "com.test.a:Outer");
    }
}
