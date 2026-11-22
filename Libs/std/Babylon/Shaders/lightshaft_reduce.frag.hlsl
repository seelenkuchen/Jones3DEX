// Added: one downsample step of stdRenderBabylon's light-shaft stage (M9.16), used with
// present.vert.hlsl. Written for this project. A four-tap box averages the previous level into the
// next, halving each axis, until a 1 x 1 target holds the mean mask weight and the mean weighted
// position. Averaging at every step keeps the values in 0..1, so a half-float target cannot
// overflow on a large screen, and the ratio y / x stays the centroid.
//
// Fragment texture slot 0 is the previous level. Uniform block 0: xy is one source texel.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer LightShaftReduce : register(b0, space3)
{
    float4 gReduce; // x, y: 1 / source width, 1 / source height
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float2 halfTexel = gReduce.xy * 0.5;
    const float4 sum = gSource.SampleLevel(gSampler, uv + float2(-halfTexel.x, -halfTexel.y), 0)
        + gSource.SampleLevel(gSampler, uv + float2( halfTexel.x, -halfTexel.y), 0)
        + gSource.SampleLevel(gSampler, uv + float2(-halfTexel.x,  halfTexel.y), 0)
        + gSource.SampleLevel(gSampler, uv + float2( halfTexel.x,  halfTexel.y), 0);
    return sum * 0.25;
}
