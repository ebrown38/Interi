// SPDX-License-Identifier: MIT
#include <interi/jobs/JobSystem.h>
#include <interi/jobs/ParallelAlgorithms.h>
#include <interi/rhi/CommandListPool.h>
#include <interi/rhi/UploadQueue.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

using namespace interi::rhi;
using interi::testing::VulkanTest;

namespace
{

std::vector<std::byte> Pattern(std::size_t size)
{
    std::vector<std::byte> data(size);
    for (std::size_t i = 0; i < size; ++i)
    {
        data[i] = static_cast<std::byte>((i * 131u + (i >> 8u)) & 0xFFu);
    }
    return data;
}

} // namespace

TEST_F(VulkanTest, VulkanDevice_Create_NamesDevice)
{
    EXPECT_FALSE(device->DeviceName().empty());
    EXPECT_EQ(device->LastSubmittedValue(), 0u);
    EXPECT_EQ(device->CompletedValue(), 0u);
}

TEST_F(VulkanTest, VulkanDevice_CreateBufferUpload_ReadbackMatches)
{
    constexpr std::uint64_t kSize = 1024 * 1024;
    const auto data = Pattern(kSize);
    auto gpu = device->CreateBuffer(
        {.size = kSize, .usage = BufferUsage::Storage | BufferUsage::CopySrc | BufferUsage::CopyDst});
    auto readback =
        device->CreateBuffer({.size = kSize, .usage = BufferUsage::CopyDst, .memory = MemoryUsage::Readback});
    auto pool = device->CreateCommandPool();
    ASSERT_TRUE(gpu && readback && pool);
    EXPECT_TRUE(device->MappedMemory(*gpu).empty());
    ASSERT_EQ(device->MappedMemory(*readback).size(), kSize);

    {
        UploadQueue uploads(*device, 256 * 1024); // the 1 MiB upload spans four staging chunks
        ICommandList& commands = device->BeginCommandList(*pool);
        const std::array toCopyDst{BufferBarrier{*gpu, ResourceState::Undefined, ResourceState::CopyDst}};
        commands.Barriers(toCopyDst, {});
        ASSERT_TRUE(uploads.UploadBuffer(commands, *gpu, 0, data).has_value());
        const std::array toCopySrc{BufferBarrier{*gpu, ResourceState::CopyDst, ResourceState::CopySrc},
                                   BufferBarrier{*readback, ResourceState::Undefined, ResourceState::CopyDst}};
        commands.Barriers(toCopySrc, {});
        commands.CopyBuffer(*gpu, 0, *readback, 0, kSize);
        const std::array toHost{BufferBarrier{*readback, ResourceState::CopyDst, ResourceState::HostRead}};
        commands.Barriers(toHost, {});

        ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device->Submit(lists);
        uploads.Submitted(value);
        EXPECT_EQ(value, 1u);
        device->WaitForValue(value);
        EXPECT_GE(device->CompletedValue(), value);
    }

    const auto mapped = device->MappedMemory(*readback);
    EXPECT_EQ(std::memcmp(mapped.data(), data.data(), kSize), 0);

    device->DestroyCommandPool(*pool);
    device->DestroyBuffer(*gpu);
    device->DestroyBuffer(*readback);
}

TEST_F(VulkanTest, VulkanDevice_TextureTransitions_NoValidationErrors)
{
    auto color = device->CreateTexture({.width = 64,
                                        .height = 32,
                                        .format = Format::R8G8B8A8Unorm,
                                        .usage = TextureUsage::ColorTarget | TextureUsage::Sampled,
                                        .debugName = "color"});
    auto depth = device->CreateTexture({.width = 64,
                                        .height = 32,
                                        .format = Format::D32Float,
                                        .usage = TextureUsage::DepthTarget | TextureUsage::Sampled});
    auto pool = device->CreateCommandPool();
    ASSERT_TRUE(color && depth && pool);

    ICommandList& commands = device->BeginCommandList(*pool);
    const std::array toTargets{TextureBarrier{*color, ResourceState::Undefined, ResourceState::ColorTarget},
                               TextureBarrier{*depth, ResourceState::Undefined, ResourceState::DepthWrite}};
    commands.Barriers({}, toTargets);
    const std::array toRead{TextureBarrier{*color, ResourceState::ColorTarget, ResourceState::ShaderRead},
                            TextureBarrier{*depth, ResourceState::DepthWrite, ResourceState::ShaderRead}};
    commands.Barriers({}, toRead);
    ICommandList* const lists[] = {&commands};
    device->WaitForValue(device->Submit(lists));

    device->DestroyCommandPool(*pool);
    device->DestroyTexture(*color);
    device->DestroyTexture(*depth);
}

TEST_F(VulkanTest, CommandListPool_RealDevice_MultithreadedRecordingOverFrames)
{
    interi::JobSystem jobs(interi::JobSystemDesc{.workerCount = 4});
    constexpr std::uint64_t kSize = 4096;
    auto src = device->CreateBuffer({.size = kSize, .usage = BufferUsage::CopySrc, .memory = MemoryUsage::Upload});
    // Each frame writes its own region: frames are not ordered by barriers, so sharing one would be a hazard.
    constexpr int kFrames = 4;
    auto dst = device->CreateBuffer({.size = kSize * kFrames, .usage = BufferUsage::CopyDst | BufferUsage::CopySrc});
    ASSERT_TRUE(src && dst);
    {
        auto pool = CommandListPool::Create(*device, jobs, 2);
        ASSERT_TRUE(pool.has_value());
        for (int frame = 0; frame < kFrames; ++frame)
        {
            pool->BeginFrame();
            std::mutex mutex;
            std::vector<ICommandList*> lists;
            interi::ParallelFor(jobs, 0, 64, 1,
                                [&](std::size_t i)
                                {
                                    ICommandList& commands = pool->Acquire();
                                    const std::uint64_t offset = (i % 64) * 64;
                                    commands.CopyBuffer(*src, offset, *dst,
                                                        static_cast<std::uint64_t>(frame) * kSize + offset, 64);
                                    const std::scoped_lock lock(mutex);
                                    lists.push_back(&commands);
                                });
            pool->EndFrame(device->Submit(lists));
        }
    }
    device->DestroyBuffer(*src);
    device->DestroyBuffer(*dst);
}
