// SPDX-License-Identifier: MIT
// Push constants and varyings of the particle draw.
#ifndef INTERI_PARTICLE_PUSH_HLSLI
#define INTERI_PARTICLE_PUSH_HLSLI

#include "ParticleCommon.hlsli"

struct ParticlePush
{
    uint frame;    ///< Bindless index of this frame's upload slot.
    uint pool;     ///< Bindless index of the particle ring.
    uint depth;    ///< Bindless texture index of the scene depth (reverse-Z).
    uint additive; ///< 1: draw the additive particles, 0: the alpha-blended ones.
};
[[vk::push_constant]] ParticlePush g_Push;

struct ParticleVaryings
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0; ///< -1..1 across the quad.
    float viewDistance : TEXCOORD1;
    nointerpolation float size : TEXCOORD2;
};

#endif
