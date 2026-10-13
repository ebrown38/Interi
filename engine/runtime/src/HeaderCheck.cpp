// SPDX-License-Identifier: MIT
// Compiles every public runtime header in a library translation unit so header-only code is also built with the
// engine's warnings and checked by clang-tidy, and instantiates the templates.
#include <interi/runtime/FixedStepClock.h>
#include <interi/runtime/FramePipeline.h>
#include <interi/runtime/Interpolation.h>

namespace interi
{

template class FramePipeline<int>;

namespace
{

// Instantiates RunFrame once so clang-tidy checks its body. Never called.
[[maybe_unused]] void InstantiateFramePipeline(FramePipeline<int>& pipeline)
{
    pipeline.RunFrame([](int& out) { out = 1; }, [](const int& in) { (void)in; });
}

} // namespace

} // namespace interi
