// SPDX-License-Identifier: MIT
#include <interi/destruction/StressBudget.h>
#include <interi/jobs/JobSystem.h>

#include "StressTestUtil.h"
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace
{

using namespace interi::destruction;
using namespace interi::destruction::testing;
using interi::JobSystem;
using interi::JobSystemDesc;

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

} // namespace

TEST(StressBudget, WorkStaysWithinBudgetPlusOneIteration)
{
    JobSystem jobs(Workers(2));
    const GridStructure wall = MakeGrid(BoxCells(10, 8, 1));
    StressSolver a(wall.graph);
    StressSolver b(wall.graph);
    const std::vector<StressTask> tasks{{&a, 1.0f}, {&b, 0.5f}};
    const std::uint64_t budget = wall.graph.bonds.size() * 10;
    const BudgetResult result = SolveWithBudget(jobs, tasks, budget);
    EXPECT_LE(result.workUsed, budget + wall.graph.bonds.size());
    EXPECT_EQ(result.unconverged, 2u);
}

TEST(StressBudget, MostImportantSolvedFirst)
{
    JobSystem jobs(Workers(2));
    const GridStructure wall = MakeGrid(BoxCells(10, 8, 1));
    StressSolver low(wall.graph);
    StressSolver high(wall.graph);
    const std::vector<StressTask> tasks{{&low, 0.1f}, {&high, 5.0f}};
    // Enough for one full solve, not two.
    StressSolver probe(wall.graph);
    const std::uint32_t full = probe.Solve(jobs, 100000).iterations;
    const BudgetResult result = SolveWithBudget(jobs, tasks, std::uint64_t{full} * wall.graph.bonds.size());
    EXPECT_TRUE(high.IsConverged());
    EXPECT_FALSE(low.IsConverged());
    EXPECT_EQ(result.unconverged, 1u);
}

TEST(StressBudget, UnconvergedFinishesOverFramesWithSameResult)
{
    JobSystem jobs(Workers(2));
    const GridStructure wall = MakeGrid(BoxCells(10, 8, 1));
    StressSolver budgeted(wall.graph);
    StressSolver reference(wall.graph);
    ASSERT_TRUE(reference.Solve(jobs, 100000).converged);
    const std::vector<StressTask> tasks{{&budgeted, 1.0f}};
    int frames = 0;
    while (!budgeted.IsConverged() && frames < 1000)
    {
        (void)SolveWithBudget(jobs, tasks, wall.graph.bonds.size() * 5);
        ++frames;
    }
    EXPECT_TRUE(budgeted.IsConverged());
    EXPECT_GT(frames, 1);
    // Both meet the same tolerance; restarts change the path, not the answer (to solver accuracy).
    for (std::uint32_t node = 0; node < budgeted.NodeCount(); ++node)
    {
        const float expected = reference.Displacement(node).y;
        EXPECT_NEAR(budgeted.Displacement(node).y, expected, std::abs(expected) * 1e-3f + 1e-12f) << node;
    }
}

TEST(StressBudget, ConvergedTasksCostNothing)
{
    JobSystem jobs(Workers(2));
    StressSolver solver(MakeGrid(BoxCells(4, 4, 1)).graph);
    ASSERT_TRUE(solver.Solve(jobs, 100000).converged);
    const std::vector<StressTask> tasks{{&solver, 1.0f}};
    const BudgetResult result = SolveWithBudget(jobs, tasks, 1);
    EXPECT_EQ(result.workUsed, 0u);
    EXPECT_EQ(result.solved, 0u);
    EXPECT_EQ(result.unconverged, 0u);
}
