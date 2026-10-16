// SPDX-License-Identifier: MIT
// GPU particle compute: mode 0 copies the particles emitted this frame into the ring, mode 1 integrates every live
// particle (the same operations, in the same order, as render::IntegrateParticles).
#include "ParticleCommon.hlsli"

struct Push
{
    uint frame; ///< Bindless index of this frame's upload slot.
    uint pool;  ///< Bindless index of the particle ring.
    uint mode;
};
[[vk::push_constant]] Push g_Push;

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint capacity = g_Buffers[g_Push.frame].Load(kHeaderCapacity);
    if (g_Push.mode == 0)
    {
        if (id.x >= g_Buffers[g_Push.frame].Load(kHeaderEmitCount))
        {
            return;
        }
        const uint slot = (g_Buffers[g_Push.frame].Load(kHeaderEmitStart) + id.x) % capacity;
        const uint source = kHeaderSize + id.x * kParticleStride;
        const uint destination = slot * kParticleStride;
        for (uint i = 0; i < kParticleStride; i += 16)
        {
            g_Buffers[g_Push.pool].Store<uint4>(destination + i, g_Buffers[g_Push.frame].Load<uint4>(source + i));
        }
        return;
    }

    if (id.x >= capacity)
    {
        return;
    }
    const uint offset = id.x * kParticleStride;
    float4 positionLife = g_Buffers[g_Push.pool].Load<float4>(offset);
    if (positionLife.w <= 0.0)
    {
        return;
    }
    float3 velocity = g_Buffers[g_Push.pool].Load<float3>(offset + 16);
    const float drag = asfloat(g_Buffers[g_Push.pool].Load(offset + 40));
    const float gravityScale = asfloat(g_Buffers[g_Push.pool].Load(offset + 44));
    const float4 gravityDt = g_Buffers[g_Push.frame].Load<float4>(kHeaderGravity);
    const float dt = gravityDt.w;
    velocity += gravityDt.xyz * gravityScale * dt;
    velocity *= max(1.0 - drag * dt, 0.0);
    positionLife.xyz += velocity * dt;
    positionLife.w -= dt;
    g_Buffers[g_Push.pool].Store<float4>(offset, positionLife);
    g_Buffers[g_Push.pool].Store<float3>(offset + 16, velocity);
}
