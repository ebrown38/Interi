// SPDX-License-Identifier: MIT
// Inclusive prefix sum of uints, 256 per workgroup (Hillis-Steele in groupshared memory).
// Mode 0 scans each block in place and, when blockSums is valid, writes each block's total.
// Mode 1 adds the inclusive scan of the block totals (blockSums) of the preceding blocks to every element.
#include <interi/Bindless.hlsli>

static const uint kNoBuffer = 0xFFFFFFFFu;
static const uint kGroupSize = 256;

struct Push
{
    uint data;
    uint blockSums;
    uint count;
    uint mode;
};
[[vk::push_constant]] Push g_Push;

groupshared uint s_Scan[kGroupSize];

[numthreads(kGroupSize, 1, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    const uint i = dispatchId.x;
    if (g_Push.mode == 1)
    {
        if (groupId.x > 0 && i < g_Push.count)
        {
            const uint offset = g_Buffers[g_Push.blockSums].Load((groupId.x - 1) * 4);
            g_Buffers[g_Push.data].Store(i * 4, g_Buffers[g_Push.data].Load(i * 4) + offset);
        }
        return;
    }

    s_Scan[groupIndex] = i < g_Push.count ? g_Buffers[g_Push.data].Load(i * 4) : 0;
    GroupMemoryBarrierWithGroupSync();
    for (uint stride = 1; stride < kGroupSize; stride <<= 1)
    {
        const uint add = groupIndex >= stride ? s_Scan[groupIndex - stride] : 0;
        GroupMemoryBarrierWithGroupSync();
        s_Scan[groupIndex] += add;
        GroupMemoryBarrierWithGroupSync();
    }
    if (i < g_Push.count)
    {
        g_Buffers[g_Push.data].Store(i * 4, s_Scan[groupIndex]);
    }
    if (groupIndex == kGroupSize - 1 && g_Push.blockSums != kNoBuffer)
    {
        g_Buffers[g_Push.blockSums].Store(groupId.x * 4, s_Scan[groupIndex]);
    }
}
