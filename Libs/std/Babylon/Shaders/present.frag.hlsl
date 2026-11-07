// Added: the present pass of stdRenderBabylon.cpp: the back buffer color target, sampled in
// fragment texture slot 0 (SDL GPU: fragment textures and samplers in register space2).

Texture2D<float4> gBackBuffer : register(t0, space2);
SamplerState gSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    return gBackBuffer.Sample(gSampler, uv);
}
