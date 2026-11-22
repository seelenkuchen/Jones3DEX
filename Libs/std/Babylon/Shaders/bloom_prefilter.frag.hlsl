// Added: the prefilter of stdRenderBabylon's built-in bloom stage, used with present.vert.hlsl. It
// downsamples the scene source to half size with a four-tap box and keeps the bright parts above a
// soft-knee luminance threshold. The game's colours are display referred (0..1), so the knee lets
// highlights below 1.0 glow while the threshold stays near white. It writes the half-size target;
// the blur chain downsamples and upsamples from there. The parameters are the threshold and knee in
// x and y and the source texel size in z and w, pushed as fragment uniform block 0.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer BloomPrefilter : register(b0, space3)
{
    float4 gPrefilter; // x: threshold, y: knee, z: 1 / source width, w: 1 / source height
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float2 texel = gPrefilter.zw;
    const float4 color = 0.25 * (
        gSource.SampleLevel(gSampler, uv + texel * float2(-0.5, -0.5), 0)
        + gSource.SampleLevel(gSampler, uv + texel * float2( 0.5, -0.5), 0)
        + gSource.SampleLevel(gSampler, uv + texel * float2(-0.5,  0.5), 0)
        + gSource.SampleLevel(gSampler, uv + texel * float2( 0.5,  0.5), 0));

    // Rec. 709 luminance of the display-referred colour.
    const float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));

    // A quadratic soft knee between threshold - knee and threshold + knee, then the hard threshold
    // above it. The knee is widened by a small epsilon so a zero knee stays finite.
    const float threshold = gPrefilter.x;
    const float knee = gPrefilter.y;
    const float soft = saturate((luma - threshold + knee) / (2.0 * knee + 1.0e-4));
    const float kneeContribution = soft * soft * knee;
    const float contribution = max(kneeContribution, luma - threshold) / max(luma, 1.0e-4);

    return float4(color.rgb * contribution, 1.0);
}
