// Added: the built-in final tone-map pass of stdRenderBabylon.cpp, used with present.vert.hlsl. It
// samples the post-processing chain's output (the back buffer color for the first pass) in fragment
// texture slot 0 and writes the presented frame: the swapchain, or the target of
// stdRenderBabylon_RenderPresentToTexture. The color is display-referred, as the scene writes it
// today, so there is no transfer function: output gamma is exactly the plain present pass's. The
// parameters are exposure (multiplied in before the operator), the operator, brightness and color
// grading in fragment uniform block 0: operator 0 clamp, 1 Khronos PBR Neutral, 2 ACES fitted;
// brightness is the gamma applied after grading, and 1 skips it so the default is byte-identical.
//
// Added: color grading samples a 32^3 lookup table stored as a 1024 x 32 Rgba8 strip of 32 slices
// side by side in fragment texture slot 1. Within a slice x is red, y is green and the slice is
// blue; the shader samples the two slices around the fragment's blue with a linear sampler and
// interpolates manually, so a value between slices is a blend of both. Preset 0 (Neutral) skips the
// lookup entirely, so the pass's output stays byte-identical to the path without the option.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);
Texture2D<float4> gGrading : register(t1, space2);
SamplerState gGradingSampler : register(s1, space2);

cbuffer ToneMap : register(b0, space3)
{
    float4 gToneMap; // x: exposure; y: operator (0 = clamp, 1 = Neutral, 2 = ACES fitted); z: brightness; w: color grading preset
};

// Added: the 32^3 grading lookup as a 1024 x 32 strip of 32-wide slices. The table's texel centers
// are at (k + 0.5) / 32, so a sample at value v lands at (v * 31 + 0.5) texels, never blending a
// slice's edge with its neighbour. Blue selects two neighbouring slices, interpolated here.
float3 ApplyColorGrading(float3 color)
{
    const float size = 32.0;
    const float maxIndex = size - 1.0;
    color = saturate(color);

    const float blue  = color.b * maxIndex;
    const float slice0 = floor(blue);
    const float slice1 = min(slice0 + 1.0, maxIndex);
    const float blend  = blue - slice0;

    const float v = (color.g * maxIndex + 0.5) / size;
    const float u0 = (slice0 * size + color.r * maxIndex + 0.5) / (size * size);
    const float u1 = (slice1 * size + color.r * maxIndex + 0.5) / (size * size);

    const float3 sample0 = gGrading.Sample(gGradingSampler, float2(u0, v)).rgb;
    const float3 sample1 = gGrading.Sample(gGradingSampler, float2(u1, v)).rgb;
    return lerp(sample0, sample1, blend);
}

// Added: Khronos PBR Neutral tone mapping, from the PBR_Neutral reference implementation
// (https://github.com/KhronosGroup/ToneMapping, Apache-2.0). The reference subtracts the constant
// Fresnel offset F90 = 0.04 from every channel before compressing; the game's scene color is
// display-referred, so that offset is omitted and midtones pass through unchanged, while the
// highlight compression (K_s = 0.8 - 0.04) and desaturation (K_d = 0.15) are the reference's.
float3 ToneMapNeutral(float3 color)
{
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    const float peak = max(color.r, max(color.g, color.b));
    if (peak <= startCompression)
    {
        return color;
    }

    const float d = 1.0 - startCompression;
    const float newPeak = 1.0 - d * d / (peak + d - startCompression);
    color *= newPeak / peak;

    const float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return lerp(color, float3(newPeak, newPeak, newPeak), g);
}

// Added: the ACES fitted curve, Krzysztof Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
// (https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/), a public formula.
float3 ToneMapAces(float3 color)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 color = gSource.Sample(gSampler, uv);
    color.rgb *= gToneMap.x;

    const uint oper = (uint)gToneMap.y;
    if (oper == 1)
    {
        color.rgb = ToneMapNeutral(color.rgb);
    }
    else if (oper == 2)
    {
        color.rgb = ToneMapAces(color.rgb);
    }

    // Operator 0 and the target format's own clamp; the curves also end inside 0..1.
    color.rgb = saturate(color.rgb);

    // Added: the color-grading preset after the operator and before brightness. Preset 0 Neutral
    // skips the lookup so the output is byte-identical to the path without grading.
    if ( gToneMap.w > 0.5f )
    {
        color.rgb = ApplyColorGrading(color.rgb);
    }

    // Added: the brightness calibration's gamma, applied after the operator. 1.0 is the identity
    // and skips the pow, so the default present is byte-identical to before.
    if ( gToneMap.z != 1.0f )
    {
        color.rgb = pow(color.rgb, 1.0f / gToneMap.z);
    }

    return color;
}
