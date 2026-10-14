// SPDX-License-Identifier: MIT
// Bindless resource tables shared by every pipeline (descriptor set 0). Shaders receive table indices from
// IDevice::BindlessIndex through push constants. Buffers are raw (byte-address) so one table serves every layout.
#ifndef INTERI_BINDLESS_HLSLI
#define INTERI_BINDLESS_HLSLI

[[vk::binding(0, 0)]] RWByteAddressBuffer g_Buffers[];
[[vk::binding(1, 0)]] Texture2D g_Textures[];
[[vk::binding(2, 0)]] SamplerState g_Samplers[];

/// Loads an engine Mat4 (16 floats, column after column) and returns the HLSL matrix M for which mul(M, v) equals
/// the engine's M * v. float4x4(a, b, c, d) takes rows, so the stored columns are transposed into place.
float4x4 LoadMat4(uint buffer, uint offset)
{
    const float4 c0 = g_Buffers[buffer].Load<float4>(offset);
    const float4 c1 = g_Buffers[buffer].Load<float4>(offset + 16);
    const float4 c2 = g_Buffers[buffer].Load<float4>(offset + 32);
    const float4 c3 = g_Buffers[buffer].Load<float4>(offset + 48);
    return transpose(float4x4(c0, c1, c2, c3));
}

#endif
