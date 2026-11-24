// Added: the fragment shader of std3D_DrawRenderListLit (std3DBabylon.cpp): std3d.frag.hlsl with the
// interpolated normal of std3d_lit.vert.hlsl in the normal target instead of the constant normal.
//
// Color target 0 is computed exactly as std3d.frag.hlsl computes it (texture * diffuse, fog, alpha
// test; see there), so a lit draw's color is the default draw's, bit for bit. Color target 1 is the
// normal target (stdRenderBabylon.h): the interpolated view-space normal, normalized again (the
// interpolation of unit vectors shortens them), as n * 0.5 + 0.5, with the render state's surface
// tag (added M9.16) in alpha. A zero normal (a vertex
// without one) writes the constant normal facing the viewer, as the default shader does.
//
// Added: M10.2 per-pixel dynamic lighting. The backend binds the light list of std3D_SetLights to
// fragment storage buffer slot 0: element 0 is the header (asuint(x) the count), light i is in
// elements 1 + 3i to 3 + 3i as StdLight lays it out (position and minRadius, color, then maxRadius,
// attenuationMin, attenuationMax, asuint flags). A lit vertex's unused specular blue byte tells
// whether its face has lights to add (0xFF for a resolved GOURAUD or DIFFUSE face, 0 for LIT/NONE
// and for every Classic vertex). The camera list is in rdroid's camera space, the same space the
// vertex carries; both are turned into the normal target's space before the dot product. The
// formula is rdLight_CalcVertexIntensities', which every per-pixel surface uses: enabled lights
// only, nothing at or beyond minRadius or when N.L <= 0, color * N.L within maxRadius and a linear
// ramp to zero at minRadius. The sum is multiplied by the screen fade factor in gSurfaceTag.y and
// added to the base diffuse after the CPU's 0..1 fade; the texture multiplies the saturated sum.
// Fog stays after, as before. Classic vertices skip the sum, so their pixels are unchanged.
//
// Fragment texture and sampler slot 0 (SDL GPU: space2) and fragment uniform block 0 (SDL GPU:
// space3) as std3d.frag.hlsl.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);
StructuredBuffer<float4> gLights : register(t1, space2);

cbuffer Std3DFragment : register(b0, space3)
{
    float4 gFogParams; // start, end, 1 / (end - start), enabled (> 0)
    float4 gFogColor;  // r, g, b, a
    float4 gAlphaTest; // x: D3DRS_ALPHAREF / 255; a fragment passes when its alpha is greater
    float4 gSurfaceTag; // Added: x is the normal target's alpha tag (M9.16); y the screen fade factor (M10.2)
};

// The outputs of std3d_lit.vert.hlsl in its order, without the trailing ones (see there).
struct Input
{
    [[vk::location(0)]] float4 diffuse : TEXCOORD0;
    [[vk::location(1)]] float2 texCoord : TEXCOORD1;
    [[vk::location(2)]] float4 rhw : TEXCOORD2;
    [[vk::location(3)]] float3 normal : TEXCOORD3;
    [[vk::location(4)]] float3 viewPosition : TEXCOORD4;
    [[vk::location(5)]] float4 specular : TEXCOORD5; // blue byte: add the dynamic lights per pixel
};

// std3d.frag.hlsl's constant normal: (0, 0, 1) encoded, with the render state's surface tag (M9.16).
float4 ConstantNormal()
{
    return float4(0.5, 0.5, 1.0, gSurfaceTag.x);
}

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

// rdroid's camera space (x right, y forward, z up) to the normal target's (x right, y up, z toward
// the viewer), as std3d_lit.vert.hlsl does for the vertex position and normal.
float3 ToTargetSpace(float3 v)
{
    return float3(v.x, v.z, -v.y);
}

// rdLight_CalcVertexIntensities evaluated at this fragment, over the uploaded camera list.
float3 DynamicLight(float3 viewPos, float3 normal)
{
    uint numLights = asuint(gLights[0].x);
    float3 sum = float3(0.0, 0.0, 0.0);

    for ( uint i = 0u; i < numLights; ++i )
    {
        float4 position = gLights[1u + 3u * i]; // xyz view-space position, w minRadius
        float4 color    = gLights[2u + 3u * i]; // rgb colour
        float4 params   = gLights[3u + 3u * i]; // x maxRadius, y attenuationMin, z attenuationMax, w flags

        if ( (asuint(params.w) & 1u) == 0u ) // STD_LIGHT_ENABLED
        {
            continue;
        }

        float3 lightDir = ToTargetSpace(position.xyz) - viewPos;
        float dist = length(lightDir);
        if ( dist >= position.w ) // nothing at or beyond minRadius
        {
            continue;
        }

        float dotNL = dot(normal, lightDir / dist);
        if ( !(dotNL > 0.0f) )
        {
            continue;
        }

        float ramp;
        if ( dist < params.x ) // full colour within maxRadius
        {
            ramp = 1.0f;
        }
        else
        {
            ramp = (position.w - dist) / (position.w - params.x); // linear to zero at minRadius
        }

        sum += color.rgb * (dotNL * ramp);
    }

    return sum;
}

float4 EncodeNormal(float3 normal)
{
    float lengthSquared = dot(normal, normal);
    if ( !(lengthSquared > 0.0) )
    {
        return ConstantNormal();
    }

    return float4(normal * rsqrt(lengthSquared) * 0.5 + 0.5, gSurfaceTag.x);
}

Output main(Input input)
{
    float4 texColor = gTexture.Sample(gSampler, input.texCoord.xy);

    float3 normal = input.normal;
    float normalLengthSquared = dot(normal, normal);

    // Only a per-pixel lit vertex asks for lights; Classic vertices and LIT/NONE faces carry 0.
    if ( input.specular.x > 0.5f && normalLengthSquared > 0.0f )
    {
        float3 dynamic = DynamicLight(input.viewPosition, normal * rsqrt(normalLengthSquared));
        float3 base = saturate(input.diffuse.rgb + dynamic * gSurfaceTag.y);
        float4 finalColor = float4(texColor.rgb * base, texColor.a * input.diffuse.a);
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
