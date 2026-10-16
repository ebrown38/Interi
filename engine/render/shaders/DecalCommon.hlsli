// SPDX-License-Identifier: MIT
// Deferred decals: layouts must match engine/render/src/Decals.cpp.
#ifndef INTERI_DECAL_COMMON_HLSLI
#define INTERI_DECAL_COMMON_HLSLI

#include <interi/Bindless.hlsli>

static const uint kHeaderViewProjection = 0;
static const uint kHeaderInverseViewProjection = 64;
static const uint kHeaderScreenSize = 128;
static const uint kHeaderSize = 192;
static const uint kDecalStride = 160;
static const uint kDecalToWorld = 0;
static const uint kWorldToDecal = 64;
static const uint kDecalColorIntensity = 128;
static const uint kDecalKindSeed = 144;

static const uint kDecalCrack = 0;
static const uint kDecalScorch = 1;

struct DecalPush
{
    uint frame; ///< Bindless index of this frame's decal buffer.
    uint depth; ///< Bindless texture index of the scene depth.
};
[[vk::push_constant]] DecalPush g_Push;

struct DecalVaryings
{
    float4 position : SV_Position;
    nointerpolation uint decal : DECAL;
};

uint DecalOffset(uint decal)
{
    return kHeaderSize + decal * kDecalStride;
}

#endif
