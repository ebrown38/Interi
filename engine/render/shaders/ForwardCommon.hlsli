// SPDX-License-Identifier: MIT
// Shared by the forward renderer's shaders. Layouts must match engine/render/src/ForwardRenderer.cpp.
#ifndef INTERI_FORWARD_COMMON_HLSLI
#define INTERI_FORWARD_COMMON_HLSLI

#include <interi/Bindless.hlsli>

static const uint kInvalidIndex = 0xFFFFFFFFu;

// Scene buffer: FrameData (528 bytes), ObjectData[] (192 bytes each), then PointLight[] at kFrameLightsOffset.
static const uint kFrameViewProjection = 0;
static const uint kFrameView = 64;
static const uint kFrameCameraPosition = 128;
static const uint kFrameSunDirection = 144; ///< float3 direction the light travels, then float intensity.
static const uint kFrameSunColor = 160;
static const uint kFrameAmbient = 176;
static const uint kFrameCascades = 192; ///< float4x4[4]: world -> shadow clip space per cascade.
static const uint kFrameCascadeSplits = 448; ///< float4: far view distance of each cascade.
static const uint kFrameCascadeTexels = 464; ///< float4: world size of a shadow texel per cascade.
static const uint kFrameLightCount = 480;
static const uint kFrameLightsOffset = 484;
static const uint kFrameScreenWidth = 488;
static const uint kFrameScreenHeight = 492;
static const uint kFrameClusterNear = 496;
static const uint kFrameClusterFar = 500;
static const uint kFrameTanHalfFovY = 504;
static const uint kFrameAspect = 508;
static const uint kFrameCascadeCount = 512;
static const uint kObjectsOffset = 528;
static const uint kObjectStride = 192;
static const uint kObjectModel = 0;
static const uint kObjectNormalMatrix = 64;
static const uint kObjectBaseColorMetallic = 128;
static const uint kObjectRoughness = 144;
static const uint kObjectBounds = 160; ///< float4: bounding sphere centre (mesh space) and radius.
static const uint kObjectDraw = 176;   ///< uint4: firstIndex, indexCount, vertexOffset, flags.
static const uint kObjectCastsShadow = 1;
static const uint kObjectOccluder = 2;
static const uint kLightStride = 32; ///< float3 position, float radius, float3 colour, float intensity

// Cluster grid (see engine/render/include/interi/render/Clusters.h).
static const uint kTilesX = 16;
static const uint kTilesY = 9;
static const uint kSlices = 24;
static const uint kClusterCount = kTilesX * kTilesY * kSlices;
static const uint kMaxLightsPerCluster = 32;
static const uint kClusterStride = (1 + kMaxLightsPerCluster) * 4;

uint ObjectOffset(uint objectIndex)
{
    return kObjectsOffset + objectIndex * kObjectStride;
}

float LoadFloat(uint scene, uint offset)
{
    return g_Buffers[scene].Load<float>(offset);
}

uint LoadUint(uint scene, uint offset)
{
    return g_Buffers[scene].Load(offset);
}

float SliceStart(uint scene, uint z)
{
    const float nearPlane = LoadFloat(scene, kFrameClusterNear);
    const float farPlane = LoadFloat(scene, kFrameClusterFar);
    return nearPlane * pow(farPlane / nearPlane, float(z) / float(kSlices));
}

uint SliceOf(uint scene, float depth)
{
    const float nearPlane = LoadFloat(scene, kFrameClusterNear);
    const float farPlane = LoadFloat(scene, kFrameClusterFar);
    const float slice = log(depth / nearPlane) / log(farPlane / nearPlane) * float(kSlices);
    return uint(clamp(slice, 0.0, float(kSlices - 1)));
}

#endif
