// SPDX-License-Identifier: MIT
#include <interi/core/math/Mat4.h>
#include <interi/core/math/Quat.h>
#include <interi/rhi/UploadQueue.h>
#include <interi/rhi_vulkan/ShaderReflection.h>
#include <interi/shaders/MatrixConvention.cs.h>
#include <interi/shaders/PrefixSum.cs.h>
#include <interi/testing/DeterministicRng.h>
#include <interi/testing/ExpectNear.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <span>
#include <vector>

using namespace interi;
using namespace interi::rhi;
using interi::testing::VulkanTest;

namespace
{

struct PrefixSumPush
{
    std::uint32_t data;
    std::uint32_t blockSums;
    std::uint32_t count;
    std::uint32_t mode;
};

constexpr std::uint32_t kNoBuffer = 0xFFFF'FFFFu;
constexpr std::uint32_t kGroupSize = 256;

BufferBarrier WriteToWrite(BufferHandle buffer)
{
    return {buffer, ResourceState::ShaderWrite, ResourceState::ShaderWrite};
}

/// Uploads `data`, runs `record`, copies `result` back and returns its bytes.
template <typename Record>
std::vector<std::byte> RunAndRead(IDevice& device, BufferHandle target, std::span<const std::byte> data,
                                  BufferHandle result, std::uint64_t resultSize, Record&& record)
{
    auto readback =
        device.CreateBuffer({.size = resultSize, .usage = BufferUsage::CopyDst, .memory = MemoryUsage::Readback});
    auto pool = device.CreateCommandPool();
    EXPECT_TRUE(readback && pool);
    std::vector<std::byte> out(resultSize);
    {
        UploadQueue uploads(device, 1024 * 1024);
        ICommandList& commands = device.BeginCommandList(*pool);
        const std::array toCopy{BufferBarrier{target, ResourceState::Undefined, ResourceState::CopyDst}};
        commands.Barriers(toCopy, {});
        EXPECT_TRUE(uploads.UploadBuffer(commands, target, 0, data).has_value());
        const std::array toShader{BufferBarrier{target, ResourceState::CopyDst, ResourceState::ShaderWrite}};
        commands.Barriers(toShader, {});
        record(commands);
        const std::array toRead{BufferBarrier{result, ResourceState::ShaderWrite, ResourceState::CopySrc},
                                BufferBarrier{*readback, ResourceState::Undefined, ResourceState::CopyDst}};
        commands.Barriers(toRead, {});
        commands.CopyBuffer(result, 0, *readback, 0, resultSize);
        const std::array toHost{BufferBarrier{*readback, ResourceState::CopyDst, ResourceState::HostRead}};
        commands.Barriers(toHost, {});
        ICommandList* const lists[] = {&commands};
        const std::uint64_t value = device.Submit(lists);
        uploads.Submitted(value);
        device.WaitForValue(value);
    }
    std::memcpy(out.data(), device.MappedMemory(*readback).data(), resultSize);
    device.DestroyCommandPool(*pool);
    device.DestroyBuffer(*readback);
    return out;
}

} // namespace

TEST(ShaderReflection, PrefixSum_ReportsWorkgroupAndPushConstants)
{
    const auto reflection = ReflectSpirv(std::as_bytes(std::span(kPrefixSumCs)));
    ASSERT_TRUE(reflection.has_value()) << reflection.error().message;
    EXPECT_EQ(reflection->stage, ShaderStage::Compute);
    EXPECT_EQ(reflection->workgroupSize, (std::array<std::uint32_t, 3>{256, 1, 1}));
    EXPECT_EQ(reflection->pushConstantSize, sizeof(PrefixSumPush));
}

TEST(ShaderReflection, NotSpirv_ReturnsError)
{
    const std::array<std::uint32_t, 4> garbage{1, 2, 3, 4};
    EXPECT_FALSE(ReflectSpirv(std::as_bytes(std::span(garbage))).has_value());
}

