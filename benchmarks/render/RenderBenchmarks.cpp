// SPDX-License-Identifier: MIT
// CPU cost of the per-frame frame graph: declaring, compiling (culling, lifetimes, memory placement) and recording
// a frame with no GPU work (NullDevice).
#include <interi/render/FrameGraph.h>
#include <interi/rhi/ResourceManager.h>

#include "../../tests/rhi/NullDevice.h"
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <vector>

namespace
{

using namespace interi;
using namespace interi::render;

/// A chain of `passes` passes, each reading the previous pass's output and writing a new transient texture, as a
/// stand-in for a frame's post-processing and lighting passes.
void DeclareChain(FrameGraph& graph, std::int64_t passes, rhi::TextureHandle backBuffer)
{
    const auto output =
        graph.ImportTexture("backbuffer", backBuffer, rhi::ResourceState::Undefined, rhi::ResourceState::Present);
    const rhi::TextureDesc desc{.width = 1920,
                                .height = 1080,
                                .format = rhi::Format::R16G16B16A16Float,
                                .usage = rhi::TextureUsage::ColorTarget | rhi::TextureUsage::Sampled};
    FGTexture previous;
    for (std::int64_t i = 0; i < passes; ++i)
    {
        FGTexture target = graph.CreateTexture("target", desc);
        graph.AddPass(
            "pass",
            [&](PassBuilder& builder)
            {
                if (previous.IsValid())
                {
                    builder.Read(previous, rhi::ResourceState::ShaderRead);
                }
                target = builder.Write(target, rhi::ResourceState::ColorTarget);
            },
            [](PassContext&) {});
        previous = target;
    }
    graph.AddPass(
        "present",
        [&](PassBuilder& builder)
        {
            builder.Read(previous, rhi::ResourceState::ShaderRead);
            builder.Write(output, rhi::ResourceState::ColorTarget);
        },
        [](PassContext&) {});
}

void BM_FrameGraph_DeclareCompileExecute(benchmark::State& state)
{
    testing::NullDevice device;
    rhi::ResourceManager resources(device);
    FrameGraph graph(device, resources);
    testing::NullCommandList commands;
    const auto backBuffer = rhi::TextureHandle::FromParts(1, 1);
    for (auto _ : state)
    {
        graph.Reset();
        DeclareChain(graph, state.range(0), backBuffer);
        benchmark::DoNotOptimize(graph.Compile());
        benchmark::DoNotOptimize(graph.Execute(commands));
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_FrameGraph_DeclareCompileExecute)->Arg(16)->Arg(64)->Arg(256);

} // namespace
