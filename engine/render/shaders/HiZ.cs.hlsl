// SPDX-License-Identifier: MIT
// Builds the hierarchical depth pyramid used for occlusion culling. Reverse-Z: each texel keeps the minimum (the
// farthest) depth of its footprint, so "everything here is at least this near" holds at every level.
// Mode 0 copies the occluder depth buffer into mip 0; mode 1 reduces mip (n - 1) into mip n. When the source has
// an odd size, the last texel of a row or column also takes in the extra source texel, so no texel is skipped.
#include <interi/Bindless.hlsli>

struct Push
{
    uint mode;
    uint source; ///< Mode 0: bindless texture (depth). Mode 1: bindless storage image (previous mip).
    uint destination;
    uint pad;
    uint2 sourceSize;
    uint2 destinationSize;
};
[[vk::push_constant]] Push g_Push;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= g_Push.destinationSize.x || id.y >= g_Push.destinationSize.y)
    {
        return;
    }
    if (g_Push.mode == 0)
    {
        g_StorageImages[g_Push.destination][id.xy] = g_Textures[g_Push.source].Load(int3(id.xy, 0)).r;
        return;
    }
    const uint2 first = id.xy * 2;
    uint2 last = min(first + 1, g_Push.sourceSize - 1);
    if (id.x == g_Push.destinationSize.x - 1)
    {
        last.x = g_Push.sourceSize.x - 1;
    }
    if (id.y == g_Push.destinationSize.y - 1)
    {
        last.y = g_Push.sourceSize.y - 1;
    }
    float depth = 1.0;
    for (uint y = first.y; y <= last.y; ++y)
    {
        for (uint x = first.x; x <= last.x; ++x)
        {
            depth = min(depth, g_StorageImages[g_Push.source][uint2(x, y)]);
        }
    }
    g_StorageImages[g_Push.destination][id.xy] = depth;
}
