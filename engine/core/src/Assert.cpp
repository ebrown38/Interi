// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>

namespace interi
{

namespace
{
std::atomic<AssertHandler> g_handler{nullptr};
} // namespace

AssertAction DefaultAssertHandler(const AssertInfo& info) noexcept
{
    // Plain C stdio: cannot throw or allocate, which matters when reporting from a noexcept function.
    std::fputs(info.file, stderr);
    std::fputc('(', stderr);
    std::array<char, 16> line{};
    std::to_chars(line.data(), line.data() + line.size() - 1, info.line);
    std::fputs(line.data(), stderr);
    std::fputs("): assertion failed: ", stderr);
    std::fputs(info.expression, stderr);
    if (info.message != nullptr)
    {
        std::fputs("\n  message: ", stderr);
        std::fputs(info.message, stderr);
    }
    std::fputc('\n', stderr);
    std::fflush(stderr);
    return AssertAction::Abort;
}

AssertHandler SetAssertHandler(AssertHandler handler) noexcept
{
    const AssertHandler previous = g_handler.exchange(handler, std::memory_order_acq_rel);
    return previous != nullptr ? previous : &DefaultAssertHandler;
}

AssertHandler GetAssertHandler() noexcept
{
    const AssertHandler handler = g_handler.load(std::memory_order_acquire);
    return handler != nullptr ? handler : &DefaultAssertHandler;
}

namespace detail
{
void ReportAssertFailure(const char* expression, const char* message, const char* file, int line) noexcept
{
    const AssertInfo info{.expression = expression, .message = message, .file = file, .line = line};
    if (GetAssertHandler()(info) == AssertAction::Abort)
    {
        std::abort();
    }
}
} // namespace detail

} // namespace interi
