// SPDX-License-Identifier: MIT
// Golden image of a fracture face with the triplanar concrete interior material, rendered on lavapipe.
#include <interi/destruction/DestructibleAsset.h>
#include <interi/destruction/FractureMesh.h>
#include <interi/destruction_render/ChunkGatherer.h>
#include <interi/destruction_render/InteriorMaterials.h>
#include <interi/render/Camera.h>
#include <interi/render/Mesh.h>
#include <interi/rhi/ResourceManager.h>

#include "../render/gpu/GoldenImage.h"
#include "../render/gpu/RenderScene.h"
#include "VulkanTestDevice.h"
#include <gtest/gtest.h>

#include <vector>

using namespace interi;
using namespace interi::destruction;
using interi::testing::Image;
using interi::testing::SceneContext;
using interi::testing::VulkanTest;

TEST_F(VulkanTest, InteriorFace_TriplanarConcrete_MatchesGolden)
{
    // A 1.2 m concrete block cut by a slanted plane: the cap is a fracture face, drawn with the procedural concrete
    // (aggregate in cement) mapped triplanar; the outer faces keep a plain painted material.
    const FractureMesh block = MakeBoxMesh({-0.6f, 0.0f, -0.6f}, {0.6f, 1.2f, 0.6f});
    const FractureMesh cut = CutMesh(block, CutPlane::FromPointNormal({0.0, 0.7, 0.0}, {0.299, 0.854, 0.427}), 1e-6);
    const std::vector<DestructionMaterial> destructionMaterials{{.name = "concrete", .interiorMaterialId = 0}};
    const RenderMesh render = BuildRenderMesh(cut, destructionMaterials);
    ASSERT_EQ(render.submeshes.size(), 2u);
    ASSERT_TRUE(render.submeshes[1].interior);

    render::SceneView view;
    view.cameraPosition = {1.6f, 2.2f, 1.9f};
    view.view = render::LookAt(view.cameraPosition, {0.0f, 0.6f, 0.0f}, {0, 1, 0});
    view.sun = {.direction = {-0.3f, -1.0f, -0.4f}, .color = {1.0f, 0.97f, 0.9f}, .intensity = 3.0f};
    view.ambient = {0.1f, 0.1f, 0.11f};

    InteriorMaterials interior;
    const Image image = interi::testing::RenderSceneWith(
        *device, 128, view,
        [&](SceneContext& context, std::vector<render::RenderObject>& objects)
        {
            auto created = CreateInteriorMaterials(context.device, context.uploads, context.commands, 128);
            ASSERT_TRUE(created.has_value()) << created.error().message;
            interior = std::move(*created);
            ChunkMaterials materials;
            materials.surface.push_back({.baseColor = {0.55f, 0.6f, 0.68f}, .roughness = 0.6f});
            materials.interior = interior.materials;

            render::MeshData mesh;
            for (const RenderVertex& vertex : render.vertices)
            {
                mesh.vertices.push_back({.position = vertex.position, .normal = vertex.normal});
            }
            mesh.indices = render.indices;
            auto range = context.meshes.Add(context.uploads, context.commands, mesh);
            ASSERT_TRUE(range.has_value());
            for (const Submesh& submesh : render.submeshes)
            {
                render::MeshRange part = *range;
                part.firstIndex += submesh.indexOffset;
                part.indexCount = submesh.indexCount;
                objects.push_back(
                    {.mesh = part,
                     .material = materials.Get(
                         {.mesh = part, .surfaceMaterial = submesh.surfaceMaterial, .interior = submesh.interior})});
            }
            auto ground = context.meshes.Add(context.uploads, context.commands, render::MakePlane(3.0f));
            ASSERT_TRUE(ground.has_value());
            objects.push_back({.mesh = *ground, .material = {.baseColor = {0.4f, 0.42f, 0.38f}}, .castsShadow = false});
            DestroyInteriorMaterials(context.resources, interior); // released once the frame completes
        });
    interi::testing::ExpectMatchesGolden(image, "interior_triplanar_concrete");
}
