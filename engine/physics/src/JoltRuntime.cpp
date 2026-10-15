// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Log.h>
#include <interi/physics/JoltRuntime.h>

// clang-format off: Jolt.h must come first
#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/RegisterTypes.h>
// clang-format on

#include <array>
#include <atomic>
#include <cstdarg>
#include <cstdio>

namespace interi::physics
{

namespace
{

std::atomic<bool> g_initialized{false};

// JPH::TraceFunction is printf-style.
void JoltTrace(const char* format, ...) // NOLINT(modernize-avoid-variadic-functions)
{
    std::array<char, 1024> buffer{};
    va_list args;
    va_start(args, format);
    (void)std::vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);
    INTERI_LOG_INFO("Physics", "Jolt: {}", buffer.data());
}

#ifdef JPH_ENABLE_ASSERTS
bool JoltAssertFailed(const char* expression, const char* message, const char* file, JPH::uint line)
{
    detail::ReportAssertFailure(expression, message, file, static_cast<int>(line));
    return false; // the Interi handler decides whether to abort; never break into the debugger from Jolt
}
#endif

} // namespace

JoltRuntime::JoltRuntime()
{
    const bool wasInitialized = g_initialized.exchange(true);
    INTERI_ASSERT(!wasInitialized, "only one JoltRuntime may exist at a time");
    (void)wasInitialized;
    JPH::RegisterDefaultAllocator();
    JPH::Trace = &JoltTrace;
    JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = &JoltAssertFailed;)
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

JoltRuntime::~JoltRuntime()
{
    JPH::UnregisterTypes();
    delete JPH::Factory::sInstance;
    JPH::Factory::sInstance = nullptr;
    g_initialized.store(false);
}

bool JoltRuntime::IsInitialized() noexcept
{
    return g_initialized.load();
}

} // namespace interi::physics
