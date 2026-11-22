// Added: the composite of stdRenderBabylon's light-shaft stage (M9.16), used with
// present.vert.hlsl. Written for this project. It adds the half-res radial blur, upsampled linearly
// and scaled by the option's strength, to the HDR scene and writes the scene-sized target that bloom
// and the tone map then read. On a pixel whose normal-target alpha carries the sky tag the shafts are
// scaled by the stage's sky factor, so the blur does not smear the clouds themselves; elsewhere they
// are added at full strength, so the rays show over the geometry in front of the sky. The scene is
// sampled 1:1 with the nearest sampler; its alpha is kept.
//
// Fragment texture slot 0 is the scene, slot 1 the half-res shafts, slot 2 the normal target.
// Uniform block 0: x the strength, y the sky factor, z the sky tag.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gShafts : register(t1, space2);
SamplerState gShaftsSampler : register(s1, space2);
Texture2D<float4> gNormal : register(t2, space2);
SamplerState gNormalSampler : register(s2, space2);

cbuffer LightShaftComposite : register(b0, space3)
{
    float4 gComposite; // x: strength; y: sky factor; z: sky tag
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    const float shafts = gShafts.SampleLevel(gShaftsSampler, uv, 0).x;
    const float normalAlpha = gNormal.SampleLevel(gNormalSampler, uv, 0).a;
    const float isSky = 1.0 - step(0.02, abs(normalAlpha - gComposite.z));
    const float scale = lerp(1.0, gComposite.y, isSky);
    return float4(scene.rgb + shafts * gComposite.x * scale, scene.a);
}
