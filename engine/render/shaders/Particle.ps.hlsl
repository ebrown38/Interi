// SPDX-License-Identifier: MIT
// Round, soft particles: hidden behind the scene depth and faded where they approach a surface, so quads do not
// show hard lines where they cut into geometry. Output is linear with straight alpha.
#include "ParticlePush.hlsli"

float4 main(ParticleVaryings input) : SV_Target0
{
    const float sceneDepth = g_Textures[g_Push.depth].Load(int3(input.position.xy, 0)).r;
    // Reverse-Z with an infinite far plane: depth = near / distance (0 = nothing drawn there).
    const float nearPlane = asfloat(g_Buffers[g_Push.frame].Load(kHeaderNearPlane));
    const float sceneDistance = sceneDepth > 0.0 ? nearPlane / sceneDepth : 1.0e30;
    const float gap = sceneDistance - input.viewDistance;
    if (gap <= 0.0)
    {
        discard;
    }
    const float radial = saturate(1.0 - dot(input.uv, input.uv));
    const float soft = saturate(gap / max(input.size, 1e-3));
    return float4(input.color.rgb, input.color.a * radial * soft);
}
