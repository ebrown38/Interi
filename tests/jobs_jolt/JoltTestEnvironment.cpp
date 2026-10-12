// SPDX-License-Identifier: MIT
// Process-wide Jolt setup for the jobs_jolt tests: allocator, factory and type registration happen once.
// clang-format off
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdio>

namespace
{

void JoltTrace(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fputc('\n', stderr);
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    ADD_FAILURE_AT(file, static_cast<int>(line))
        << "Jolt assert: " << expression << (message != nullptr ? message : "");
    return false; // don't break into the debugger
}
#endif

class JoltEnvironment final : public ::testing::Environment
{
public:
    void SetUp() override
    {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = &JoltTrace;
        JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &JoltAssertFailed;)
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }

    void TearDown() override
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
};

[[maybe_unused]] ::testing::Environment* const g_joltEnvironment =
    ::testing::AddGlobalTestEnvironment(new JoltEnvironment());

} // namespace
