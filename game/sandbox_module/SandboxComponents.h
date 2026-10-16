// SPDX-License-Identifier: MIT
#pragma once

// Components shared by the sandbox and its hot-reloadable gameplay module (SandboxModule.cpp): trivially copyable,
// with explicit names, so the module's copies of these types find the sandbox's component ids.

namespace sandbox
{

/// Debris cubes expire after this many seconds (the module's Expire system counts down and destroys them).
struct Lifetime
{
    static constexpr const char* kComponentName = "sandbox.Lifetime";
    float seconds = 0.0f;
};

/// On one entity: the fixed step length, written by the sandbox for the module's systems.
struct StepTime
{
    static constexpr const char* kComponentName = "sandbox.StepTime";
    float seconds = 0.0f;
};

} // namespace sandbox
