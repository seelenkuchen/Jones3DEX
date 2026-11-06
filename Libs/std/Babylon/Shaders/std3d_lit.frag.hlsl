// Added: the fragment shader of std3D_DrawRenderListLit (std3DBabylon.cpp): std3d.frag.hlsl with the
// interpolated normal of std3d_lit.vert.hlsl in the normal target instead of the constant normal.
//
// Color target 0 is computed exactly as std3d.frag.hlsl computes it (texture * diffuse, fog, alpha
// test; see there), so a lit draw's color is the default draw's, bit for bit. Color target 1 is the
// normal target (stdRenderBabylon.h): the interpolated view-space normal, normalized again (the
// interpolation of unit vectors shortens them), as n * 0.5 + 0.5, alpha 1. A zero normal (a vertex
// without one) writes the constant normal facing the viewer, as the default shader does.
//
// Fragment texture and sampler slot 0 (SDL GPU: space2) and fragment uniform block 0 (SDL GPU:
// space3) as std3d.frag.hlsl.
//
// std3DBabylon.cpp binds the light list of std3D_SetLights to fragment storage buffer slot 0 of the
// lit draws. Per-pixel lighting (option A L5) declares it here as
//     StructuredBuffer<float4> gLights : register(t1, space2);
// (SDL GPU: after the texture) and reads element 0 as the header (asuint(x) the count) and light i
// in elements 1 + 3i to 3 + 3i as StdLight lays it out (position and minRadius, color, then
// maxRadius, attenuationMin, attenuationMax, asuint flags). It isn't declared yet: the compiler
// drops a buffer nothing reads, and this shader's color must stay the default shader's.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer Std3DFragment : register(b0, space3)
{
    float4 gFogParams; // start, end, 1 / (end - start), enabled (> 0)
    float4 gFogColor;  // r, g, b, a
    float4 gAlphaTest; // x: D3DRS_ALPHAREF / 255; a fragment passes when its alpha is greater
};

// The outputs of std3d_lit.vert.hlsl in its order, without the trailing ones (see there).
struct Input
{
    [[vk::location(0)]] float4 diffuse : TEXCOORD0;
    [[vk::location(1)]] float2 texCoord : TEXCOORD1;
    [[vk::location(2)]] float4 rhw : TEXCOORD2;
    [[vk::location(3)]] float3 normal : TEXCOORD3;
};

// std3d.frag.hlsl's kStd3DConstantNormal: (0, 0, 1) encoded.
static const float4 kStd3DConstantNormal = float4(0.5, 0.5, 1.0, 1.0);

struct Output
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
};

// std3d.frag.hlsl's ApplyFog.
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

float4 EncodeNormal(float3 normal)
{
    float lengthSquared = dot(normal, normal);
    if ( !(lengthSquared > 0.0) )
    {
        return kStd3DConstantNormal;
    }

    return float4(normal * rsqrt(lengthSquared) * 0.5 + 0.5, 1.0);
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
    output.normal = EncodeNormal(input.normal);
    return output;
}
