// Added: the radial blur of stdRenderBabylon's light-shaft stage (M9.16), used with
// present.vert.hlsl. Written for this project, from the standard screen-space light-shaft
// formulation: march from each pixel toward the luminance-weighted sky centroid, sample the sky mask
// along the ray and weight the samples with an exponential decay, so the mask smears into shafts that
// are brightest at the sky and fade with distance. The smoothed strength scales the result, so the
// shafts go to zero when no sky is visible. It runs at half the scene size.
//
// Fragment texture slot 0 is the half-res mask (linear), slot 1 the 1 x 1 centroid (nearest) and
// slot 2 the 1 x 1 smoothed strength (nearest). Uniform block 0: x decay per sample, y length of the
// ray as a fraction of the distance to the centroid, zw unused.

Texture2D<float4> gMask : register(t0, space2);
SamplerState gMaskSampler : register(s0, space2);
Texture2D<float4> gCentroid : register(t1, space2);
SamplerState gCentroidSampler : register(s1, space2);
Texture2D<float4> gStrength : register(t2, space2);
SamplerState gStrengthSampler : register(s2, space2);

cbuffer LightShaftBlur : register(b0, space3)
{
    float4 gBlur; // x: decay; y: ray length
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 centroidSample = gCentroid.SampleLevel(gCentroidSampler, float2(0.5, 0.5), 0);
    const float weight = centroidSample.x;
    const float2 center = weight > 1.0e-5 ? centroidSample.yz / weight : float2(0.5, 0.5);
    const float strength = gStrength.SampleLevel(gStrengthSampler, float2(0.5, 0.5), 0).x;

    // 40 taps, enough to smear the mask without a visible band; the decay makes the near samples
    // count more, so the shafts point away from the light.
    const int numSamples = 40;
    const float2 march = (center - uv) * gBlur.y / (float)numSamples;
    float2 coord = uv;
    float accum = 0.0;
    float total = 0.0;
    float decay = 1.0;
    for ( int i = 0; i < numSamples; ++i )
    {
        accum += gMask.SampleLevel(gMaskSampler, coord, 0).x * decay;
        total += decay;
        decay *= gBlur.x;
        coord += march;
    }

    const float shafts = total > 1.0e-5 ? accum / total : 0.0;
    return float4(shafts * strength, 0.0, 0.0, 1.0);
}
