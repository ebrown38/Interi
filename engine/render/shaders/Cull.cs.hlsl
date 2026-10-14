// SPDX-License-Identifier: MIT
// GPU-driven culling: one thread per object tests its bounding sphere against one view's frustum (the camera or a
// shadow cascade) and, for the camera, against the occluders' hierarchical depth (Hi-Z); it appends a
// DrawIndexedIndirect command for each visible object. The command's firstInstance is the object index, which the
// vertex shaders read back as BaseInstance.
#include "ForwardCommon.hlsli"

struct Push
{
    uint scene;
    uint objectCount;
    uint viewMatrixOffset; ///< Byte offset in the scene buffer of the view's world -> clip matrix.
    uint requiredFlags;    ///< Objects need all these flags (kObjectCastsShadow for cascades, kObjectOccluder...).
    uint arguments;        ///< Bindless index of the indirect argument buffer.
    uint argumentsOffset;  ///< Byte offset of this view's first command (20 bytes each).
    uint count;            ///< Bindless index of the draw count buffer.
    uint countOffset;      ///< Byte offset of this view's count.
    uint hiZ;              ///< Bindless texture index of the depth pyramid, or kInvalidIndex for no occlusion test.
};

/// True when the occluders hide the whole sphere (camera view only). The sphere's view-space bounding box is
/// projected to a screen rectangle; in the pyramid level where it spans at most 2x2 texels, the farthest occluder
/// depth there is compared with the sphere's nearest depth (reverse-Z: larger is nearer).
bool Occluded(uint scene, uint hiZ, float3 centre, float radius)
{
    const float4x4 view = LoadMat4(scene, kFrameView);
    const float3 viewCentre = mul(view, float4(centre, 1.0)).xyz;
    const float nearPlane = LoadFloat(scene, kFrameClusterNear);
    const float distance = -viewCentre.z;
    if (distance - radius <= nearPlane)
    {
        return false; // touches the near plane: cannot be projected safely
    }
    const float tanHalf = LoadFloat(scene, kFrameTanHalfFovY);
    const float2 scale = float2(1.0 / (tanHalf * LoadFloat(scene, kFrameAspect)), 1.0 / tanHalf);
    float2 ndcMin = float2(1e30, 1e30);
    float2 ndcMax = float2(-1e30, -1e30);
    for (uint corner = 0; corner < 8; ++corner)
    {
        const float3 offset = float3((corner & 1) ? radius : -radius, (corner & 2) ? radius : -radius,
                                     (corner & 4) ? radius : -radius);
        const float3 p = viewCentre + offset;
        const float2 ndc = p.xy * scale / -p.z;
        ndcMin = min(ndcMin, ndc);
        ndcMax = max(ndcMax, ndc);
    }
    const float2 screen = float2(LoadFloat(scene, kFrameScreenWidth), LoadFloat(scene, kFrameScreenHeight));
    // NDC +Y is the top of the screen; pixel rows count from the top.
    const float2 pixelMin = clamp(float2(ndcMin.x * 0.5 + 0.5, 0.5 - ndcMax.y * 0.5) * screen, 0.0, screen - 1.0);
    const float2 pixelMax = clamp(float2(ndcMax.x * 0.5 + 0.5, 0.5 - ndcMin.y * 0.5) * screen, 0.0, screen - 1.0);

    uint width;
    uint height;
    uint levels;
    g_Textures[hiZ].GetDimensions(0, width, height, levels);
    const float extent = max(pixelMax.x - pixelMin.x, pixelMax.y - pixelMin.y);
    const uint mip = min(uint(ceil(log2(max(extent, 1.0)))), levels - 1);
    const uint2 mipSize = uint2(max(width >> mip, 1u), max(height >> mip, 1u));
    const uint2 first = min(uint2(pixelMin) >> mip, mipSize - 1);
    const uint2 last = min(uint2(pixelMax) >> mip, mipSize - 1);
    float farthest = 1.0;
    for (uint y = first.y; y <= last.y; ++y)
    {
        for (uint x = first.x; x <= last.x; ++x)
        {
            farthest = min(farthest, g_Textures[hiZ].Load(int3(x, y, mip)).r);
        }
    }
    const float nearestDepth = nearPlane / (distance - radius);
    return nearestDepth < farthest;
}
[[vk::push_constant]] Push g_Push;

[numthreads(64, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID)
{
    const uint object = dispatchId.x;
    if (object >= g_Push.objectCount)
    {
        return;
    }
    const uint scene = g_Push.scene;
    const uint base = ObjectOffset(object);
    const uint4 draw = g_Buffers[scene].Load<uint4>(base + kObjectDraw); // firstIndex, indexCount, vertexOffset, flags
    if ((draw.w & g_Push.requiredFlags) != g_Push.requiredFlags)
    {
        return;
    }

    // World-space bounding sphere (radius scaled by the largest axis scale).
    const float4x4 model = LoadMat4(scene, base + kObjectModel);
    const float4 bounds = g_Buffers[scene].Load<float4>(base + kObjectBounds);
    const float3 centre = mul(model, float4(bounds.xyz, 1.0)).xyz;
    const float scale = sqrt(max(dot(model._m00_m10_m20, model._m00_m10_m20),
                                 max(dot(model._m01_m11_m21, model._m01_m11_m21),
                                     dot(model._m02_m12_m22, model._m02_m12_m22))));
    const float radius = bounds.w * scale;

    // Frustum planes from the rows of the clip matrix (clip space: -w <= x, y <= w and 0 <= z <= w).
    const float4x4 clip = LoadMat4(scene, g_Push.viewMatrixOffset);
    const float4 planes[6] = {clip[3] + clip[0], clip[3] - clip[0], clip[3] + clip[1],
                              clip[3] - clip[1], clip[2],           clip[3] - clip[2]};
    for (uint i = 0; i < 6; ++i)
    {
        const float length = sqrt(dot(planes[i].xyz, planes[i].xyz));
        if (length > 0.0 && dot(planes[i].xyz, centre) + planes[i].w < -radius * length)
        {
            return;
        }
    }

    if (g_Push.hiZ != kInvalidIndex && Occluded(scene, g_Push.hiZ, centre, radius))
    {
        return;
    }

    uint slot;
    g_Buffers[g_Push.count].InterlockedAdd(g_Push.countOffset, 1, slot);
    const uint command = g_Push.argumentsOffset + slot * 20;
    g_Buffers[g_Push.arguments].Store(command, draw.y);        // indexCount
    g_Buffers[g_Push.arguments].Store(command + 4, 1);         // instanceCount
    g_Buffers[g_Push.arguments].Store(command + 8, draw.x);    // firstIndex
    g_Buffers[g_Push.arguments].Store(command + 12, draw.z);   // vertexOffset
    g_Buffers[g_Push.arguments].Store(command + 16, object);   // firstInstance = object index
}
