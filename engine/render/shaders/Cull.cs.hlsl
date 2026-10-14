// SPDX-License-Identifier: MIT
// GPU-driven culling: one thread per object tests its bounding sphere against one view's frustum (the camera or a
// shadow cascade) and appends a DrawIndexedIndirect command for each visible object. The command's firstInstance
// is the object index, which the vertex shaders read back as BaseInstance.
#include "ForwardCommon.hlsli"

struct Push
{
    uint scene;
    uint objectCount;
    uint viewMatrixOffset; ///< Byte offset in the scene buffer of the view's world -> clip matrix.
    uint shadowView;       ///< 1 for shadow cascades: objects that do not cast shadows are skipped.
    uint arguments;        ///< Bindless index of the indirect argument buffer.
    uint argumentsOffset;  ///< Byte offset of this view's first command (20 bytes each).
    uint count;            ///< Bindless index of the draw count buffer.
    uint countOffset;      ///< Byte offset of this view's count.
};
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
    if (g_Push.shadowView != 0 && (draw.w & kObjectCastsShadow) == 0)
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

    uint slot;
    g_Buffers[g_Push.count].InterlockedAdd(g_Push.countOffset, 1, slot);
    const uint command = g_Push.argumentsOffset + slot * 20;
    g_Buffers[g_Push.arguments].Store(command, draw.y);        // indexCount
    g_Buffers[g_Push.arguments].Store(command + 4, 1);         // instanceCount
    g_Buffers[g_Push.arguments].Store(command + 8, draw.x);    // firstIndex
    g_Buffers[g_Push.arguments].Store(command + 12, draw.z);   // vertexOffset
    g_Buffers[g_Push.arguments].Store(command + 16, object);   // firstInstance = object index
}
