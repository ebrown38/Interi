// SPDX-License-Identifier: MIT
// Shared by the forward pass shaders. Layouts must match engine/render/src/ForwardRenderer.cpp.
#ifndef INTERI_FORWARD_COMMON_HLSLI
#define INTERI_FORWARD_COMMON_HLSLI

#include <interi/Bindless.hlsli>

struct ForwardPush
{
    uint vertices;    ///< Bindless index of the mesh's vertex buffer (24-byte Vertex).
    uint scene;       ///< Bindless index of this frame's scene buffer.
    uint objectIndex; ///< Index into the scene buffer's object array.
    uint pad;
};
[[vk::push_constant]] ForwardPush g_Push;

// Scene buffer: FrameData (128 bytes), then ObjectData[] (160 bytes each).
static const uint kFrameViewProjection = 0;
static const uint kFrameCameraPosition = 64;
static const uint kFrameSunDirection = 80; ///< float3 direction the light travels, then float intensity.
static const uint kFrameSunColor = 96;
static const uint kFrameAmbient = 112;
static const uint kObjectsOffset = 128;
static const uint kObjectStride = 160;
static const uint kObjectModel = 0;
static const uint kObjectNormalMatrix = 64;
static const uint kObjectBaseColorMetallic = 128;
static const uint kObjectRoughness = 144;

uint ObjectOffset()
{
    return kObjectsOffset + g_Push.objectIndex * kObjectStride;
}

struct ForwardVaryings
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL0;
};

#endif
