// SPDX-License-Identifier: MIT
// Clustered light assignment: one thread per cluster lists the point lights whose sphere touches the cluster's
// view-space box. Mirrors interi::render::AssignLights (the CPU reference used by the tests).
#include "ForwardCommon.hlsli"

struct Push
{
    uint scene;
    uint clusters;
};
[[vk::push_constant]] Push g_Push;

[numthreads(64, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    const uint cluster = dispatchId.x;
    if (cluster >= kClusterCount)
    {
        return;
    }
    const uint x = cluster % kTilesX;
    const uint y = (cluster / kTilesX) % kTilesY;
    const uint z = cluster / (kTilesX * kTilesY);
    const uint scene = g_Push.scene;

    // Tile rectangle in NDC (+Y up; tile row 0 is the top of the screen), swept between the slice's depths.
    const float ndcX0 = -1.0 + 2.0 * float(x) / float(kTilesX);
    const float ndcX1 = -1.0 + 2.0 * float(x + 1) / float(kTilesX);
    const float ndcY1 = 1.0 - 2.0 * float(y) / float(kTilesY);
    const float ndcY0 = 1.0 - 2.0 * float(y + 1) / float(kTilesY);
    const float tanHalf = LoadFloat(scene, kFrameTanHalfFovY);
    const float aspect = LoadFloat(scene, kFrameAspect);
    const float depths[2] = {SliceStart(scene, z), SliceStart(scene, z + 1)};
    float3 boxMin = float3(1e30, 1e30, -depths[1]);
    float3 boxMax = float3(-1e30, -1e30, -depths[0]);
    for (uint i = 0; i < 2; ++i)
    {
        const float sx = depths[i] * tanHalf * aspect;
        const float sy = depths[i] * tanHalf;
        boxMin.x = min(boxMin.x, min(ndcX0 * sx, ndcX1 * sx));
        boxMax.x = max(boxMax.x, max(ndcX0 * sx, ndcX1 * sx));
        boxMin.y = min(boxMin.y, min(ndcY0 * sy, ndcY1 * sy));
        boxMax.y = max(boxMax.y, max(ndcY0 * sy, ndcY1 * sy));
    }

    const float4x4 view = LoadMat4(scene, kFrameView);
    const uint lightCount = LoadUint(scene, kFrameLightCount);
    const uint lightsOffset = LoadUint(scene, kFrameLightsOffset);
    const uint base = cluster * kClusterStride;
    uint count = 0;
    for (uint light = 0; light < lightCount && count < kMaxLightsPerCluster; ++light)
    {
        const float4 positionRadius = g_Buffers[scene].Load<float4>(lightsOffset + light * kLightStride);
        const float3 centre = mul(view, float4(positionRadius.xyz, 1.0)).xyz;
        const float3 delta = centre - clamp(centre, boxMin, boxMax);
        if (dot(delta, delta) - positionRadius.w * positionRadius.w <= 0.0)
        {
            g_Buffers[g_Push.clusters].Store(base + 4 + count * 4, light);
            ++count;
        }
    }
    g_Buffers[g_Push.clusters].Store(base, count);
}
