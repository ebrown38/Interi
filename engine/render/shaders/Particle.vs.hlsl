// SPDX-License-Identifier: MIT
// Camera-facing particle quads: six vertices per ring slot. Free slots and particles of the other blend mode get a
// position outside the clip volume.
#include "ParticlePush.hlsli"

static const float2 kCorners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1),
                                   float2(-1, 1)};

ParticleVaryings main(uint vertexId : SV_VertexID)
{
    ParticleVaryings output;
    const Particle p = LoadParticle(g_Push.pool, (vertexId / 6) * kParticleStride);
    const bool additive = (p.flags & kParticleAdditive) != 0;
    if (p.life <= 0.0 || additive != (g_Push.additive != 0))
    {
        output.position = float4(2.0, 2.0, 2.0, 1.0);
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        output.uv = float2(0.0, 0.0);
        output.viewDistance = 0.0;
        output.size = 0.0;
        return output;
    }
    const float4x4 view = LoadMat4(g_Push.frame, kHeaderView);
    const float2 corner = kCorners[vertexId % 6];
    // Rows of the view rotation are the camera's right and up axes in world space.
    const float3 world = p.position + (view[0].xyz * corner.x + view[1].xyz * corner.y) * (p.size * 0.5);
    output.position = mul(LoadMat4(g_Push.frame, kHeaderViewProjection), float4(world, 1.0));
    const float4 color = float4(p.color & 0xFF, (p.color >> 8) & 0xFF, (p.color >> 16) & 0xFF, p.color >> 24) / 255.0;
    output.color = float4(pow(color.rgb, 2.2), color.a * saturate(p.life / p.lifetime));
    output.uv = corner;
    output.viewDistance = -mul(view, float4(p.position, 1.0)).z;
    output.size = p.size;
    return output;
}