TEST_F(VulkanTest, ComputeDispatch_PrefixSum_MatchesCPU)
{
    constexpr std::uint32_t kCount = kGroupSize * kGroupSize - 77; // two levels, last block partial
    constexpr std::uint32_t kBlocks = (kCount + kGroupSize - 1) / kGroupSize;
    interi::testing::DeterministicRng rng(1234);
    std::vector<std::uint32_t> values(kCount);
    for (auto& value : values)
    {
        value = static_cast<std::uint32_t>(rng.NextU64() % 1000);
    }
    std::vector<std::uint32_t> expected(kCount);
    std::inclusive_scan(values.begin(), values.end(), expected.begin());

    auto pipeline = device->CreateComputePipeline({.code = std::as_bytes(std::span(kPrefixSumCs))});
    auto data = device->CreateBuffer(
        {.size = kCount * 4ull, .usage = BufferUsage::Storage | BufferUsage::CopyDst | BufferUsage::CopySrc});
    auto sums = device->CreateBuffer({.size = kBlocks * 4ull, .usage = BufferUsage::Storage});
    ASSERT_TRUE(pipeline && data && sums) << (pipeline ? "" : pipeline.error().message);
    const std::uint32_t dataIndex = device->BindlessIndex(*data);
    const std::uint32_t sumsIndex = device->BindlessIndex(*sums);
    EXPECT_NE(dataIndex, sumsIndex);

    const auto bytes = RunAndRead(*device, *data, std::as_bytes(std::span(values)), *data, kCount * 4ull,
                                  [&](ICommandList& commands)
                                  {
                                      commands.BindPipeline(*pipeline);
                                      const std::array sumsFirstUse{
                                          BufferBarrier{*sums, ResourceState::Undefined, ResourceState::ShaderWrite}};
                                      commands.Barriers(sumsFirstUse, {});
                                      commands.PushConstants(PrefixSumPush{dataIndex, sumsIndex, kCount, 0});
                                      commands.Dispatch(kBlocks, 1, 1);
                                      const std::array afterBlocks{WriteToWrite(*sums)};
                                      commands.Barriers(afterBlocks, {});
                                      commands.PushConstants(PrefixSumPush{sumsIndex, kNoBuffer, kBlocks, 0});
                                      commands.Dispatch(1, 1, 1);
                                      const std::array afterSums{WriteToWrite(*sums), WriteToWrite(*data)};
                                      commands.Barriers(afterSums, {});
                                      commands.PushConstants(PrefixSumPush{dataIndex, sumsIndex, kCount, 1});
                                      commands.Dispatch(kBlocks, 1, 1);
                                  });

    std::vector<std::uint32_t> result(kCount);
    std::memcpy(result.data(), bytes.data(), bytes.size());
    EXPECT_EQ(result, expected);

    device->DestroyPipeline(*pipeline);
    device->DestroyBuffer(*data);
    device->DestroyBuffer(*sums);
}

TEST_F(VulkanTest, ComputeDispatch_MatrixTimesVector_MatchesCpuConvention)
{
    // A non-symmetric transform so a transposed or reversed multiply is caught.
    const Mat4 m = Mat4::FromTRS({1.0f, -2.0f, 3.5f}, Normalize(Quat{0.2f, 0.4f, -0.1f, 0.9f}), {2.0f, 0.5f, 1.5f});
    const Vec4 v{0.25f, -1.0f, 2.0f, 1.0f};
    const Vec4 expected = m * v;

    struct Input
    {
        Mat4 m;
        Vec4 v;
    } input{m, v};
    static_assert(sizeof(Input) == 80);

    struct Push
    {
        std::uint32_t input;
        std::uint32_t output;
    };
    auto pipeline = device->CreateComputePipeline({.code = std::as_bytes(std::span(kMatrixConventionCs))});
    auto in = device->CreateBuffer({.size = sizeof(Input), .usage = BufferUsage::Storage | BufferUsage::CopyDst});
    auto out = device->CreateBuffer({.size = sizeof(Vec4), .usage = BufferUsage::Storage | BufferUsage::CopySrc});
    ASSERT_TRUE(pipeline && in && out);

    const auto bytes = RunAndRead(
        *device, *in, std::as_bytes(std::span(&input, 1)), *out, sizeof(Vec4),
        [&](ICommandList& commands)
        {
            const std::array first{BufferBarrier{*out, ResourceState::Undefined, ResourceState::ShaderWrite}};
            commands.Barriers(first, {});
            commands.BindPipeline(*pipeline);
            commands.PushConstants(Push{device->BindlessIndex(*in), device->BindlessIndex(*out)});
            commands.Dispatch(1, 1, 1);
        });
    Vec4 result{};
    std::memcpy(&result, bytes.data(), sizeof(Vec4));
    EXPECT_NEAR(result.x, expected.x, 1e-5f);
    EXPECT_NEAR(result.y, expected.y, 1e-5f);
    EXPECT_NEAR(result.z, expected.z, 1e-5f);
    EXPECT_NEAR(result.w, expected.w, 1e-5f);

    device->DestroyPipeline(*pipeline);
    device->DestroyBuffer(*in);
    device->DestroyBuffer(*out);
}
