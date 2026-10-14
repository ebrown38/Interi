// SPDX-License-Identifier: MIT
// Forward pass vertex shader: pulls the vertex from the bindless vertex buffer and transforms it.
#include "ForwardPush.hlsli"

ForwardVaryings main(uint vertexId : SV_VertexID)
{
    const float3 position = g_Buffers[g_Push.vertices].Load<float3>(vertexId * 24);
    const float3 normal = g_Buffers[g_Push.vertices].Load<float3>(vertexId * 24 + 12);
    const uint object = ObjectOffset(g_Push.objectIndex);
    const float4 world = mul(LoadMat4(g_Push.scene, object + kObjectModel), float4(position, 1.0));

    ForwardVaryings output;
    output.position = mul(LoadMat4(g_Push.scene, kFrameViewProjection), world);
    output.worldPosition = world.xyz;
    output.normal = mul(LoadMat4(g_Push.scene, object + kObjectNormalMatrix), float4(normal, 0.0)).xyz;
    return output;
}
