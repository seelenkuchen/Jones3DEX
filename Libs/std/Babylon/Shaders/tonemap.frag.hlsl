// Added: the built-in final tone-map pass of stdRenderBabylon.cpp, used with present.vert.hlsl. It
// samples the post-processing chain's output (the back buffer color for the first pass) in fragment
// texture slot 0 and writes the presented frame: the swapchain, or the target of
// stdRenderBabylon_RenderPresentToTexture. The color is display-referred, as the scene writes it
// today, so there is no transfer function: output gamma is exactly the plain present pass's. The
// parameters are exposure and the operator in fragment uniform block 0.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer ToneMap : register(b0, space3)
{
    float4 gToneMap; // x: exposure; y: operator (0 = none, clamp to 0..1)
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 color = gSource.Sample(gSampler, uv);
    color.rgb *= gToneMap.x;
    // Operator 0: no curve, only the target format's own clamp. Other operators join here.
    color.rgb = saturate(color.rgb);
    return color;
}
