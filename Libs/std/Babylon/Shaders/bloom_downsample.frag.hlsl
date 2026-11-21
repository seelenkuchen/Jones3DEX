// Added: one downsample step of stdRenderBabylon's built-in bloom stage, used with present.vert.hlsl.
// A four-tap bilinear box halves the source in each axis into the next, smaller bloom target. The
// blur chain runs this from the half-size prefilter target down to 1/4, 1/8, 1/16 and 1/32 of the
// scene. gDownsample.xy is the source texel size.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer BloomDownsample : register(b0, space3)
{
    float4 gDownsample; // x: 1 / source width, y: 1 / source height
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float2 texel = gDownsample.xy;
    const float3 color = 0.25 * (
        gSource.SampleLevel(gSampler, uv + texel * float2(-0.5, -0.5), 0).rgb
        + gSource.SampleLevel(gSampler, uv + texel * float2( 0.5, -0.5), 0).rgb
        + gSource.SampleLevel(gSampler, uv + texel * float2(-0.5,  0.5), 0).rgb
        + gSource.SampleLevel(gSampler, uv + texel * float2( 0.5,  0.5), 0).rgb);
    return float4(color, 1.0);
}
