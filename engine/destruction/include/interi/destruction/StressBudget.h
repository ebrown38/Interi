// SPDX-License-Identifier: MIT
#pragma once

#include <interi/destruction/StressSolver.h>

#include <cstdint>
#include <span>

namespace interi
{
class JobSystem;
}

namespace interi::destruction
{

/// A structure waiting for stress solving this frame.
struct StressTask
{
    StressSolver* solver = nullptr;
    float importance = 0.0f; ///< Higher first (e.g. near the player, recently damaged).
};

struct BudgetResult
{
    std::uint64_t workUsed = 0;    ///< Bond-iterations spent.
    std::uint32_t solved = 0;      ///< Solves run (each converged or ran out of budget).
    std::uint32_t unconverged = 0; ///< Tasks still not converged afterwards.
};

/// Spends a per-frame budget of `workBudget` bond-iterations (one CG iteration of a solver costs its bond count)
/// on the tasks in descending importance (ties: lower index first). Converged tasks cost nothing; each other task
/// gets as many iterations as the remaining budget pays for, at least one. Unconverged solves continue next frame
/// from where they stopped (warm start), so a structure always converges eventually.
BudgetResult SolveWithBudget(JobSystem& jobs, std::span<const StressTask> tasks, std::uint64_t workBudget);

} // namespace interi::destruction
