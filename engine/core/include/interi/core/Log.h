// SPDX-License-Identifier: MIT
#pragma once

#include <interi/core/Result.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace interi
{

enum class LogLevel : std::uint8_t
{
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal,
};

/// Upper-case level name, e.g. "WARN".
[[nodiscard]] std::string_view ToString(LogLevel level) noexcept;

/// Messages longer than this are truncated (not dropped).
inline constexpr std::size_t kMaxLogMessageBytes = 400;
/// Categories longer than this are truncated.
inline constexpr std::size_t kMaxLogCategoryBytes = 31;

/// One message as seen by a sink. The views are only valid for the duration of ILogSink::Write.
struct LogRecord
{
    LogLevel level;
    std::uint32_t threadId;    ///< Small sequential id of the logging thread (not the OS id).
    std::uint64_t timestampNs; ///< Steady-clock nanoseconds since the logging system started.
    std::string_view category;
    std::string_view message;
};

/// Formats a record as "[    1.500000] [ERROR] [T3] [Render] device lost\n".
[[nodiscard]] std::string FormatLogLine(const LogRecord& record);

/// Destination for log records. Sinks are only ever called from one thread at a time (the logger serializes
/// them), so implementations do not need their own locking.
class ILogSink
{
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogRecord& record) = 0;
    /// Pushes buffered output to its destination.
    virtual void Flush() {}

protected:
    ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
    ILogSink(ILogSink&&) = default;
    ILogSink& operator=(ILogSink&&) = default;
};

/// Writes Warning and above to stderr, everything else to stdout.
class ConsoleSink final : public ILogSink
{
public:
    void Write(const LogRecord& record) override;
    void Flush() override;
};

/// Appends formatted lines to a file.
class FileSink final : public ILogSink
{
public:
    /// Opens (truncates) the file. Fails with ErrorCode::IoError if it cannot be created.
    [[nodiscard]] static Result<std::unique_ptr<FileSink>> Open(const std::filesystem::path& path);

    void Write(const LogRecord& record) override;
    void Flush() override;

private:
    explicit FileSink(std::ofstream stream)
        : m_stream(std::move(stream))
    {
    }

    std::ofstream m_stream;
};

struct LoggerConfig
{
    /// Ring buffer slots; rounded up to a power of two. When full, new messages are dropped and counted.
    std::size_t capacity = 2048;
    /// Start a background thread that drains the ring every flushInterval. When false, messages are only
    /// delivered by Flush() (and the destructor), which makes the ring's fill level deterministic for tests.
    bool startFlushThread = true;
    std::chrono::milliseconds flushInterval{5};
};

/// Thread-safe logger. Log() is lock-free (a bounded multi-producer/single-consumer ring of fixed-size slots)
/// and never blocks or allocates, so it is safe to call from job-system workers. Delivery to sinks happens on
/// the flush thread or in Flush().
class Logger
{
public:
    explicit Logger(LoggerConfig config = {});
    /// Stops the flush thread, then delivers everything still in the ring.
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /// Adds a sink. Thread-safe; blocks while the ring is being drained.
    void AddSink(std::unique_ptr<ILogSink> sink);

    /// Enqueues a message. Returns false if it was filtered by the minimum level or dropped because the ring
    /// was full (only the latter increments DroppedCount()). Never blocks. Thread-safe.
    bool Log(LogLevel level, std::string_view category, std::string_view message) noexcept;

    /// Delivers every message whose Log() call returned before this call, then flushes all sinks.
    /// Thread-safe; blocks while another thread is draining.
    void Flush();

    void SetMinLevel(LogLevel level) noexcept;
    [[nodiscard]] LogLevel MinLevel() const noexcept;
    [[nodiscard]] bool IsEnabled(LogLevel level) const noexcept { return level >= MinLevel(); }

    /// Number of messages rejected because the ring was full.
    [[nodiscard]] std::uint64_t DroppedCount() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

/// Installs the logger used by the INTERI_LOG_* macros (nullptr disables them) and returns the previous one.
/// The caller keeps ownership and must uninstall it before destroying it. Thread-safe.
Logger* SetGlobalLogger(Logger* logger) noexcept;
[[nodiscard]] Logger* GetGlobalLogger() noexcept;

/// Formats into a stack buffer (truncating at kMaxLogMessageBytes) and logs through the global logger.
/// Does nothing, and skips formatting, when no logger is installed or the level is filtered.
template <typename... Args>
void LogFormat(LogLevel level, std::string_view category, std::format_string<Args...> format, Args&&... args)
{
    Logger* logger = GetGlobalLogger();
    if (logger == nullptr || !logger->IsEnabled(level))
    {
        return;
    }
    char buffer[kMaxLogMessageBytes];
    const auto result = std::format_to_n(buffer, kMaxLogMessageBytes, format, std::forward<Args>(args)...);
    const auto length = std::min(static_cast<std::size_t>(result.size), kMaxLogMessageBytes);
    logger->Log(level, category, std::string_view(buffer, length));
}

} // namespace interi

#define INTERI_LOG_TRACE(category, ...) ::interi::LogFormat(::interi::LogLevel::Trace, category, __VA_ARGS__)
#define INTERI_LOG_DEBUG(category, ...) ::interi::LogFormat(::interi::LogLevel::Debug, category, __VA_ARGS__)
#define INTERI_LOG_INFO(category, ...) ::interi::LogFormat(::interi::LogLevel::Info, category, __VA_ARGS__)
#define INTERI_LOG_WARN(category, ...) ::interi::LogFormat(::interi::LogLevel::Warning, category, __VA_ARGS__)
#define INTERI_LOG_ERROR(category, ...) ::interi::LogFormat(::interi::LogLevel::Error, category, __VA_ARGS__)
#define INTERI_LOG_FATAL(category, ...) ::interi::LogFormat(::interi::LogLevel::Fatal, category, __VA_ARGS__)
