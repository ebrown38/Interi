// SPDX-License-Identifier: MIT
// The GPU light-culling pass against the CPU reference (interi::render::AssignLights).
#include <interi/core/math/Mat4.h>
#include <interi/render/Camera.h>
#include <interi/render/Clusters.h>
#include <interi/testing/DeterministicRng.h>

#include "RenderScene.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace interi;
using namespace interi::render;
using interi::testing::VulkanTest;

TEST_F(VulkanTest, LightCulling_ClustersMatchCpuReference)
{
    interi::testing::DeterministicRng rng(77);
    std::vector<PointLight> lights(300);
    for (PointLight& light : lights)
    {
        light.position = {rng.NextFloat(-15.0f, 15.0f), rng.NextFloat(-3.0f, 6.0f), rng.NextFloat(-30.0f, 5.0f)};
        light.radius = rng.NextFloat(0.5f, 4.0f);
    }
    SceneView view;
    view.cameraPosition = {0, 2, 8};
    view.view = LookAt(view.cameraPosition, {0, 0, -10}, {0, 1, 0});
    view.pointLights = lights;
    const ForwardSettings settings{.cascadeCount = 0, .clusterFar = 60.0f};

    constexpr std::uint64_t kClusterBytes = std::uint64_t{ClusterGrid::kClusterCount} * ClusterGrid::kClusterStride;
    auto readback = device->CreateBuffer(
        {.size = kClusterBytes, .usage = rhi::BufferUsage::CopyDst, .memory = rhi::MemoryUsage::Readback});
    ASSERT_TRUE(readback.has_value());
    const MeshData plane = MakePlane(1.0f);
    (void)interi::testing::RenderScene(
        *device, 64, view, {plane}, {{.mesh = 0, .object = {}}}, settings,
        [&](FrameGraph& graph, const ForwardOutputs& outputs)
        {
            const FGBuffer out = graph.ImportBuffer("cluster readback", *readback, rhi::ResourceState::Undefined,
                                                    rhi::ResourceState::HostRead);
            graph.AddPass(
                "ReadClusters",
                [&](PassBuilder& builder)
                {
                    builder.Read(outputs.clusters, rhi::ResourceState::CopySrc);
                    builder.Write(out, rhi::ResourceState::CopyDst);
                },
                [clusters = outputs.clusters, out](PassContext& context)
                { context.Commands().CopyBuffer(context.Buffer(clusters), 0, context.Buffer(out), 0, kClusterBytes); });
        });

    const ClusterGrid grid{.nearPlane = view.nearPlane,
                           .farPlane = settings.clusterFar,
                           .tanHalfFovY = std::tan(view.verticalFov * 0.5f),
                           .aspect = 1.0f};
    std::vector<Vec3> viewPositions;
    std::vector<float> radii;
    for (const PointLight& light : lights)
    {
        const Vec4 p = view.view * Vec4(light.position, 1.0f);
        viewPositions.push_back({p.x, p.y, p.z});
        radii.push_back(light.radius);
    }
    const auto expected = AssignLights(grid, viewPositions, radii);

    std::vector<std::uint32_t> gpu(kClusterBytes / 4);
    std::memcpy(gpu.data(), device->MappedMemory(*readback).data(), kClusterBytes);
    std::size_t nonEmpty = 0;
    std::size_t boundaryDisagreements = 0;
    for (std::uint32_t z = 0; z < ClusterGrid::kSlices; ++z)
    {
        for (std::uint32_t y = 0; y < ClusterGrid::kTilesY; ++y)
        {
            for (std::uint32_t x = 0; x < ClusterGrid::kTilesX; ++x)
            {
                const std::uint32_t cluster = ClusterGrid::Index(x, y, z);
                const std::uint32_t* entry = &gpu[cluster * (ClusterGrid::kClusterStride / 4)];
                const std::vector<std::uint32_t> actual(entry + 1, entry + 1 + entry[0]);
                const std::vector<std::uint32_t>& reference = expected[cluster];
                nonEmpty += reference.empty() ? 0 : 1;
                if (actual == reference)
                {
                    continue;
                }
                // CPU and GPU rounding may only disagree about lights that touch the cluster's box within float
                // precision, and only while neither list is truncated.
                Vec3 min;
                Vec3 max;
                grid.Bounds(x, y, z, min, max);
                std::vector<std::uint32_t> difference;
                std::ranges::set_symmetric_difference(actual, reference, std::back_inserter(difference));
                for (const std::uint32_t light : difference)
                {
                    const float margin = SphereBoxMargin(viewPositions[light], radii[light], min, max);
                    EXPECT_LT(std::abs(margin), 1e-3f * radii[light] * radii[light])
                        << "cluster (" << x << ", " << y << ", " << z << ") light " << light;
                    ++boundaryDisagreements;
                }
            }
        }
    }
    EXPECT_GT(nonEmpty, 100u); // the scene really exercises the grid
    EXPECT_LT(boundaryDisagreements, 10u);
    device->DestroyBuffer(*readback);
}
