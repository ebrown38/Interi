// SPDX-License-Identifier: MIT
#include <interi/destruction/StressBudget.h>

#include <algorithm>
#include <numeric>
#include <vector>

namespace interi::destruction
{

BudgetResult SolveWithBudget(JobSystem& jobs, std::span<const StressTask> tasks, std::uint64_t workBudget)
{
    std::vector<std::uint32_t> order(tasks.size());
    std::ranges::iota(order, 0u);
    std::ranges::stable_sort(order, [&](std::uint32_t a, std::uint32_t b)
                             { return tasks[a].importance > tasks[b].importance; });

    BudgetResult result;
    for (const std::uint32_t index : order)
    {
        StressSolver& solver = *tasks[index].solver;
        if (solver.IsConverged())
        {
            continue;
        }
        const std::uint64_t remaining = workBudget - std::min(result.workUsed, workBudget);
        if (remaining == 0)
        {
            ++result.unconverged;
            continue;
        }
        const std::uint64_t cost = std::max<std::uint64_t>(solver.BondCount(), 1);
        const auto iterations = static_cast<std::uint32_t>(std::clamp<std::uint64_t>(remaining / cost, 1, UINT32_MAX));
        const SolveResult solve = solver.Solve(jobs, iterations);
        result.workUsed += cost * solve.iterations;
        ++result.solved;
        if (!solve.converged)
        {
            ++result.unconverged;
        }
    }
    return result;
}

} // namespace interi::destruction
