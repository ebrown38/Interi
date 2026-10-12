// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>

#include <gtest/gtest.h>

#include <charconv>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr int kThreads = 8;
constexpr int kMessagesPerThread = 20'000;

/// Records (thread, sequence) pairs parsed from "t:s" messages. Only the flush thread calls Write.
class SequenceSink final : public interi::ILogSink
{
public:
    explicit SequenceSink(std::shared_ptr<std::vector<std::vector<int>>> received)
        : m_received(std::move(received))
    {
    }

    void Write(const interi::LogRecord& record) override
    {
        const std::string_view text = record.message;
        const std::size_t colon = text.find(':');
        int thread = -1;
        int sequence = -1;
        std::from_chars(text.data(), text.data() + colon, thread);
        std::from_chars(text.data() + colon + 1, text.data() + text.size(), sequence);
        (*m_received)[static_cast<std::size_t>(thread)].push_back(sequence);
    }

private:
    std::shared_ptr<std::vector<std::vector<int>>> m_received;
};
} // namespace

TEST(LoggerStress, ConcurrentWritersFromNThreads_NoLostMessages)
{
    auto received = std::make_shared<std::vector<std::vector<int>>>(kThreads);
    {
        // Small ring so producers regularly hit "full" and must retry while the flush thread drains.
        interi::Logger logger(interi::LoggerConfig{.capacity = 256, .startFlushThread = true});
        logger.AddSink(std::make_unique<SequenceSink>(received));

        std::vector<std::jthread> writers;
        for (int t = 0; t < kThreads; ++t)
        {
            writers.emplace_back(
                [&logger, t]
                {
                    for (int s = 0; s < kMessagesPerThread; ++s)
                    {
                        const std::string message = std::to_string(t) + ":" + std::to_string(s);
                        while (!logger.Log(interi::LogLevel::Info, "Stress", message))
                        {
                            std::this_thread::yield();
                        }
                    }
                });
        }
        writers.clear(); // join
        logger.Flush();
    }

    for (int t = 0; t < kThreads; ++t)
    {
        const auto& sequence = (*received)[static_cast<std::size_t>(t)];
        ASSERT_EQ(sequence.size(), static_cast<std::size_t>(kMessagesPerThread)) << "thread " << t;
        for (int s = 0; s < kMessagesPerThread; ++s)
        {
            // Every message exactly once, and per-producer order preserved.
            ASSERT_EQ(sequence[static_cast<std::size_t>(s)], s) << "thread " << t;
        }
    }
}
