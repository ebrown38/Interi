// SPDX-License-Identifier: MIT
// Draws vertices given directly in clip space (float4 each) from a bindless buffer.
#include <interi/Bindless.hlsli>

struct Push
{
    uint vertices;
    uint pad0;
    uint pad1;
    uint pad2;
    float4 color;
};
[[vk::push_constant]] Push g_Push;

float4 main(uint vertexId : SV_VertexID) : SV_Position
{
    return g_Buffers[g_Push.vertices].Load<float4>(vertexId * 16);
}
