// SPDX-License-Identifier: MIT
// Dear ImGui draw lists: layouts must match engine/editor_ui/src/ImGuiRenderer.cpp.
#ifndef INTERI_IMGUI_COMMON_HLSLI
#define INTERI_IMGUI_COMMON_HLSLI

#include <interi/Bindless.hlsli>

static const uint kVertexStride = 20; ///< ImDrawVert: float2 pos, float2 uv, uint color (RGBA8, sRGB encoded).

struct ImGuiPush
{
    float2 scale;     ///< Pixels to clip space.
    float2 translate;
    uint vertices;    ///< Bindless index of this frame's vertex buffer.
    uint texture;     ///< Bindless texture index.
    uint sampler;     ///< Bindless sampler index.
    uint linearizeColors; ///< 1 when the target is sRGB: vertex colours are converted to linear.
};
[[vk::push_constant]] ImGuiPush g_Push;

struct ImGuiVaryings
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

#endif
