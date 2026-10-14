// SPDX-License-Identifier: MIT
// Pins the CPU/GPU matrix convention: input holds an engine Mat4 followed by a Vec4; output receives mul(M, v),
// which must equal the engine's M * v.
#include <interi/Bindless.hlsli>

struct Push
{
    uint input;
    uint output;
};
[[vk::push_constant]] Push g_Push;

[numthreads(1, 1, 1)]
void main()
{
    const float4x4 m = LoadMat4(g_Push.input, 0);
    const float4 v = g_Buffers[g_Push.input].Load<float4>(64);
    g_Buffers[g_Push.output].Store<float4>(0, mul(m, v));
}
