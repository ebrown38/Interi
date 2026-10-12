// SPDX-License-Identifier: MIT
// Compiles every public jobs header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy, and explicitly instantiates the templates.
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/TaskGraph.h>
#include <interi/jobs/WorkStealingDeque.h>

#include <cstdint>

namespace interi
{

template class WorkStealingDeque<std::uint64_t>;

} // namespace interi
