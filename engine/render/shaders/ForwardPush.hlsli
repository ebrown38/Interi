// SPDX-License-Identifier: MIT
// Push constants and varyings of the forward pass.
#ifndef INTERI_FORWARD_PUSH_HLSLI
#define INTERI_FORWARD_PUSH_HLSLI

#include "ForwardCommon.hlsli"

struct ForwardPush
{
    uint vertices;      ///< Bindless index of the mesh's vertex buffer (24-byte Vertex).
    uint scene;         ///< Bindless index of this frame's scene buffer.
    uint objectIndex;   ///< Index into the scene buffer's object array.
    uint clusters;      ///< Bindless index of the light-culling result.
    uint shadowMap;     ///< Bindless index of the cascade texture array, or kInvalidIndex.
    uint shadowSampler; ///< Bindless index of the depth-comparison sampler.
    uint pad0;
    uint pad1;
};
[[vk::push_constant]] ForwardPush g_Push;

struct ForwardVaryings
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL0;
};

#endif
