// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen-space contact shadows, blur pass: a depth-aware 4x4 box blur of the
// mask. The four taps per axis sit at -1.5, -0.5, +0.5 and +1.5 texels, so the
// 4x4 block holds every index modulo 4 of the mask's ordered jitter tile
// exactly. A tap is weighted by how close its linear view depth is to the
// centre's; taps that are not receivers or not scene depth weigh nothing, so a
// tagged non-receiver neighbour cannot lighten a real shadow. When no tap
// carries weight the centre mask value passes through.

Texture2D<float4> gMask : register(t0, space2);
Texture2D<float> gDepth : register(t1, space2);
// One sampler state per texture: SDL_shadercross's reflection counts a
// sampler shared by several textures as storage textures, which the shader
// build refuses. The pass binds the same nearest, clamped sampler to every
// slot, so the filtering is identical.
SamplerState gMaskSampler : register(s0, space2);
SamplerState gDepthSampler : register(s1, space2);

cbuffer ContactShadowBlurParams : register(b0, space3)
{
    float gNear;
    float gFar;
    float gDepthNear;
    float gDepthFar;
    float gInputWidth;
    float gInputHeight;
    float2 gPadding;
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

static const float kTapOffsets[4] = { -1.5, -0.5, 0.5, 1.5 };

float4 main(float2 uv : TEXCOORD0, float4 position : SV_Position) : SV_Target0
{
    float2 texel = 1.0 / float2(gInputWidth, gInputHeight);
    float2 centre = floor(position.xy);
    float2 centreUv = (centre + 0.5) * texel;

    float4 centreMask = gMask.Sample(gMaskSampler, centreUv);
    if ( centreMask.g <= 0.5 )
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float centreDepth = gDepth.Sample(gDepthSampler, centreUv);
    if ( !isSceneDepth(centreDepth) )
    {
        return float4(centreMask.r, 0.0, 0.0, 1.0);
    }

    float zCentre = linearViewDepth(centreDepth);
    if ( zCentre <= 0.0 )
    {
        return float4(centreMask.r, 0.0, 0.0, 1.0);
    }

    float weighted = 0.0;
    float totalWeight = 0.0;
    [unroll] for ( uint y = 0; y < 4; ++y )
    {
        [unroll] for ( uint x = 0; x < 4; ++x )
        {
            float2 tap = floor(position.xy + float2(kTapOffsets[x], kTapOffsets[y]));
            float2 tapUv = (tap + 0.5) * texel;

            float4 tapMask = gMask.Sample(gMaskSampler, tapUv);
            if ( tapMask.g <= 0.5 )
            {
                continue;
            }

            float tapDepth = gDepth.Sample(gDepthSampler, tapUv);
            if ( !isSceneDepth(tapDepth) )
            {
                continue;
            }

            float zTap = linearViewDepth(tapDepth);
            float weight = max(0.0, 1.0 - abs(zTap - zCentre) / (0.05 * zCentre));
            weighted += weight * tapMask.r;
            totalWeight += weight;
        }
    }

    float mask = totalWeight > 0.0 ? weighted / totalWeight : centreMask.r;
    return float4(mask, 0.0, 0.0, 1.0);
}
