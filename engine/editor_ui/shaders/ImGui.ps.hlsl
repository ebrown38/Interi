// SPDX-License-Identifier: MIT
// Vertex colour times the texture (the font atlas or an image), straight alpha.
#include "ImGuiCommon.hlsli"

float4 main(ImGuiVaryings input) : SV_Target0
{
    return input.color * g_Textures[g_Push.texture].Sample(g_Samplers[g_Push.sampler], input.uv);
}
