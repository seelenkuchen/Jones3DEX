// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen-space contact shadows, mask pass: marches a short ray from each
// receiver toward a directional light through the depth buffer and writes the
// shadow amount to red. A sample blocks when the stored surface is in front of
// the ray point, but not by more than the thickness, so a thin object does not
// cast from a gap behind it. Green is 1 for a receiver and 0 otherwise, so the
// blur can ignore taps that do not receive shadows. Pixels that are not
// receivers write 0 to both channels. An optional ignore mask (red >= 0.5)
// removes a pixel from the effect: a masked receiver writes 0 to both channels,
// and a ray sample that lands on a masked pixel is a gap that never blocks.

Texture2D<float> gDepth : register(t0, space2);
Texture2D<float4> gNormals : register(t1, space2);
Texture2D<float4> gIgnoreMask : register(t2, space2);
// One sampler state per texture: SDL_shadercross's reflection counts a
// sampler shared by several textures as storage textures, which the shader
// build refuses. The pass binds the same nearest, clamped sampler to every
// slot, so the filtering is identical.
SamplerState gDepthSampler : register(s0, space2);
SamplerState gNormalsSampler : register(s1, space2);
SamplerState gIgnoreMaskSampler : register(s2, space2);

cbuffer ContactShadowMaskParams : register(b0, space3)
{
    float3 gLightDirection;
    float gLength;
    float gThickness;
    uint gSteps;
    uint gReceiverClasses;
    float gFadeStart;
    float gFadeEnd;
    float gNear;
    float gFar;
    float gDepthNear;
    float gDepthFar;
    float gTanHalfFovX;
    float gTanHalfFovY;
    float gInputWidth;
    float gInputHeight;
    uint gHasIgnoreMask; // 0 no mask, 1 the extra texture holds one
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

// The Bayer 4x4 ordered matrix, 0..15 in reading order. Its values, divided by
// 16 and shifted by half a step, give the fixed 4x4 jitter tile the blur
// averages exactly. Deterministic, with no time input.
static const uint kBayer4x4[16] = {
    0, 8, 2, 10,
    12, 4, 14, 6,
    3, 11, 1, 9,
    15, 7, 13, 5
};

float orderedJitter(float2 pixel)
{
    uint2 tile = uint2(pixel) % 4u;
    return (float(kBayer4x4[tile.y * 4u + tile.x]) + 0.5) / 16.0;
}

float4 main(float2 uv : TEXCOORD0, float4 position : SV_Position) : SV_Target0
{
    float depth = gDepth.Sample(gDepthSampler, uv);
    if ( !isSceneDepth(depth) )
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // A masked receiver takes no shadow. Writing green 0 also tells the blur
    // that this pixel is not a receiver tap. The flag is tested first so the
    // fallback texture in the slot is never read when there is no mask.
    if ( gHasIgnoreMask != 0u )
    {
        if ( gIgnoreMask.Sample(gIgnoreMaskSampler, uv).r >= 0.5 )
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
    }

    if ( gReceiverClasses != 0xF )
    {
        uint receiverClass = (uint)round(saturate(gNormals.Sample(gNormalsSampler, uv).a) * 3.0);
        if ( ((gReceiverClasses >> receiverClass) & 1u) == 0u )
        {
            return float4(0.0, 0.0, 0.0, 1.0);
        }
    }

    float z = linearViewDepth(depth);
    if ( z <= 0.0 )
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    // Reconstruct the view-space position: x right, y up, z forward.
    float3 receiver = float3(
        (uv.x * 2.0 - 1.0) * gTanHalfFovX * z,
        (1.0 - uv.y * 2.0) * gTanHalfFovY * z,
        z);

    float viewFade = 1.0;
    if ( gFadeEnd > gFadeStart )
    {
        viewFade = 1.0 - smoothstep(gFadeStart, gFadeEnd, z);
    }

    float jitter = orderedJitter(position.xy);
    float shadow = 0.0;

    for ( uint i = 0; i < gSteps; ++i )
    {
        // Start past a self-shadow bias of 0.02 * length, then step toward the light.
        float along = gLength * (0.02 + (float(i) + jitter) / float(gSteps));
        float3 samplePoint = receiver + gLightDirection * along;
        if ( samplePoint.z <= 0.0 )
        {
            break;
        }

        // Project back to the screen; the inverse of the reconstruction above.
        float2 sampleUv = float2(
            0.5 + 0.5 * samplePoint.x / (gTanHalfFovX * samplePoint.z),
            0.5 - 0.5 * samplePoint.y / (gTanHalfFovY * samplePoint.z));
        if ( sampleUv.x < 0.0 || sampleUv.x > 1.0
            || sampleUv.y < 0.0 || sampleUv.y > 1.0 )
        {
            break;
        }

        float2 edge = min(sampleUv, 1.0 - sampleUv);
        float edgeFade = smoothstep(0.0, 0.05, min(edge.x, edge.y));

        // A sample on a masked pixel never blocks: it is a gap in the scene.
        if ( gHasIgnoreMask != 0u )
        {
            if ( gIgnoreMask.Sample(gIgnoreMaskSampler, sampleUv).r >= 0.5 )
            {
                continue;
            }
        }

        float sampleDepth = gDepth.Sample(gDepthSampler, sampleUv);
        if ( !isSceneDepth(sampleDepth) )
        {
            continue;
        }

        float sampleZ = linearViewDepth(sampleDepth);
        float difference = samplePoint.z - sampleZ;
        if ( difference > 0.002 * samplePoint.z && difference <= gThickness )
        {
            // A later blocker still gives half shadow, so the fade along the
            // ray does not hide the shadow at the far end.
            float rayFade = 1.0 - 0.5 * float(i) / float(gSteps);
            shadow = rayFade * edgeFade * viewFade;
            break;
        }
    }

    return float4(shadow, 1.0, 0.0, 1.0);
}
