// SPDX-License-Identifier: MIT
// Pulls ImDrawVerts from a raw buffer. SV_VertexID includes the draw's vertex offset.
#include "ImGuiCommon.hlsli"

ImGuiVaryings main(uint vertexId : SV_VertexID)
{
    const uint offset = vertexId * kVertexStride;
    const float2 position = g_Buffers[g_Push.vertices].Load<float2>(offset);
    const float2 uv = g_Buffers[g_Push.vertices].Load<float2>(offset + 8);
    const uint packed = g_Buffers[g_Push.vertices].Load(offset + 16);
    float4 color = float4(packed & 0xFF, (packed >> 8) & 0xFF, (packed >> 16) & 0xFF, packed >> 24) / 255.0;
    if (g_Push.linearizeColors != 0)
    {
        color.rgb = pow(color.rgb, 2.2);
    }
    ImGuiVaryings output;
    // ImGui's y grows downwards; clip space is +Y up.
    const float2 clip = position * g_Push.scale + g_Push.translate;
    output.position = float4(clip.x, -clip.y, 0.0, 1.0);
    output.color = color;
    output.uv = uv;
    return output;
}
