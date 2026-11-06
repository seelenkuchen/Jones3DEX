// Added: a test pass of stdRenderBabylon's post-processing chain (std3DBabylonTest.cpp) that reads
// the normal target: its red, green and blue with the previous output's alpha. Fragment texture
// slots 0 (previous output) and 1 (normal target), SDL GPU: space2.

Texture2D<float4> gPrevious : register(t0, space2);
SamplerState gPreviousSampler : register(s0, space2);
Texture2D<float4> gNormal : register(t1, space2);
SamplerState gNormalSampler : register(s1, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 normal = gNormal.Sample(gNormalSampler, uv);
    return float4(normal.rgb, gPrevious.Sample(gPreviousSampler, uv).a);
}
