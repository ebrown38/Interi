// SPDX-License-Identifier: MIT
// Shadow map depth pass: transforms vertices into one cascade's shadow clip space.
#include "ForwardCommon.hlsli"

struct Push
{
    uint vertices;
    uint scene;
    uint objectIndex;
    uint cascade;
};
[[vk::push_constant]] Push g_Push;

float4 main(uint vertexId : SV_VertexID) : SV_Position
{
    const float3 position = g_Buffers[g_Push.vertices].Load<float3>(vertexId * 24);
    const float4 world =
        mul(LoadMat4(g_Push.scene, ObjectOffset(g_Push.objectIndex) + kObjectModel), float4(position, 1.0));
    return mul(LoadMat4(g_Push.scene, kFrameCascades + g_Push.cascade * 64), world);
}
