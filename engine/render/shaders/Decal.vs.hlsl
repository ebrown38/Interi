// SPDX-License-Identifier: MIT
// Deferred decal boxes: 36 vertices (12 triangles, counter-clockwise from outside) per decal.
#include "DecalCommon.hlsli"

static const float3 kCube[8] = {float3(-0.5, -0.5, -0.5), float3(0.5, -0.5, -0.5), float3(-0.5, 0.5, -0.5),
                                float3(0.5, 0.5, -0.5),   float3(-0.5, -0.5, 0.5), float3(0.5, -0.5, 0.5),
                                float3(-0.5, 0.5, 0.5),   float3(0.5, 0.5, 0.5)};
static const uint kIndices[36] = {0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4,
                                  2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5};

DecalVaryings main(uint vertexId : SV_VertexID)
{
    DecalVaryings output;
    output.decal = vertexId / 36;
    const float3 local = kCube[kIndices[vertexId % 36]];
    const uint frame = g_Push.frame;
    const float4 world = mul(LoadMat4(frame, DecalOffset(output.decal) + kDecalToWorld), float4(local, 1.0));
    output.position = mul(LoadMat4(frame, kHeaderViewProjection), world);
    return output;
}
