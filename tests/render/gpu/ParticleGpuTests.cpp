// SPDX-License-Identifier: MIT
// GPU particles on a real device: the compute simulation against the CPU reference, and drawing.
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/Particles.h>
#include <interi/rhi/ResourceManager.h>

#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::VulkanTest;

namespace
{

std::vector<Particle> Burst(std::uint32_t count)
{
    std::vector<Particle> particles(count);
    for (std::uint32_t i = 0; i < count; ++i)
    {
        const auto f = static_cast<float>(i);
        particles[i].position = {f * 0.1f, 2.0f + f * 0.01f, -f * 0.05f};
        particles[i].velocity = {1.0f - f * 0.02f, 3.0f + f * 0.03f, f * 0.01f};
        particles[i].life = 0.3f + f * 0.01f; // some expire during the run
        particles[i].lifetime = 2.0f;
        particles[i].drag = f * 0.05f;
        particles[i].gravityScale = i % 3 == 0 ? -0.2f : 1.0f;
        particles[i].size = 0.2f;
        particles[i].color = PackColor(200, 200, 200, 255);
    }
    return particles;
}

} // namespace

TEST_F(VulkanTest, ParticleSim_Gravity_MatchesCPUReference)
{
    constexpr std::uint32_t kCapacity = 128;
    constexpr int kSteps = 20;
    constexpr float kDt = 1.0f / 60.0f;
    rhi::ResourceManager resources(*device);
    auto system = GpuParticleSystem::Create(*device, resources, rhi::Format::R8G8B8A8Srgb,
                                            {.capacity = kCapacity, .maxEmitPerFrame = 64, .framesInFlight = 1});
    ASSERT_TRUE(system.has_value()) << system.error().message;
    auto readback = device->CreateBuffer({.size = sizeof(Particle) * kCapacity,
                                          .usage = rhi::BufferUsage::CopyDst,
                                          .memory = rhi::MemoryUsage::Readback});
    auto pool = device->CreateCommandPool();
    ASSERT_TRUE(readback && pool);

    // 40 particles at the start of the ring, then 30 more emitted on step 10 (CPU mirrors the ring).
    std::vector<Particle> reference(kCapacity);
    const std::vector<Particle> first = Burst(40);
    const std::vector<Particle> second = Burst(30);
    for (int step = 0; step < kSteps; ++step)
    {
        if (step == 0)
        {
            EXPECT_EQ((*system)->Emit(first), 40u);
            std::ranges::copy(first, reference.begin());
        }
        if (step == 10)
        {
            EXPECT_EQ((*system)->Emit(second), 30u);
            std::ranges::copy(second, reference.begin() + 40);
        }
        IntegrateParticles(reference, {0.0f, -9.81f, 0.0f}, kDt);

        rhi::ICommandList& commands = device->BeginCommandList(*pool);
        FrameGraph graph(*device, resources);
        FGTexture noColor;
        (*system)->AddPasses(graph, kDt, ParticleView{}, noColor, {}, 0, 0);
        ASSERT_TRUE(graph.Compile().has_value());
        ASSERT_TRUE(graph.Execute(commands).has_value());
        if (step == kSteps - 1)
        {
            const std::array toCopy{
                rhi::BufferBarrier{(*system)->Pool(), rhi::ResourceState::ShaderRead, rhi::ResourceState::CopySrc},
                rhi::BufferBarrier{*readback, rhi::ResourceState::Undefined, rhi::ResourceState::CopyDst}};
            commands.Barriers(toCopy, {});
            commands.CopyBuffer((*system)->Pool(), 0, *readback, 0, sizeof(Particle) * kCapacity);
            const std::array toHost{
                rhi::BufferBarrier{*readback, rhi::ResourceState::CopyDst, rhi::ResourceState::HostRead},
                rhi::BufferBarrier{(*system)->Pool(), rhi::ResourceState::CopySrc, rhi::ResourceState::ShaderRead}};
            commands.Barriers(toHost, {});
        }
        rhi::ICommandList* const lists[] = {&commands};
        device->WaitForValue(device->Submit(lists));
        device->ResetCommandPool(*pool);
    }

    std::vector<Particle> gpu(kCapacity);
    std::memcpy(gpu.data(), device->MappedMemory(*readback).data(), sizeof(Particle) * kCapacity);
    std::uint32_t alive = 0;
    for (std::uint32_t i = 0; i < kCapacity; ++i)
    {
        // GPU compilers may fuse multiply-adds, so allow float rounding differences.
        EXPECT_NEAR(gpu[i].position.x, reference[i].position.x, 1e-4f) << i;
        EXPECT_NEAR(gpu[i].position.y, reference[i].position.y, 1e-4f) << i;
        EXPECT_NEAR(gpu[i].position.z, reference[i].position.z, 1e-4f) << i;
        EXPECT_NEAR(gpu[i].velocity.y, reference[i].velocity.y, 1e-4f) << i;
        EXPECT_NEAR(gpu[i].life, reference[i].life, 1e-5f) << i;
        EXPECT_EQ(gpu[i].life > 0.0f, reference[i].life > 0.0f) << i;
        alive += reference[i].life > 0.0f ? 1u : 0u;
    }
    EXPECT_GT(alive, 20u);
    EXPECT_LT(alive, 70u); // some expired

    device->DestroyCommandPool(*pool);
    device->DestroyBuffer(*readback);
}

TEST_F(VulkanTest, ParticleSystem_EmitBeyondFrameLimit_Truncated)
{
    rhi::ResourceManager resources(*device);
    auto system = GpuParticleSystem::Create(*device, resources, rhi::Format::R8G8B8A8Srgb,
                                            {.capacity = 64, .maxEmitPerFrame = 16, .framesInFlight = 1});
    ASSERT_TRUE(system.has_value());
    const std::vector<Particle> many(20);
    EXPECT_EQ((*system)->Emit(many), 16u);
    EXPECT_EQ((*system)->Emit(many), 0u);
}
