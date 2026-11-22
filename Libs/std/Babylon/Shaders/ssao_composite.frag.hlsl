// Added: the composite pass of stdRenderBabylon's screen-space ambient occlusion stage (M9.17b),
// used with present.vert.hlsl. Written for this project. It upsamples the blurred half-res
// occlusion term (linear, depth-aware neighbours were already weighted in the blur) and multiplies
// the HDR scene colour by it. Sky, water and empty pixels are returned unchanged, so the stage
// never darkens the sky, translucent water, the HUD, menus or prompts.
//
// Fragment texture slot 0 is the HDR scene (nearest), slot 1 the blurred occlusion term (linear),
// slot 2 the normal target (nearest). Uniform block 0: x is the ordinary surface tag; the rest is
// unused.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gSsao : register(t1, space2);
SamplerState gSsaoSampler : register(s1, space2);
Texture2D<float4> gNormal : register(t2, space2);
SamplerState gNormalSampler : register(s2, space2);

cbuffer SsaoComposite : register(b0, space3)
{
    float4 gComposite; // x: ordinary surface tag
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    const float tag = gNormal.SampleLevel(gNormalSampler, uv, 0).a;
    if ( abs(tag - gComposite.x) > 0.02 )
    {
        return scene;
    }

    const float ao = saturate(gSsao.SampleLevel(gSsaoSampler, uv, 0).r);
    return float4(scene.rgb * ao, scene.a);
}
