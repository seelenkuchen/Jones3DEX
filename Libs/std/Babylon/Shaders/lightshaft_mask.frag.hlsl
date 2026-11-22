// Added: the mask pass of stdRenderBabylon's light-shaft stage (M9.16), used with
// present.vert.hlsl. Written for this project. It runs at half the scene size and keeps the sky
// pixels that are bright enough to throw shafts: the normal target's alpha identifies a sky surface
// (stdRenderBabylon_kSurfaceTags.sky) and the scene luminance has to pass a soft threshold, so a dark
// sky or lit geometry that is not sky contributes nothing.
//
// The output is (w, w * u, w * v, 0) with w the mask weight and (u, v) the pixel's centre. The
// following reduce passes average this to 1 x 1, whose y / x and z / x are the luminance-weighted
// centroid of the masked pixels; the mask's red channel is also the image the radial blur spreads.
//
// Fragment texture slot 0 is the HDR scene (linear sampler), slot 1 the normal target (nearest
// sampler). Uniform block 0: x is the sky tag, y and z the luminance threshold and knee, zw unused.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gNormal : register(t1, space2);
SamplerState gNormalSampler : register(s1, space2);

cbuffer LightShaftMask : register(b0, space3)
{
    float4 gMask; // x: sky tag; y: threshold; z: knee
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 normal = gNormal.SampleLevel(gNormalSampler, uv, 0);
    const float isSky = 1.0 - step(0.02, abs(normal.a - gMask.x));

    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    const float luma = dot(scene.rgb, float3(0.2126, 0.7152, 0.0722));
    const float bright = smoothstep(gMask.y, gMask.y + max(gMask.z, 1.0e-4), luma);

    const float w = isSky * bright;
    return float4(w, w * uv.x, w * uv.y, 0.0);
}
