// SPDX-License-Identifier: MIT
#pragma once

#include <interi/physics/JoltRuntime.h>

namespace interi::benchmarks
{

/// The process's one JoltRuntime, shared by every benchmark file (an inline function's static is one object).
inline void EnsureJolt()
{
    static const physics::JoltRuntime runtime; // lives until exit, after every benchmark's PhysicsWorld
}

} // namespace interi::benchmarks
