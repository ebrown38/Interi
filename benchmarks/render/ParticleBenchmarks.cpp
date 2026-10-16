// SPDX-License-Identifier: MIT
// GPU particle capacity (M8.4's deferred GPU particle count, measured in M9): wall time of one frame of particle
// simulation and drawing (1280 x 720, every particle alive and on screen), submitted and waited for, on whatever
// Vulkan device the machine has. Report-only: the result depends on the GPU (lavapipe on CI is a CPU rasteriser).
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/FrameGraph.h>
#include <interi/render/Particles.h>
#include <interi/rhi/ResourceManager.h>
#include <interi/rhi_vulkan/VulkanDevice.h>

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
/// The benchmark runs on the machine's real Vulkan driver (not lavapipe, which the GPU tests pin and suppress), whose
/// process-lifetime allocations LeakSanitizer reports from an already unloaded module that no suppression can match.
extern "C" const char* __lsan_default_options() // NOLINT(bugprone-reserved-identifier)
{
    return "detect_leaks=0";
}
#endif
#endif

namespace
{

using namespace interi;
using namespace interi::render;

constexpr std::uint32_t kWidth = 1280;
constexpr std::uint32_t kHeight = 720;

/// Arg: particle count. Dust-sized (0.2 m) alpha particles in a 20 m cube in front of the camera, a quarter of them
/// additive sparks.
void BM_GpuParticlesFrame(benchmark::State& state)
{
    auto device = rhi::VulkanDevice::Create({});
    if (!device)
    {
        state.SkipWithError("no Vulkan device");
        return;
    }
    const auto count = static_cast<std::uint32_t>(state.range(0));
    {
        rhi::ResourceManager resources(**device);
        auto system = GpuParticleSystem::Create(**device, resources, rhi::Format::R8G8B8A8Srgb,
                                                {.capacity = count, .maxEmitPerFrame = count, .framesInFlight = 1});
        auto color = (*device)->CreateTexture({.width = kWidth,
                                               .height = kHeight,
                                               .format = rhi::Format::R8G8B8A8Srgb,
                                               .usage = rhi::TextureUsage::ColorTarget});
        auto depth = (*device)->CreateTexture({.width = kWidth,
                                               .height = kHeight,
                                               .format = rhi::Format::D32Float,
                                               .usage = rhi::TextureUsage::DepthTarget | rhi::TextureUsage::Sampled});
        auto pool = (*device)->CreateCommandPool();
        if (!system || !color || !depth || !pool)
        {
            state.SkipWithError("could not create the particle system");
            return;
        }
        std::vector<Particle> particles(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const auto f = static_cast<float>(i);
            particles[i].position = {std::fmod(f * 0.37f, 20.0f) - 10.0f, std::fmod(f * 0.11f, 20.0f) - 10.0f,
                                     -15.0f - std::fmod(f * 0.73f, 20.0f)};
            particles[i].life = 1.0e6f; // stay alive for the whole run
            particles[i].lifetime = 1.0e6f;
            particles[i].size = 0.2f;
            particles[i].drag = 0.5f;
            particles[i].gravityScale = 0.0f;
            particles[i].color = PackColor(150, 145, 135, 120);
            particles[i].flags = i % 4 == 0 ? kParticleAdditive : 0u;
        }
        (void)(*system)->Emit(particles);
        const Mat4 view = LookAt({0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0, 1, 0});
        const ParticleView particleView{.viewProjection = PerspectiveReverseZ(1.0471976f, 16.0f / 9.0f, 0.1f) * view,
                                        .view = view,
                                        .nearPlane = 0.1f};

        FrameGraph graph(**device, resources);
        bool first = true;
        const auto frame = [&]
        {
            graph.Reset();
            FGTexture target = graph.ImportTexture(
                "color", *color, first ? rhi::ResourceState::Undefined : rhi::ResourceState::ColorTarget,
                rhi::ResourceState::ColorTarget);
            const FGTexture sceneDepth = graph.ImportTexture(
                "depth", *depth, first ? rhi::ResourceState::Undefined : rhi::ResourceState::ShaderRead,
                rhi::ResourceState::ShaderRead);
            first = false;
            (*system)->AddPasses(graph, 1.0f / 60.0f, particleView, target, sceneDepth, kWidth, kHeight);
            rhi::ICommandList& commands = (*device)->BeginCommandList(*pool);
            if (!graph.Compile() || !graph.Execute(commands))
            {
                state.SkipWithError("frame graph failed");
            }
            rhi::ICommandList* const lists[] = {&commands};
            (*device)->WaitForValue((*device)->Submit(lists));
            (*device)->ResetCommandPool(*pool);
        };
        frame(); // emits the particles
        for (auto _ : state)
        {
            frame();
        }
        state.SetItemsProcessed(state.iterations() * count);
        (*device)->DestroyCommandPool(*pool);
        (*device)->DestroyTexture(*color);
        (*device)->DestroyTexture(*depth);
    }
}
BENCHMARK(BM_GpuParticlesFrame)->Arg(16384)->Arg(65536)->Arg(262144)->Unit(benchmark::kMillisecond)->UseRealTime();

} // namespace
