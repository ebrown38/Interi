// SPDX-License-Identifier: MIT
// Forward pass pixel shader: metallic-roughness PBR (GGX / Smith-Schlick / Fresnel-Schlick + Lambert) lit by one
// directional light plus a constant ambient term. Output is linear; an sRGB target encodes it.
#include "ForwardCommon.hlsli"

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

float4 main(ForwardVaryings input) : SV_Target0
{
    const uint object = ObjectOffset();
    const float4 baseColorMetallic = g_Buffers[g_Push.scene].Load<float4>(object + kObjectBaseColorMetallic);
    const float3 baseColor = baseColorMetallic.rgb;
    const float metallic = baseColorMetallic.a;
    const float roughness = max(g_Buffers[g_Push.scene].Load<float>(object + kObjectRoughness), 0.04);
    const float3 cameraPosition = g_Buffers[g_Push.scene].Load<float3>(kFrameCameraPosition);
    const float4 sun = g_Buffers[g_Push.scene].Load<float4>(kFrameSunDirection);
    const float3 sunColor = g_Buffers[g_Push.scene].Load<float3>(kFrameSunColor) * sun.w;
    const float3 ambient = g_Buffers[g_Push.scene].Load<float3>(kFrameAmbient);

    const float3 n = normalize(input.normal);
    const float3 v = normalize(cameraPosition - input.worldPosition);
    const float3 l = normalize(-sun.xyz);
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

    const float3 color = (diffuse + specular) * sunColor * nDotL + ambient * baseColor;
    return float4(color, 1.0);
}
