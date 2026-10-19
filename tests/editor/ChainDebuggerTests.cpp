// SPDX-License-Identifier: MIT
#include <interi/editor/ChainDebugger.h>

#include <gtest/gtest.h>

using namespace interi;
using namespace interi::editor;
using chain::TraceRecord;

namespace
{

TraceRecord Fired(std::uint64_t step, std::string node, std::string output, std::uint32_t chainId = 0)
{
    TraceRecord record;
    record.kind = TraceRecord::Kind::Fired;
    record.step = step;
    record.node = std::move(node);
    record.output = std::move(output);
    record.chain = chainId;
    return record;
}

TraceRecord Condition(std::uint64_t step, std::size_t link, bool passed)
{
    TraceRecord record;
    record.kind = TraceRecord::Kind::Condition;
    record.step = step;
    record.link = link;
    record.passed = passed;
    return record;
}

const auto kPath = [](std::uint32_t chainId)
{ return chainId == 0 ? std::string_view("a.chain.json") : std::string_view("b.chain.json"); };

} // namespace

TEST(ChainDebugger, Heat_FadesOverSteps)
{
    ChainDebugger debugger;
    EXPECT_FLOAT_EQ(debugger.NodeHeat("n", 5), 0.0f);
    (void)debugger.ObserveRecords({Fired(10, "n", "Out")}, kPath);
    EXPECT_FLOAT_EQ(debugger.NodeHeat("n", 10), 1.0f);
    EXPECT_LT(debugger.NodeHeat("n", 20), 1.0f);
    EXPECT_GT(debugger.NodeHeat("n", 20), 0.0f);
    EXPECT_FLOAT_EQ(debugger.NodeHeat("n", 10 + ChainDebugger::kHeatSteps), 0.0f);
}

TEST(ChainDebugger, NodeBreakpoint_HitsOnFire_AndToggles)
{
    ChainDebugger debugger;
    EXPECT_TRUE(debugger.ToggleNodeBreakpoint("n"));
    EXPECT_TRUE(debugger.HasNodeBreakpoint("n"));
    EXPECT_FALSE(debugger.ObserveRecords({Fired(1, "other", "Out")}, kPath).has_value());
    const auto hit = debugger.ObserveRecords({Fired(2, "n", "Hit")}, kPath);
    ASSERT_TRUE(hit.has_value());
    EXPECT_NE(hit->find("step 2"), std::string::npos);
    EXPECT_EQ(debugger.LastHit(), *hit);
    EXPECT_FALSE(debugger.ToggleNodeBreakpoint("n"));
    EXPECT_FALSE(debugger.ObserveRecords({Fired(3, "n", "Hit")}, kPath).has_value());
}

TEST(ChainDebugger, LinkBreakpoint_AndConditionResults)
{
    ChainDebugger debugger;
    debugger.ToggleLinkBreakpoint(3);
    EXPECT_FALSE(debugger.ObserveRecords({Condition(1, 2, true)}, kPath).has_value());
    EXPECT_TRUE(debugger.ObserveRecords({Condition(1, 3, false)}, kPath).has_value());
    EXPECT_FALSE(debugger.LinkPassed(3));
    EXPECT_TRUE(debugger.LinkPassed(2));
    EXPECT_GT(debugger.LinkHeat(3, 1), 0.0f);
    debugger.ClearBreakpoints();
    EXPECT_FALSE(debugger.HasLinkBreakpoint(3));
}

TEST(ChainDebugger, Target_FiltersChains)
{
    ChainDebugger debugger;
    debugger.SetTarget("b.chain.json");
    debugger.ToggleNodeBreakpoint("n");
    EXPECT_FALSE(debugger.ObserveRecords({Fired(1, "n", "Out", 0)}, kPath).has_value());
    EXPECT_TRUE(debugger.Timeline().empty());
    EXPECT_TRUE(debugger.ObserveRecords({Fired(1, "n", "Out", 1)}, kPath).has_value());
    EXPECT_EQ(debugger.Timeline().size(), 1u);
}

TEST(ChainDebugger, Timeline_IsBounded_ResetClears_BreakpointsStay)
{
    ChainDebugger debugger;
    debugger.ToggleNodeBreakpoint("keep");
    std::vector<TraceRecord> records;
    for (std::uint64_t i = 0; i < ChainDebugger::kTimelineCapacity + 50; ++i)
    {
        records.push_back(Fired(i, "n", "Out"));
    }
    (void)debugger.ObserveRecords(records, kPath);
    EXPECT_EQ(debugger.Timeline().size(), ChainDebugger::kTimelineCapacity);
    EXPECT_EQ(debugger.Timeline().front().step, 50u);
    debugger.Reset();
    EXPECT_TRUE(debugger.Timeline().empty());
    EXPECT_FLOAT_EQ(debugger.NodeHeat("n", 0), 0.0f);
    EXPECT_TRUE(debugger.HasNodeBreakpoint("keep"));
}

TEST(ChainDebugger, Watches_AreUnique_AndRemovable)
{
    ChainDebugger debugger;
    debugger.AddWatch("a");
    debugger.AddWatch("a");
    debugger.AddWatch("");
    EXPECT_EQ(debugger.Watches().size(), 1u);
    debugger.RemoveWatch("a");
    EXPECT_TRUE(debugger.Watches().empty());
}
