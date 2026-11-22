// Added: the vignette and film-grain composite of stdRenderBabylon's M9.18 camera effects, used
// with present.vert.hlsl. Written for this project. It runs on the presented-size LDR image after
// SMAA and sharpening and before the HUD layer, so the HUD, menus and prompts never get the
// vignette or the grain. The source is sampled nearest at 1:1, so a pixel is copied exactly when
// its effect is off; the whole pass is skipped when both effects are off.
//
// Vignette: a smooth radial darkening, aspect-corrected so the corners of any window shape darken
// equally, with the centre untouched. Film grain: luminance noise from a hash of the pixel and a
// frame counter, weaker in the highlights, with zero mean so it does not shift the exposure.
//
// Fragment texture slot 0 is the LDR image (nearest). Uniform block 0: gSize.xy the image size in
// pixels; gVignette.x the vignette strength, y and z its inner and outer radii; gGrain.x the
// grain amplitude, y the game-time frame counter.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer CameraFxSize : register(b0, space3)
{
    float4 gSize;
};

cbuffer CameraFxVignette : register(b1, space3)
{
    float4 gVignette;
};

cbuffer CameraFxGrain : register(b2, space3)
{
    float4 gGrain;
};

// A 32-bit integer hash of the pixel and frame, in 0..1. A hash instead of rand() keeps the grain
// deterministic for the same frame counter.
float Hash(uint x, uint y, uint frame)
{
    uint h = x * 374761393u + y * 668265263u + frame * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)h / 4294967295.0;
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 color = gSource.SampleLevel(gSampler, uv, 0);

    if ( gVignette.x > 0.0 )
    {
        const float aspect = gSize.x / max(gSize.y, 1.0);
        const float2 offset = (uv - 0.5) * float2(aspect, 1.0);
        const float corner = 0.5 * sqrt(aspect * aspect + 1.0);
        const float radius = length(offset) / max(corner, 1.0e-4);
        const float darkening = smoothstep(gVignette.y, max(gVignette.z, gVignette.y + 1.0e-4),
            radius);
        color.rgb *= 1.0 - gVignette.x * darkening;
    }

    if ( gGrain.x > 0.0 )
    {
        const uint2 pixel = (uint2)(uv * gSize.xy);
        const float noise = (Hash(pixel.x, pixel.y, (uint)gGrain.y) - 0.5) * 2.0;
        const float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
        const float amplitude = gGrain.x * (1.0 - saturate(luma) * 0.75);
        color.rgb = saturate(color.rgb + noise * amplitude);
    }

    return color;
}
