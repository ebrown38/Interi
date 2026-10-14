// SPDX-License-Identifier: MIT
// Writes the push-constant colour.
struct Push
{
    uint vertices;
    uint pad0;
    uint pad1;
    uint pad2;
    float4 color;
};
[[vk::push_constant]] Push g_Push;

float4 main() : SV_Target0
{
    return g_Push.color;
}
