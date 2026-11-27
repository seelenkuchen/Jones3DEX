// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Shadow receive: reconstructs the view-space position of each receiver from
// depth, projects it into a directional shadow map and darkens the colour
// where the map's comparison sampler says the light is blocked. A receiver
// test repeats the contact-shadow composite's, so a non-receiver pixel returns
// the input byte for byte. An optional mask either excludes selected pixels
// (a caster's own surface) or, in include mode, confines the receive pass to
// them. The non-selected pixels of either mode return the input byte for byte.
//
// The fragment shader must read uv (TEXCOORD0): a shader that only uses
// SV_Position loses TEXCOORD0 from its input signature, and D3D12 rejects the
// pipeline. Every sample below is taken at uv, so the input stays used.

Texture2D<float4> gColor : register(t0, space2);
Texture2D<float> gDepth : register(t1, space2);
Texture2D<float4> gNormals : register(t2, space2);
Texture2D<float> gShadowMap : register(t3, space2);
Texture2D<float4> gExcludeMask : register(t4, space2);
// One sampler state per texture: SDL_shadercross's reflection counts a
// sampler shared by several textures as storage textures, which the shader
// build refuses. The colour, depth, normals and exclude-mask slots use the
// same nearest, clamped sampler; the shadow map needs its comparison sampler.
SamplerState gColorSampler : register(s0, space2);
SamplerState gDepthSampler : register(s1, space2);
SamplerState gNormalsSampler : register(s2, space2);
SamplerComparisonState gShadowSampler : register(s3, space2);
SamplerState gExcludeMaskSampler : register(s4, space2);

cbuffer ShadowReceiveParams : register(b0, space3)
{
    // View space -> (u, v, depth, 1), row-major like the caller's float[16].
    row_major float4x4 gViewToShadowTexture;
    float3 gTowardLight;
    float gStrength;
    float gNormalOffset;
    float gDepthBias;
    float gFilterRadius;
    float gMapSize;
    uint gReceiverClasses;
    float gFadeStart;
    float gFadeEnd;
    float gNear;
    float gFar;
    float gDepthNear;
    float gDepthFar;
    float gTanHalfFovX;
    float gTanHalfFovY;
    uint gMaskMode; // 0 none, 1 exclude, 2 include
    float gPenumbra;            // view units: minimum filter radius
    float gPenumbraPerDistance; // view units of radius per view unit behind the map's far side
    float gDistanceFadeStart;   // view units behind the map's far side
    float gDistanceFadeEnd;     // end <= start disables the distance fade
    float gMinLight;            // input-colour luminance below which no shadow applies
    float gFullLight;           // fullLight <= minLight disables the luminance gate
    float gPadding1;
    float gPadding2;
    float gPadding3;
    float gPadding4;
};

// A fixed 12-tap Poisson disc, offsets in texels.
static const float2 kPoissonDisc[12] = {
    float2(-0.326, -0.406),
    float2(-0.840, -0.074),
    float2(-0.696,  0.457),
    float2(-0.203,  0.621),
    float2( 0.962, -0.195),
    float2( 0.473, -0.480),
    float2( 0.519,  0.767),
    float2( 0.185, -0.893),
    float2( 0.507,  0.064),
    float2( 0.896,  0.412),
    float2(-0.322, -0.933),
    float2(-0.792, -0.598),
};

// The positive view-space distance along the view axis of a depth texel.
// Depth is affine in 1/z between depthNear (at the near plane) and depthFar
// (at the far plane):
//
//   t = (depthTexel - depthNear) / (depthFar - depthNear)
//   z = 1 / (1 / near + t * (1 / far - 1 / near))
//
// postfx::linearViewDepth() in DepthMath.h uses the same formula.
float linearViewDepth(float depthTexel)
{
    float t = (depthTexel - gDepthNear) / (gDepthFar - gDepthNear);
    return 1.0 / (1.0 / gNear + t * (1.0 / gFar - 1.0 / gNear));
}

// True when the texel names a scene surface. Clear depth is exactly gDepthFar
// and must not count; DepthMath.h uses the same far-plane epsilon.
bool isSceneDepth(float depthTexel)
{
    return depthTexel >= gDepthNear && depthTexel < gDepthFar - 1.0e-6;
}

// A deterministic 4x4 interleaved angle from the screen pixel. Larger filter
// radii with a fixed disc would show its 12 taps as bands; rotating the disc
// per 4x4 tile breaks them up without a random texture or time input.
float interleavedAngle(float2 pixel)
{
    uint2 tile = uint2(pixel) % 4u;
    return (float(tile.y * 4u + tile.x) + 0.5) * (6.2831853 / 16.0);
}

