// Added: one upsample step of stdRenderBabylon's built-in bloom stage, used with present.vert.hlsl.
// A separable 3 x 3 tent filter widens the smaller bloom target and the pass blends its result into
// the next larger target with additive blending, which still holds that level's downsampled image.
// gUpsample.xy is the source texel size; running from 1/32 back to 1/2 accumulates the falloff.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer BloomUpsample : register(b0, space3)
{
    float4 gUpsample; // x: 1 / source width, y: 1 / source height
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float2 texel = gUpsample.xy;
    float3 color = 1.0 * gSource.SampleLevel(gSampler, uv + texel * float2(-1.0, -1.0), 0).rgb;
    color += 2.0 * gSource.SampleLevel(gSampler, uv + texel * float2( 0.0, -1.0), 0).rgb;
    color += 1.0 * gSource.SampleLevel(gSampler, uv + texel * float2( 1.0, -1.0), 0).rgb;
    color += 2.0 * gSource.SampleLevel(gSampler, uv + texel * float2(-1.0,  0.0), 0).rgb;
    color += 4.0 * gSource.SampleLevel(gSampler, uv, 0).rgb;
    color += 2.0 * gSource.SampleLevel(gSampler, uv + texel * float2( 1.0,  0.0), 0).rgb;
    color += 1.0 * gSource.SampleLevel(gSampler, uv + texel * float2(-1.0,  1.0), 0).rgb;
    color += 2.0 * gSource.SampleLevel(gSampler, uv + texel * float2( 0.0,  1.0), 0).rgb;
    color += 1.0 * gSource.SampleLevel(gSampler, uv + texel * float2( 1.0,  1.0), 0).rgb;
    return float4(color / 16.0, 1.0);
}
