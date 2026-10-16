// SPDX-License-Identifier: MIT
// Forward pass pixel shader: metallic-roughness PBR (GGX / Smith-Schlick / Fresnel-Schlick + Lambert) lit by the
// sun (with cascaded shadows) and by the clustered point lights, plus a constant ambient term. Output is linear;
// an sRGB target encodes it.
#include "ForwardPush.hlsli"

static const float kPi = 3.14159265358979;

float DistributionGgx(float nDotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / (kPi * d * d);
}

float GeometrySchlickGgx(float nDotX, float roughness)
{
    const float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return nDotX / (nDotX * (1.0 - k) + k);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

/// Radiance reflected towards v from light arriving along l (unit vectors) with the given radiance.
float3 Brdf(float3 n, float3 v, float3 l, float3 radiance, float3 baseColor, float metallic, float roughness)
{
    const float3 h = normalize(v + l);
    const float nDotL = saturate(dot(n, l));
    const float nDotV = max(dot(n, v), 1e-4);
    const float nDotH = saturate(dot(n, h));
    const float vDotH = saturate(dot(v, h));
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    const float3 fresnel = FresnelSchlick(vDotH, f0);
    const float d = DistributionGgx(nDotH, roughness * roughness);
    const float g = GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(max(nDotL, 1e-4), roughness);
    const float3 specular = d * g * fresnel / (4.0 * nDotV * max(nDotL, 1e-4));
    const float3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColor / kPi;
    return (diffuse + specular) * radiance * nDotL;
}

/// Fraction of the sun reaching this point: 3x3 percentage-closer filtering in the cascade covering its depth.
float SunVisibility(float3 worldPosition, float3 normal, float viewDepth)
{
    if (g_Push.shadowMap == kInvalidIndex)
    {
        return 1.0;
    }
    const uint scene = g_Push.scene;
    const uint cascadeCount = LoadUint(scene, kFrameCascadeCount);
    const float4 splits = g_Buffers[scene].Load<float4>(kFrameCascadeSplits);
    const float4 texels = g_Buffers[scene].Load<float4>(kFrameCascadeTexels);
    uint cascade = cascadeCount;
    for (uint i = cascadeCount; i > 0; --i)
    {
        if (viewDepth < splits[i - 1])
        {
            cascade = i - 1;
        }
    }
    if (cascade == cascadeCount)
    {
        return 1.0; // beyond the shadow distance
    }
    // Offset along the normal by about a texel to keep surfaces from shadowing themselves.
    const float3 position = worldPosition + normal * texels[cascade] * 1.5;
    const float4 clip = mul(LoadMat4(scene, kFrameCascades + cascade * 64), float4(position, 1.0));
    const float2 uv = float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
    uint width;
    uint height;
    uint layers;
    g_TextureArrays[g_Push.shadowMap].GetDimensions(width, height, layers);
    const float texel = 1.0 / float(width);
    float lit = 0.0;
    for (int dy = -1; dy <= 1; ++dy)
    {
        for (int dx = -1; dx <= 1; ++dx)
        {
            lit += g_TextureArrays[g_Push.shadowMap].SampleCmpLevelZero(
                g_ComparisonSamplers[g_Push.shadowSampler], float3(uv + float2(dx, dy) * texel, cascade), clip.z);
        }
    }
    return lit / 9.0;
}

/// Albedo from a texture projected along the three world axes, blended by the normal (no UVs needed, so fracture
/// faces of any orientation get undistorted texture).
float3 TriplanarAlbedo(uint texture, float3 worldPosition, float3 n, float scale)
{
    float3 weights = pow(abs(n), 4.0);
    weights /= weights.x + weights.y + weights.z;
    const float3 p = worldPosition * scale;
    const SamplerState s = g_Samplers[NonUniformResourceIndex(g_Push.materialSampler)];
    const Texture2D t = g_Textures[NonUniformResourceIndex(texture)];
    return t.Sample(s, p.zy).rgb * weights.x + t.Sample(s, p.xz).rgb * weights.y + t.Sample(s, p.xy).rgb * weights.z;
}

/// 4x4 ordered-dither threshold in (0, 1) for a pixel.
float DitherThreshold(float2 pixel)
{
    static const float kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    const uint2 p = uint2(pixel) & 3u;
    return (kBayer[p.y * 4 + p.x] + 0.5) / 16.0;
}

float4 main(ForwardVaryings input) : SV_Target0
{
    const uint scene = g_Push.scene;
    const uint object = ObjectOffset(input.objectIndex);
    const float opacity = LoadFloat(scene, object + kObjectOpacity);
    if (opacity < 1.0 && opacity < DitherThreshold(input.position.xy))
    {
        discard;
    }
    const float4 baseColorMetallic = g_Buffers[scene].Load<float4>(object + kObjectBaseColorMetallic);
    float3 baseColor = baseColorMetallic.rgb;
    const float metallic = baseColorMetallic.a;
    const float roughness = max(LoadFloat(scene, object + kObjectRoughness), 0.04);
    const float3 cameraPosition = g_Buffers[scene].Load<float3>(kFrameCameraPosition);
    const float4 sun = g_Buffers[scene].Load<float4>(kFrameSunDirection);
    const float3 sunColor = g_Buffers[scene].Load<float3>(kFrameSunColor) * sun.w;
    const float3 ambient = g_Buffers[scene].Load<float3>(kFrameAmbient);

    const float3 n = normalize(input.normal);
    const uint albedoTexture = LoadUint(scene, object + kObjectAlbedoTexture);
    if (albedoTexture != kInvalidIndex)
    {
        baseColor *= TriplanarAlbedo(albedoTexture, input.worldPosition, n, LoadFloat(scene, object + kObjectTextureScale));
    }
    const float3 v = normalize(cameraPosition - input.worldPosition);
    const float viewDepth = -mul(LoadMat4(scene, kFrameView), float4(input.worldPosition, 1.0)).z;

    float3 color = ambient * baseColor;
    const float visibility = SunVisibility(input.worldPosition, n, viewDepth);
    color += Brdf(n, v, normalize(-sun.xyz), sunColor * visibility, baseColor, metallic, roughness);

    // Point lights of this pixel's cluster.
    const uint tileX = min(uint(input.position.x * kTilesX / LoadFloat(scene, kFrameScreenWidth)), kTilesX - 1);
    const uint tileY = min(uint(input.position.y * kTilesY / LoadFloat(scene, kFrameScreenHeight)), kTilesY - 1);
    const uint cluster = tileX + tileY * kTilesX + SliceOf(scene, viewDepth) * kTilesX * kTilesY;
    const uint base = cluster * kClusterStride;
    const uint count = g_Buffers[g_Push.clusters].Load(base);
    const uint lightsOffset = LoadUint(scene, kFrameLightsOffset);
    for (uint i = 0; i < count; ++i)
    {
        const uint light = g_Buffers[g_Push.clusters].Load(base + 4 + i * 4);
        const float4 positionRadius = g_Buffers[scene].Load<float4>(lightsOffset + light * kLightStride);
        const float4 colorIntensity = g_Buffers[scene].Load<float4>(lightsOffset + light * kLightStride + 16);
        const float3 toLight = positionRadius.xyz - input.worldPosition;
        const float distanceSquared = dot(toLight, toLight);
        const float ratio = distanceSquared / (positionRadius.w * positionRadius.w);
        const float window = saturate(1.0 - ratio * ratio);
        const float attenuation = window * window / (distanceSquared + 1.0);
        color += Brdf(n, v, toLight * rsqrt(distanceSquared), colorIntensity.rgb * colorIntensity.a * attenuation,
                      baseColor, metallic, roughness);
    }
    return float4(color, 1.0);
}