float4 main(float2 uv : TEXCOORD0, float4 screenPosition : SV_Position) : SV_Target0
{
    float4 color = gColor.Sample(gColorSampler, uv);

    float depth = gDepth.Sample(gDepthSampler, uv);
    if ( !isSceneDepth(depth) )
    {
        return color;
    }

    // In exclude mode a masked pixel is a shadow caster's own surface: it
    // keeps its exact input colour, so the caster still casts but does not
    // receive. In include mode only a masked pixel may receive; every other
    // pixel keeps its exact input colour.
    if ( gMaskMode != 0 )
    {
        const bool selected = gExcludeMask.Sample(gExcludeMaskSampler, uv).r >= 0.5;
        if ( gMaskMode == 1 ? selected : !selected )
        {
            return color;
        }
    }

    if ( gReceiverClasses != 0xF )
    {
        uint receiverClass = (uint)round(saturate(gNormals.Sample(gNormalsSampler, uv).a) * 3.0);
        if ( ((gReceiverClasses >> receiverClass) & 1u) == 0u )
        {
            return color;
        }
    }

    float z = linearViewDepth(depth);
    if ( z <= 0.0 )
    {
        return color;
    }

    // Reconstruct the view-space position: x right, y up, z forward.
    float3 position = float3(
        (uv.x * 2.0 - 1.0) * gTanHalfFovX * z,
        (1.0 - uv.y * 2.0) * gTanHalfFovY * z,
        z);

    // Push the receiver toward the light, more with distance, to keep its own
    // surface out of the comparison.
    float3 towardLight = gTowardLight;
    float lightLength = length(towardLight);
    if ( lightLength > 0.0 )
    {
        towardLight /= lightLength;
    }
    float offsetScale = clamp(z / 10.0, 0.5, 4.0);
    float3 biasedPosition = position + towardLight * (gNormalOffset * offsetScale);

    float3 shadowCoord = mul(gViewToShadowTexture, float4(biasedPosition, 1.0)).xyz;

    // Outside the map's footprint there is no occluder information, so the
    // pixel is lit. A receiver nearer the light than the whole map (z < 0) is
    // in front of every caster and also lit.
    if ( shadowCoord.x < 0.0 || shadowCoord.x > 1.0
        || shadowCoord.y < 0.0 || shadowCoord.y > 1.0
        || shadowCoord.z < 0.0 )
    {
        return color;
    }

    // The light fit covers the casters only, so a receiver behind all of them
    // can be past the far plane (z > 1). It is still behind every caster and
    // must be compared; clamping to the cleared far value keeps a receiver
    // outside the casters lit while caster texels (stored < 1) shadow it.
    // A comparison sample is 1 where the receiver depth passes (LessOrEqual),
    // which means lit; average it over the disc for PCF.
    float reference = min(shadowCoord.z, 1.0) - gDepthBias;

    // The receiver's distance behind the map's far side, in view units. The
    // unclamped receiver depth gives this gap even where the reference above
    // is clamped, so a receiver behind every caster can widen and fade.
    float uvPerUnit = length(gViewToShadowTexture[0].xyz);
    float depthRange = 1.0 / max(length(gViewToShadowTexture[2].xyz), 1.0e-6);
    float gap = max(shadowCoord.z - 1.0, 0.0) * depthRange;

    float visibility = 0.0;
    if ( gPenumbra > 0.0 || gPenumbraPerDistance > 0.0 )
    {
        // A view-unit radius grows with the distance behind the map; clamp the
        // uv radius so a broken matrix cannot sample across the whole map.
        float radiusUv = max(gFilterRadius / gMapSize,
            (gPenumbra + gPenumbraPerDistance * gap) * uvPerUnit);
        radiusUv = min(radiusUv, 0.05);

        float angle = interleavedAngle(screenPosition.xy);
        float sine = sin(angle);
        float cosine = cos(angle);
        float2x2 rotation = float2x2(cosine, -sine, sine, cosine);
        for ( uint i = 0; i < 12; ++i )
        {
            visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler,
                shadowCoord.xy + mul(rotation, kPoissonDisc[i]) * radiusUv, reference);
        }
    }
    else
    {
        // The default path keeps the original texel-radius taps byte for byte,
        // including their order and offsets.
        float2 texel = gFilterRadius / gMapSize;
        for ( uint i = 0; i < 12; ++i )
        {
            visibility += gShadowMap.SampleCmpLevelZero(gShadowSampler,
                shadowCoord.xy + kPoissonDisc[i] * texel, reference);
        }
    }
    visibility *= 1.0 / 12.0;

    float shadow = 1.0 - visibility;

    // Fade shadows out with view distance and near the map border.
    float viewFade = 1.0;
    if ( gFadeEnd > gFadeStart )
    {
        viewFade = 1.0 - smoothstep(gFadeStart, gFadeEnd, z);
    }
    float edge = min(min(shadowCoord.x, 1.0 - shadowCoord.x),
        min(shadowCoord.y, 1.0 - shadowCoord.y));
    float borderFade = smoothstep(0.0, 0.05, edge);

    // Fade a receiver farther behind the casters and scale the shadow by the
    // input colour's luminance: a dim, torch-lit surface takes no hard sun
    // shadow. Both gates are off when their two values are equal (0 by default).
    float distanceFade = 1.0;
    if ( gDistanceFadeEnd > gDistanceFadeStart )
    {
        distanceFade = 1.0 - smoothstep(gDistanceFadeStart, gDistanceFadeEnd, gap);
    }
    float lightScale = 1.0;
    if ( gFullLight > gMinLight )
    {
        float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
        lightScale = smoothstep(gMinLight, gFullLight, luma);
    }

    shadow *= viewFade * borderFade * distanceFade * lightScale;

    float3 rgb = color.rgb * (1.0 - gStrength * shadow);
    return float4(rgb, color.a);
}
