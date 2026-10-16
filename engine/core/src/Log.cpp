// SPDX-License-Identifier: MIT
#include <interi/core/Assert.h>
#include <interi/core/Config.h>
#include <interi/core/Log.h>

#include <atomic>
#include <bit>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace interi
{

namespace
{
const std::chrono::steady_clock::time_point g_logEpoch = std::chrono::steady_clock::now();
std::atomic<std::uint32_t> g_nextThreadId{1};
std::atomic<Logger*> g_globalLogger{nullptr};

std::uint32_t CurrentLogThreadId() noexcept
{
    thread_local const std::uint32_t id = g_nextThreadId.fetch_add(1, std::memory_order_relaxed);
    return id;
}

std::uint64_t NowNs() noexcept
{
    const auto elapsed = std::chrono::steady_clock::now() - g_logEpoch;
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}
} // namespace

std::string_view ToString(LogLevel level) noexcept
{
    switch (level)
    {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARN";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    }
    return "?";
}

std::string FormatLogLine(const LogRecord& record)
{
    const double seconds = static_cast<double>(record.timestampNs) * 1e-9;
    return std::format("[{:12.6f}] [{}] [T{}] [{}] {}\n", seconds, ToString(record.level), record.threadId,
                       record.category, record.message);
}

// --- Sinks --------------------------------------------------------------------------------------------------------

void ConsoleSink::Write(const LogRecord& record)
{
    const std::string line = FormatLogLine(record);
    std::FILE* stream = record.level >= LogLevel::Warning ? stderr : stdout;
    std::fwrite(line.data(), 1, line.size(), stream);
}

void ConsoleSink::Flush()
{
    std::fflush(stdout);
    std::fflush(stderr);
}

Result<std::unique_ptr<FileSink>> FileSink::Open(const std::filesystem::path& path)
{
    std::ofstream stream(path, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!stream.is_open())
    {
        return MakeError(ErrorCode::IoError, "cannot open log file " + path.string());
    }
    return std::unique_ptr<FileSink>(new FileSink(std::move(stream)));
}

void FileSink::Write(const LogRecord& record)
{
    const std::string line = FormatLogLine(record);
    m_stream.write(line.data(), static_cast<std::streamsize>(line.size()));
}

void FileSink::Flush()
{
    m_stream.flush();
}

// --- Logger -------------------------------------------------------------------------------------------------------

INTERI_MSVC_WARNING_PUSH_DISABLE(4324) // structure padded due to alignas

struct Logger::Impl
{
    /// Clock and thread ids of the binary that created the logger. A hot-reloaded module's copy of this code logs
    /// through the host's logger, so records get the host's epoch and thread numbering rather than its own.
    std::uint32_t (*threadId)() noexcept = &CurrentLogThreadId;
    std::uint64_t (*now)() noexcept = &NowNs;

    /// One message. `sequence` implements Dmitry Vyukov's bounded queue protocol: a slot is writable by the
    /// producer that claimed position p when sequence == p, and readable by the consumer when sequence == p + 1.
    struct Slot
    {
        std::atomic<std::size_t> sequence{0};
        std::uint64_t timestampNs = 0;
        std::uint32_t threadId = 0;
        LogLevel level = LogLevel::Info;
        std::uint8_t categoryLength = 0;
        std::uint16_t messageLength = 0;
        char category[kMaxLogCategoryBytes];
        char message[kMaxLogMessageBytes];
    };

    explicit Impl(const LoggerConfig& config)
        : capacity(std::bit_ceil(std::max<std::size_t>(config.capacity, 2)))
        , mask(capacity - 1)
        , slots(std::make_unique<Slot[]>(capacity))
    {
        for (std::size_t i = 0; i < capacity; ++i)
        {
            slots[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    /// Delivers all published messages to the sinks. Returns the number delivered. Caller holds drainMutex.
    std::size_t DrainLocked()
    {
        std::size_t delivered = 0;
        for (;;)
        {
            Slot& slot = slots[dequeuePosition & mask];
            if (slot.sequence.load(std::memory_order_acquire) != dequeuePosition + 1)
            {
                return delivered; // empty, or the next producer has claimed but not yet published its slot
            }
            const LogRecord record{.level = slot.level,
                                   .threadId = slot.threadId,
                                   .timestampNs = slot.timestampNs,
                                   .category = std::string_view(slot.category, slot.categoryLength),
                                   .message = std::string_view(slot.message, slot.messageLength)};
            for (const auto& sink : sinks)
            {
                sink->Write(record);
            }
            slot.sequence.store(dequeuePosition + capacity, std::memory_order_release);
            ++dequeuePosition;
            ++delivered;
        }
    }

    void FlushSinksLocked()
    {
        for (const auto& sink : sinks)
        {
            sink->Flush();
        }
    }

    void FlushThreadMain(const std::stop_token& stop, std::chrono::milliseconds interval)
    {
        while (!stop.stop_requested())
        {
            {
                std::unique_lock lock(wakeMutex);
                wake.wait_for(lock, stop, interval, [] { return false; });
            }
            const std::scoped_lock lock(drainMutex);
            if (DrainLocked() > 0)
            {
                FlushSinksLocked();
            }
        }
    }

    const std::size_t capacity;
    const std::size_t mask;
    std::unique_ptr<Slot[]> slots;

    alignas(kCacheLineSize) std::atomic<std::size_t> enqueuePosition{0};
    alignas(kCacheLineSize) std::atomic<std::uint64_t> dropped{0};
    std::atomic<LogLevel> minLevel{LogLevel::Trace};

    alignas(kCacheLineSize) std::mutex drainMutex;
    std::size_t dequeuePosition = 0;              // guarded by drainMutex
    std::vector<std::unique_ptr<ILogSink>> sinks; // guarded by drainMutex

    std::mutex wakeMutex;
    std::condition_variable_any wake;
    std::jthread flushThread; // last: stopped before the members it uses are destroyed
};

INTERI_MSVC_WARNING_POP()

Logger::Logger(LoggerConfig config)
    : m_impl(std::make_unique<Impl>(config))
{
    if (config.startFlushThread)
    {
        m_impl->flushThread = std::jthread([impl = m_impl.get(), interval = config.flushInterval](
                                               const std::stop_token& stop) { impl->FlushThreadMain(stop, interval); });
    }
}

Logger::~Logger()
{
    if (m_impl->flushThread.joinable())
    {
        m_impl->flushThread.request_stop();
        m_impl->flushThread.join();
    }
    Flush();
}

void Logger::AddSink(std::unique_ptr<ILogSink> sink)
{
    INTERI_ASSERT(sink != nullptr);
    const std::scoped_lock lock(m_impl->drainMutex);
    m_impl->sinks.push_back(std::move(sink));
}

bool Logger::Log(LogLevel level, std::string_view category, std::string_view message) noexcept
{
    if (!IsEnabled(level))
    {
        return false;
    }

    Impl& impl = *m_impl;
    std::size_t position = impl.enqueuePosition.load(std::memory_order_relaxed);
    Impl::Slot* slot = nullptr;
    for (;;)
    {
        slot = &impl.slots[position & impl.mask];
        const std::size_t sequence = slot->sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::ptrdiff_t>(sequence - position);
        if (diff == 0)
        {
            if (impl.enqueuePosition.compare_exchange_weak(position, position + 1, std::memory_order_relaxed))
            {
                break;
            }
        }
        else if (diff < 0)
        {
            impl.dropped.fetch_add(1, std::memory_order_relaxed);
            return false; // full: the consumer has not released this slot yet
        }
        else
        {
            position = impl.enqueuePosition.load(std::memory_order_relaxed);
        }
    }

    const std::size_t categoryLength = std::min(category.size(), kMaxLogCategoryBytes);
    const std::size_t messageLength = std::min(message.size(), kMaxLogMessageBytes);
    std::memcpy(slot->category, category.data(), categoryLength);
    std::memcpy(slot->message, message.data(), messageLength);
    slot->categoryLength = static_cast<std::uint8_t>(categoryLength);
    slot->messageLength = static_cast<std::uint16_t>(messageLength);
    slot->level = level;
    slot->threadId = impl.threadId();
    slot->timestampNs = impl.now();
    slot->sequence.store(position + 1, std::memory_order_release);
    return true;
}

void Logger::Flush()
{
    const std::scoped_lock lock(m_impl->drainMutex);
    m_impl->DrainLocked();
    m_impl->FlushSinksLocked();
}

void Logger::SetMinLevel(LogLevel level) noexcept
{
    m_impl->minLevel.store(level, std::memory_order_relaxed);
}

LogLevel Logger::MinLevel() const noexcept
{
    return m_impl->minLevel.load(std::memory_order_relaxed);
}

std::uint64_t Logger::DroppedCount() const noexcept
{
    return m_impl->dropped.load(std::memory_order_relaxed);
}

Logger* SetGlobalLogger(Logger* logger) noexcept
{
    return g_globalLogger.exchange(logger, std::memory_order_acq_rel);
}

Logger* GetGlobalLogger() noexcept
{
    return g_globalLogger.load(std::memory_order_acquire);
}

} // namespace interi
