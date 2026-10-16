// SPDX-License-Identifier: MIT
// Deferred decal pixels: reconstruct the surface under the pixel from the scene depth, keep it if it lies inside
// the decal box, and blend the procedural pattern over the colour.
#include "DecalCommon.hlsli"

uint Hash(uint x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

float Lattice(int2 p, uint seed)
{
    return float(Hash(uint(p.x) * 73856093u ^ uint(p.y) * 19349663u ^ seed) & 0xFFFFu) / 65535.0;
}

/// Smooth value noise in 0..1.
float Noise(float2 p, uint seed)
{
    const int2 cell = int2(floor(p));
    const float2 f = p - floor(p);
    const float2 t = f * f * (3.0 - 2.0 * f);
    const float a = Lattice(cell, seed);
    const float b = Lattice(cell + int2(1, 0), seed);
    const float c = Lattice(cell + int2(0, 1), seed);
    const float d = Lattice(cell + int2(1, 1), seed);
    return lerp(lerp(a, b, t.x), lerp(c, d, t.x), t.y);
}

/// Jagged crack where the surface crosses the local z = 0 plane: the line wanders with noise along the surface
/// (parametrised by local x and y), and a thinner branch splits off.
float Crack(float3 local, uint seed)
{
    const float2 along = local.xy * 6.0;
    const float wander = (Noise(along, seed) - 0.5) * 0.25 + (Noise(along * 3.0, seed + 1) - 0.5) * 0.08;
    const float main = 1.0 - smoothstep(0.015, 0.05, abs(local.z + wander));
    const float branchWander = (Noise(along * 1.7, seed + 2) - 0.5) * 0.5;
    const float branch = (1.0 - smoothstep(0.01, 0.03, abs(local.z - 0.12 + branchWander))) *
                         step(0.55, Noise(along * 0.8, seed + 3));
    return max(main, branch * 0.7);
}

/// Soot: densest at the centre, ragged towards the box's surface.
float Scorch(float3 local, uint seed)
{
    const float r = length(local) * 2.0; // 1 at the inscribed sphere
    const float ragged = Noise(local.xy * 8.0 + local.z * 5.0, seed) * 0.35;
    return saturate(1.0 - smoothstep(0.3, 1.0, r + ragged - 0.15));
}

float4 main(DecalVaryings input) : SV_Target0
{
    const uint frame = g_Push.frame;
    const float depth = g_Textures[g_Push.depth].Load(int3(input.position.xy, 0)).r;
    if (depth <= 0.0)
    {
        discard; // nothing drawn there
    }
    const float2 screen = g_Buffers[frame].Load<float2>(kHeaderScreenSize);
    // Pixel -> NDC: +Y is the top row.
    const float2 ndc = float2(input.position.x / screen.x * 2.0 - 1.0, 1.0 - input.position.y / screen.y * 2.0);
    const float4 world = mul(LoadMat4(frame, kHeaderInverseViewProjection), float4(ndc, depth, 1.0));
    const uint offset = DecalOffset(input.decal);
    const float3 local = mul(LoadMat4(frame, offset + kWorldToDecal), float4(world.xyz / world.w, 1.0)).xyz;
    if (any(abs(local) > 0.5))
    {
        discard;
    }
    const float4 colorIntensity = g_Buffers[frame].Load<float4>(offset + kDecalColorIntensity);
    const uint2 kindSeed = g_Buffers[frame].Load<uint2>(offset + kDecalKindSeed);
    const float pattern = kindSeed.x == kDecalCrack ? Crack(local, kindSeed.y) : Scorch(local, kindSeed.y);
    // Fade out towards the box's sides so the pattern never ends in a hard edge.
    const float3 edge = saturate((0.5 - abs(local)) * 10.0);
    const float alpha = pattern * colorIntensity.w * edge.x * edge.y * (kindSeed.x == kDecalCrack ? 1.0 : edge.z);
    if (alpha <= 0.0)
    {
        discard;
    }
    return float4(colorIntensity.rgb, alpha);
}
