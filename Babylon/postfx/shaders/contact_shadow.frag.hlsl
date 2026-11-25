// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen-space contact shadows: marches a short ray from each pixel toward a
// directional light through the depth buffer. A sample blocks when the stored
// surface is in front of the ray point, but not by more than the thickness, so
// a thin object does not cast from a gap behind it. The result darkens the
// input colour where the ray is blocked.

Texture2D<float4> gColor : register(t0, space2);
Texture2D<float> gDepth : register(t1, space2);
Texture2D<float4> gNormals : register(t2, space2);
// One sampler state per texture: SDL_shadercross's reflection counts a
// sampler shared by several textures as storage textures, which the shader
// build refuses. The pass binds the same nearest, clamped sampler to every
// slot, so the filtering is identical.
SamplerState gColorSampler : register(s0, space2);
SamplerState gDepthSampler : register(s1, space2);
SamplerState gNormalsSampler : register(s2, space2);

cbuffer ContactShadowParams : register(b0, space3)
{
    float3 gLightDirection;
    float gLength;
    float gThickness;
    float gStrength;
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
    float2 gInputSize;
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

// Interleaved-gradient noise in [0,1): a deterministic hash of the pixel
// position, so the ray start jitter hides banding without any time input.
float pixelJitter(float2 pixel)
{
    return frac(52.9829189 * frac(0.06711056 * pixel.x + 0.00583715 * pixel.y));
}

float4 main(float2 uv : TEXCOORD0, float4 position : SV_Position) : SV_Target0
{
    float4 color = gColor.Sample(gColorSampler, uv);

    float depth = gDepth.Sample(gDepthSampler, uv);
    if ( !isSceneDepth(depth) )
    {
        return color;
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
    float3 receiver = float3(
        (uv.x * 2.0 - 1.0) * gTanHalfFovX * z,
        (1.0 - uv.y * 2.0) * gTanHalfFovY * z,
        z);

    float viewFade = 1.0;
    if ( gFadeEnd > gFadeStart )
    {
        viewFade = 1.0 - smoothstep(gFadeStart, gFadeEnd, z);
    }

    float jitter = pixelJitter(position.xy);
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

        float sampleDepth = gDepth.Sample(gDepthSampler, sampleUv);
        if ( !isSceneDepth(sampleDepth) )
        {
            continue;
        }

        float sampleZ = linearViewDepth(sampleDepth);
        float difference = samplePoint.z - sampleZ;
        if ( difference > 0.002 * samplePoint.z && difference <= gThickness )
        {
            float rayFade = 1.0 - float(i) / float(gSteps);
            shadow = rayFade * edgeFade * viewFade;
            break;
        }
    }

    float3 rgb = color.rgb * (1.0 - gStrength * shadow);
    return float4(rgb, color.a);
}
