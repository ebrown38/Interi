// SPDX-License-Identifier: MIT
#include <interi/core/Log.h>
#include <interi/testing/ScopedTempDir.h>

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace
{
/// Records every record it receives. Shared ownership lets the test read it after handing it to a logger.
struct CapturedLog
{
    std::mutex mutex;
    std::vector<interi::LogLevel> levels;
    std::vector<std::string> categories;
    std::vector<std::string> messages;
    int flushes = 0;
};

class CaptureSink final : public interi::ILogSink
{
public:
    explicit CaptureSink(std::shared_ptr<CapturedLog> log)
        : m_log(std::move(log))
    {
    }

    void Write(const interi::LogRecord& record) override
    {
        const std::scoped_lock lock(m_log->mutex);
        m_log->levels.push_back(record.level);
        m_log->categories.emplace_back(record.category);
        m_log->messages.emplace_back(record.message);
    }

    void Flush() override
    {
        const std::scoped_lock lock(m_log->mutex);
        ++m_log->flushes;
    }

private:
    std::shared_ptr<CapturedLog> m_log;
};

interi::LoggerConfig ManualConfig(std::size_t capacity)
{
    return {.capacity = capacity, .startFlushThread = false};
}
} // namespace

TEST(Logger, Flush_DeliversMessagesInOrder)
{
    auto captured = std::make_shared<CapturedLog>();
    interi::Logger logger(ManualConfig(16));
    logger.AddSink(std::make_unique<CaptureSink>(captured));

    EXPECT_TRUE(logger.Log(interi::LogLevel::Info, "Core", "first"));
    EXPECT_TRUE(logger.Log(interi::LogLevel::Warning, "Physics", "second"));
    logger.Flush();

    ASSERT_EQ(captured->messages.size(), 2u);
    EXPECT_EQ(captured->messages[0], "first");
    EXPECT_EQ(captured->messages[1], "second");
    EXPECT_EQ(captured->categories[1], "Physics");
    EXPECT_EQ(captured->levels[1], interi::LogLevel::Warning);
    EXPECT_EQ(captured->flushes, 1);
}

TEST(Logger, RingFull_DropsAndCountsOverflow)
{
    auto captured = std::make_shared<CapturedLog>();
    interi::Logger logger(ManualConfig(4));
    logger.AddSink(std::make_unique<CaptureSink>(captured));

    for (int i = 0; i < 4; ++i)
    {
        EXPECT_TRUE(logger.Log(interi::LogLevel::Info, "Core", std::to_string(i)));
    }
    EXPECT_FALSE(logger.Log(interi::LogLevel::Info, "Core", "overflow 1"));
    EXPECT_FALSE(logger.Log(interi::LogLevel::Info, "Core", "overflow 2"));
    EXPECT_EQ(logger.DroppedCount(), 2u);

    logger.Flush();
    ASSERT_EQ(captured->messages.size(), 4u);
    EXPECT_EQ(captured->messages[3], "3");

    // Space is reclaimed after a flush.
    EXPECT_TRUE(logger.Log(interi::LogLevel::Info, "Core", "after"));
    logger.Flush();
    EXPECT_EQ(captured->messages.back(), "after");
}

TEST(Logger, BelowMinLevel_IsFilteredWithoutCountingAsDropped)
{
    auto captured = std::make_shared<CapturedLog>();
    interi::Logger logger(ManualConfig(8));
    logger.AddSink(std::make_unique<CaptureSink>(captured));
    logger.SetMinLevel(interi::LogLevel::Warning);

    EXPECT_FALSE(logger.Log(interi::LogLevel::Debug, "Core", "hidden"));
    EXPECT_TRUE(logger.Log(interi::LogLevel::Error, "Core", "shown"));
    logger.Flush();

    ASSERT_EQ(captured->messages.size(), 1u);
    EXPECT_EQ(captured->messages[0], "shown");
    EXPECT_EQ(logger.DroppedCount(), 0u);
}

TEST(Logger, LongMessage_IsTruncatedNotDropped)
{
    auto captured = std::make_shared<CapturedLog>();
    interi::Logger logger(ManualConfig(4));
    logger.AddSink(std::make_unique<CaptureSink>(captured));

    const std::string longMessage(interi::kMaxLogMessageBytes * 2, 'x');
    EXPECT_TRUE(logger.Log(interi::LogLevel::Info, "Core", longMessage));
    logger.Flush();

    ASSERT_EQ(captured->messages.size(), 1u);
    EXPECT_EQ(captured->messages[0].size(), interi::kMaxLogMessageBytes);
}

TEST(Logger, FlushThread_DeliversWithoutExplicitFlush)
{
    auto captured = std::make_shared<CapturedLog>();
    {
        interi::Logger logger(interi::LoggerConfig{.capacity = 64, .startFlushThread = true});
        logger.AddSink(std::make_unique<CaptureSink>(captured));
        EXPECT_TRUE(logger.Log(interi::LogLevel::Info, "Core", "background"));
    } // destructor stops the thread and drains everything left

    ASSERT_EQ(captured->messages.size(), 1u);
    EXPECT_EQ(captured->messages[0], "background");
}

TEST(Logger, FormatMacro_FormatsArgumentsThroughGlobalLogger)
{
    auto captured = std::make_shared<CapturedLog>();
    interi::Logger logger(ManualConfig(8));
    logger.AddSink(std::make_unique<CaptureSink>(captured));

    interi::Logger* previous = interi::SetGlobalLogger(&logger);
    INTERI_LOG_INFO("Destruction", "chunk {} broke with impulse {:.1f}", 17, 2.5);
    interi::SetGlobalLogger(previous);
    INTERI_LOG_INFO("Destruction", "not delivered: logger detached"); // must not crash

    logger.Flush();
    ASSERT_EQ(captured->messages.size(), 1u);
    EXPECT_EQ(captured->messages[0], "chunk 17 broke with impulse 2.5");
    EXPECT_EQ(captured->categories[0], "Destruction");
}

TEST(Logger, FormatLogLine_ContainsLevelCategoryAndMessage)
{
    const interi::LogRecord record{.level = interi::LogLevel::Error,
                                   .threadId = 3,
                                   .timestampNs = 1'500'000'000,
                                   .category = "Render",
                                   .message = "device lost"};
    const std::string line = interi::FormatLogLine(record);
    EXPECT_NE(line.find("ERROR"), std::string::npos);
    EXPECT_NE(line.find("[Render]"), std::string::npos);
    EXPECT_NE(line.find("device lost"), std::string::npos);
    EXPECT_EQ(line.back(), '\n');
}

TEST(Logger, FileSink_WritesFormattedLines)
{
    const interi::testing::ScopedTempDir dir;
    const auto path = dir.Path() / "interi.log";
    {
        interi::Logger logger(ManualConfig(8));
        auto sink = interi::FileSink::Open(path);
        ASSERT_TRUE(sink.has_value()) << sink.error().message;
        logger.AddSink(std::move(*sink));
        logger.Log(interi::LogLevel::Info, "Core", "to disk");
        logger.Flush();
    }
    std::ifstream file(path);
    std::stringstream contents;
    contents << file.rdbuf();
    EXPECT_NE(contents.str().find("[Core] to disk"), std::string::npos);
}

TEST(Logger, FileSink_UnwritablePath_ReturnsError)
{
    const interi::testing::ScopedTempDir dir;
    const auto sink = interi::FileSink::Open(dir.Path() / "missing_dir" / "interi.log");
    ASSERT_FALSE(sink.has_value());
    EXPECT_EQ(sink.error().code, interi::ErrorCode::IoError);
}
