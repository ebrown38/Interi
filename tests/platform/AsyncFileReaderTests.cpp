// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/platform/AsyncFileReader.h>
#include <interi/platform/FileSystem.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using interi::AsyncFileReader;
using interi::ErrorCode;
using interi::JobSystem;
using interi::JobSystemDesc;
using interi::MemoryFileSystem;

namespace
{

JobSystemDesc Workers(std::uint32_t count)
{
    JobSystemDesc desc;
    desc.workerCount = count;
    return desc;
}

std::vector<std::byte> Bytes(const std::string& text)
{
    const auto* data = reinterpret_cast<const std::byte*>(text.data());
    return {data, data + text.size()};
}

std::string FileName(int index)
{
    return "files/" + std::to_string(index) + ".bin";
}

class AsyncFileReaderWorkers : public ::testing::TestWithParam<std::uint32_t>
{
};

} // namespace

TEST_P(AsyncFileReaderWorkers, ManyRequests_AllComplete)
{
    constexpr int kFiles = 500;
    MemoryFileSystem fs;
    for (int i = 0; i < kFiles; ++i)
    {
        ASSERT_TRUE(fs.WriteFile(FileName(i), Bytes("content-" + std::to_string(i))).has_value());
    }

    JobSystem jobs(Workers(GetParam()));
    AsyncFileReader reader(jobs, fs);
    std::vector<AsyncFileReader::RequestId> ids;
    ids.reserve(kFiles);
    for (int i = 0; i < kFiles; ++i)
    {
        ids.push_back(reader.Request(FileName(i)));
    }
    reader.WaitAll();
    EXPECT_EQ(reader.PendingCount(), 0u);

    for (int i = 0; i < kFiles; ++i)
    {
        const auto id = ids[static_cast<std::size_t>(i)];
        ASSERT_TRUE(reader.IsDone(id));
        auto result = reader.TryTake(id);
        ASSERT_TRUE(result.has_value());
        ASSERT_TRUE(result->has_value());
        EXPECT_EQ((*result)->size(), ("content-" + std::to_string(i)).size());
        EXPECT_EQ(**result, Bytes("content-" + std::to_string(i)));
    }
}

INSTANTIATE_TEST_SUITE_P(Workers, AsyncFileReaderWorkers, ::testing::Values(0u, 1u, 4u));

TEST(AsyncFileReader, MissingFile_CompletesWithNotFound)
{
    MemoryFileSystem fs;
    JobSystem jobs(Workers(2));
    AsyncFileReader reader(jobs, fs);
    const auto id = reader.Request("missing.bin");
    reader.WaitAll();
    auto result = reader.TryTake(id);
    ASSERT_TRUE(result.has_value());
    ASSERT_FALSE(result->has_value());
    EXPECT_EQ(result->error().code, ErrorCode::NotFound);
}

TEST(AsyncFileReader, Poll_WithWorkers_CompletesWithoutWaiting)
{
    MemoryFileSystem fs;
    ASSERT_TRUE(fs.WriteFile("a.txt", Bytes("a")).has_value());
    JobSystem jobs(Workers(2));
    AsyncFileReader reader(jobs, fs);
    const auto id = reader.Request("a.txt");
    while (!reader.IsDone(id))
    {
        std::this_thread::yield();
    }
    auto result = reader.TryTake(id);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value(), Bytes("a"));
}

TEST(AsyncFileReader, TryTake_ConsumesResultOnce)
{
    MemoryFileSystem fs;
    ASSERT_TRUE(fs.WriteFile("a.txt", Bytes("a")).has_value());
    JobSystem jobs(Workers(1));
    AsyncFileReader reader(jobs, fs);
    const auto id = reader.Request("a.txt");
    reader.WaitAll();
    EXPECT_TRUE(reader.TryTake(id).has_value());
    EXPECT_FALSE(reader.TryTake(id).has_value());
    EXPECT_FALSE(reader.IsDone(id));
    EXPECT_FALSE(reader.TryTake(AsyncFileReader::RequestId{9999}).has_value());
}

TEST(AsyncFileReader, DestroyWithRequestsInFlight_Drains)
{
    MemoryFileSystem fs;
    for (int i = 0; i < 64; ++i)
    {
        ASSERT_TRUE(fs.WriteFile(FileName(i), Bytes("x")).has_value());
    }
    JobSystem jobs(Workers(4));
    for (int round = 0; round < 20; ++round)
    {
        AsyncFileReader reader(jobs, fs);
        for (int i = 0; i < 64; ++i)
        {
            (void)reader.Request(FileName(i));
        }
    } // destructor must wait for the jobs, which reference the reader
}
