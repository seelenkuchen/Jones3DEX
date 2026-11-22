// Added: the strength pass of stdRenderBabylon's light-shaft stage (M9.16), used with
// present.vert.hlsl. Written for this project. It turns the 1 x 1 centroid's mean mask weight into a
// strength (0 with no sky visible, 1 from the full-coverage threshold up) and smooths it against the
// previous frame over about 0.3 s of game time, so the shafts fade instead of popping as the sky
// enters or leaves the view.
//
// Fragment texture slot 0 is the centroid (x: mean weight), slot 1 the previous smoothed strength.
// Uniform block 0: x the blend to the new strength (1 snaps), y and z the coverage thresholds.

Texture2D<float4> gCentroid : register(t0, space2);
SamplerState gCentroidSampler : register(s0, space2);
Texture2D<float4> gPrevious : register(t1, space2);
SamplerState gPreviousSampler : register(s1, space2);

cbuffer LightShaftStrength : register(b0, space3)
{
    float4 gStrength; // x: blend; y: min coverage; z: full coverage
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float coverage = gCentroid.SampleLevel(gCentroidSampler, uv, 0).x;
    const float target = smoothstep(gStrength.y, max(gStrength.z, gStrength.y + 1.0e-6), coverage);
    const float previous = gPrevious.SampleLevel(gPreviousSampler, uv, 0).x;
    const float smoothed = lerp(previous, target, saturate(gStrength.x));
    return float4(smoothed, 0.0, 0.0, 1.0);
}
