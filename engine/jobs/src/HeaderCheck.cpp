// SPDX-License-Identifier: MIT
// Compiles every public jobs header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy, and explicitly instantiates the templates.
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>
#include <interi/jobs/TaskGraph.h>
#include <interi/jobs/WorkStealingDeque.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace interi
{

template class WorkStealingDeque<std::uint64_t>;

namespace
{

// Instantiates each algorithm once so clang-tidy checks the template bodies. Never called.
[[maybe_unused]] void InstantiateParallelAlgorithms(JobSystem& jobs, std::span<std::uint32_t> keys,
                                                    std::span<std::uint32_t> scratch, std::span<float> values)
{
    ParallelFor(jobs, 0, keys.size(), 64, [keys](std::size_t i) { keys[i] = 0; });
    const float sum = ParallelReduce(
        jobs, 0, values.size(), 64, 0.0f, [values](std::size_t i) { return values[i]; },
        [](float a, float b) { return a + b; });
    (void)sum;
    ParallelPrefixSum(jobs, std::span<const float>(values), values, 64, ScanMode::Exclusive);
    ParallelRadixSort(jobs, keys, scratch);
}

} // namespace

} // namespace interi
