// SPDX-License-Identifier: MIT
// GPU particles: layouts must match engine/render/include/interi/render/Particles.h and src/Particles.cpp.
#ifndef INTERI_PARTICLE_COMMON_HLSLI
#define INTERI_PARTICLE_COMMON_HLSLI

#include <interi/Bindless.hlsli>

// Per-frame upload slot: header, then the particles emitted this frame.
static const uint kHeaderViewProjection = 0;
static const uint kHeaderView = 64;
static const uint kHeaderGravity = 128; ///< float3 gravity, float dt
static const uint kHeaderEmitCount = 144;
static const uint kHeaderEmitStart = 148;
static const uint kHeaderCapacity = 152;
static const uint kHeaderNearPlane = 156;
static const uint kHeaderSize = 192;

// Particle (64 bytes): float3 position, float life, float3 velocity, float lifetime, uint color, float size,
// float drag, float gravityScale, uint flags, uint pad[3].
static const uint kParticleStride = 64;
static const uint kParticleAdditive = 1;

struct Particle
{
    float3 position;
    float life;
    float3 velocity;
    float lifetime;
    uint color;
    float size;
    float drag;
    float gravityScale;
    uint flags;
};

Particle LoadParticle(uint buffer, uint offset)
{
    const float4 a = g_Buffers[buffer].Load<float4>(offset);
    const float4 b = g_Buffers[buffer].Load<float4>(offset + 16);
    const uint4 c = g_Buffers[buffer].Load<uint4>(offset + 32);
    const uint flags = g_Buffers[buffer].Load(offset + 48);
    Particle p;
    p.position = a.xyz;
    p.life = a.w;
    p.velocity = b.xyz;
    p.lifetime = b.w;
    p.color = c.x;
    p.size = asfloat(c.y);
    p.drag = asfloat(c.z);
    p.gravityScale = asfloat(c.w);
    p.flags = flags;
    return p;
}

#endif
