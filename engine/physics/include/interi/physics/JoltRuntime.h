// SPDX-License-Identifier: MIT
#pragma once

namespace interi::physics
{

/// Process-wide Jolt setup: registers Jolt's default allocator, creates the type Factory and registers all types,
/// and routes Jolt's trace output to the Interi log and Jolt asserts (when Jolt is built with them) to INTERI_ASSERT's
/// handler. Construct exactly one for the whole time Jolt is used (in `main`, or a gtest Environment in tests); every
/// shape and PhysicsWorld must be gone before it is destroyed.
///
/// Jolt keeps its own default allocator: its free hooks don't pass a size, so they can't sit on IAllocator.
class JoltRuntime
{
public:
    JoltRuntime();
    ~JoltRuntime();

    JoltRuntime(const JoltRuntime&) = delete;
    JoltRuntime& operator=(const JoltRuntime&) = delete;
    JoltRuntime(JoltRuntime&&) = delete;
    JoltRuntime& operator=(JoltRuntime&&) = delete;

    /// True while a JoltRuntime exists.
    [[nodiscard]] static bool IsInitialized() noexcept;
};

} // namespace interi::physics
