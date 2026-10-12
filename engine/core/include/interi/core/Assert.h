// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Config.h>

#include <cstdint>

namespace interi
{

/// True when INTERI_ASSERT checks are compiled in.
inline constexpr bool kAssertsEnabled = INTERI_ENABLE_ASSERTS != 0;

/// Describes a failed INTERI_ASSERT / INTERI_VERIFY.
struct AssertInfo
{
    const char* expression; ///< Stringified condition.
    const char* message;    ///< Optional user message, or nullptr.
    const char* file;
    int line;
};

/// What the failing assert does after the handler returns.
enum class AssertAction : std::uint8_t
{
    Abort,   ///< Terminate the process (std::abort).
    Continue ///< Resume execution after the assert. Used by tests that check an assert fires.
};

/// Assert handlers may be called from any thread, concurrently. They must not assert themselves.
using AssertHandler = AssertAction (*)(const AssertInfo& info);

/// Installs a process-wide assert handler and returns the previous one. nullptr restores the default
/// handler, which prints the failure to stderr and returns AssertAction::Abort. Thread-safe.
AssertHandler SetAssertHandler(AssertHandler handler) noexcept;

/// Returns the currently installed handler (never nullptr). Thread-safe.
[[nodiscard]] AssertHandler GetAssertHandler() noexcept;

/// The handler used when none is installed: prints to stderr and requests Abort.
AssertAction DefaultAssertHandler(const AssertInfo& info) noexcept;

/// RAII: installs a handler for the current scope and restores the previous one on destruction.
/// Intended for tests; the handler is process-wide, not per-thread.
class ScopedAssertHandler
{
public:
    explicit ScopedAssertHandler(AssertHandler handler) noexcept
        : m_previous(SetAssertHandler(handler))
    {
    }
    ~ScopedAssertHandler() { SetAssertHandler(m_previous); }

    ScopedAssertHandler(const ScopedAssertHandler&) = delete;
    ScopedAssertHandler& operator=(const ScopedAssertHandler&) = delete;
    ScopedAssertHandler(ScopedAssertHandler&&) = delete;
    ScopedAssertHandler& operator=(ScopedAssertHandler&&) = delete;

private:
    AssertHandler m_previous;
};

namespace detail
{
/// Calls the installed handler; aborts if it returns AssertAction::Abort.
void ReportAssertFailure(const char* expression, const char* message, const char* file, int line) noexcept;

constexpr const char* AssertMessage() noexcept
{
    return nullptr;
}
constexpr const char* AssertMessage(const char* message) noexcept
{
    return message;
}
} // namespace detail

} // namespace interi

// INTERI_ASSERT(condition [, "message"])
//   Checked when INTERI_ENABLE_ASSERTS. The condition is not evaluated when asserts are compiled out, so it must
//   have no side effects.
// INTERI_VERIFY(condition [, "message"])
//   The condition is always evaluated (use it to check the result of a call that must happen); it is only
//   reported when asserts are enabled.
#if INTERI_ENABLE_ASSERTS
#define INTERI_ASSERT(condition, ...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        if (!(condition)) [[unlikely]]                                                                                 \
        {                                                                                                              \
            ::interi::detail::ReportAssertFailure(#condition, ::interi::detail::AssertMessage(__VA_ARGS__), __FILE__,  \
                                                  __LINE__);                                                           \
        }                                                                                                              \
    } while (false)
#define INTERI_VERIFY(condition, ...) INTERI_ASSERT(condition, __VA_ARGS__)
#else
#define INTERI_ASSERT(condition, ...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)sizeof(!(condition));                                                                                    \
    } while (false)
#define INTERI_VERIFY(condition, ...)                                                                                  \
    do                                                                                                                 \
    {                                                                                                                  \
        (void)(condition);                                                                                             \
    } while (false)
#endif
