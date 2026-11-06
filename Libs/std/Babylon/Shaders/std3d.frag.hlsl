// Added: the fragment shader of std3D_DrawRenderList (std3DBabylon.cpp), a port of the DirectX 9
// backend's default_ps.hlsl with ApplyFog from common.hlsli (std/Win95/DX9/Shaders before 82b8f607),
// plus the alpha test Direct3D 9 did in fixed function (std3DX9.c's InitRenderState and
// SetRenderState: D3DRS_ALPHATESTENABLE, D3DCMP_GREATER, D3DRS_ALPHAREF 0 or 160).
//
// The color is texture * diffuse, as DX9: an untextured draw samples a white texture, as std3DX9.c's
// SetTexture bound in shader mode. The specular color is not added: std3DX9.c turned
// D3DRS_SPECULARENABLE off and its pixel shader ignored it. Fog is linear in w = 1 / rhw, the
// interpolated depth DX9 passed: factor = saturate((w - start) * (1 / (end - start))), the color's
// rgb lerped to the fog color, alpha kept. The alpha test runs on the final alpha; blending is
// pipeline state.
//
// Fragment texture and sampler slot 0 (SDL GPU: space2): the texture. Fragment uniform block 0
// (SDL GPU: space3): DX9's g_fogParams and g_fogColor, then the alpha reference / 255.
//
// Color target 1 is the scene's normal target (stdRenderBabylon.h): the view-space normal (x right,
// y up, z toward the viewer) as n * 0.5 + 0.5, alpha 1. The vertices carry no normal yet (M6.9b gives
// them one), so every fragment writes the constant normal facing the viewer, kStd3DConstantNormal.
// Color target 0 is computed exactly as before.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer Std3DFragment : register(b0, space3)
{
    float4 gFogParams; // start, end, 1 / (end - start), enabled (> 0)
    float4 gFogColor;  // r, g, b, a
    float4 gAlphaTest; // x: D3DRS_ALPHAREF / 255; a fragment passes when its alpha is greater
};

// The outputs of std3d.vert.hlsl in its order, without the trailing specular color (see there).
struct Input
{
    [[vk::location(0)]] float4 diffuse : TEXCOORD0;
    [[vk::location(1)]] float2 texCoord : TEXCOORD1;
    [[vk::location(2)]] float4 rhw : TEXCOORD2;
};

// (0, 0, 1) encoded: the normal of every fragment until the vertices carry one.
static const float4 kStd3DConstantNormal = float4(0.5, 0.5, 1.0, 1.0);

struct Output
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

// common.hlsli's ApplyFog.
float4 ApplyFog(float4 color, float depth)
{
    if ( gFogParams.w > 0.0f )
    {
        float fogFactor = (depth - gFogParams.x) * gFogParams.z;
        fogFactor = saturate(fogFactor);
        color.rgb = lerp(color.rgb, gFogColor.rgb, fogFactor);
    }

    return color;
}

Output main(Input input)
{
    float4 texColor   = gTexture.Sample(gSampler, input.texCoord.xy);
    float4 finalColor = texColor * input.diffuse;
    finalColor = ApplyFog(finalColor, input.rhw.x);
    if ( !(finalColor.a > gAlphaTest.x) )
    {
        discard;
    }

    Output output;
    output.color  = finalColor;
    output.normal = kStd3DConstantNormal;
    return output;
}
