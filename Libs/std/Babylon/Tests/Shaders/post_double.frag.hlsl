// Added: a test pass of stdRenderBabylon's post-processing chain (std3DBabylonTest.cpp) that doubles
// its input. It writes values above 1 through the Rgba16F intermediate to the tone map. Fragment
// texture slot 0 (SDL GPU: space2).

Texture2D<float4> gPrevious : register(t0, space2);
SamplerState gPreviousSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 color = gPrevious.Sample(gPreviousSampler, uv);
    return float4(color.rgb * 2.0, color.a);
}
