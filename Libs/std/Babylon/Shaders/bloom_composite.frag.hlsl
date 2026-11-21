// Added: the composite of stdRenderBabylon's built-in bloom stage, used with present.vert.hlsl. It
// adds the blurred bloom result, times the option's strength, to the scene source and writes the
// scene-format target the tone map then reads. The scene is sampled 1:1 with the nearest sampler and
// the half-size bloom with the linear one. gComposite.x is the strength; the scene alpha is kept so
// the later HUD composite and captures keep their contract.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gBloom : register(t1, space2);
SamplerState gBloomSampler : register(s1, space2);

cbuffer BloomComposite : register(b0, space3)
{
    float4 gComposite; // x: strength
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    const float3 bloom = gBloom.SampleLevel(gBloomSampler, uv, 0).rgb;
    return float4(scene.rgb + bloom * gComposite.x, scene.a);
}
