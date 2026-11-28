// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen-space contact shadows, temporal pass: blends the blurred mask with the
// previous frames' mask so any change fades in instead of switching in one
// frame. The previous mask is sampled through the camera motion: the current
// pixel's view position is transformed into the previous frame's view space and
// projected back with the same tangents. Red carries the shadow amount, green
// the linear view depth of that pixel and blue the receiver flag the blur
// wrote, so a pixel that was not a receiver, or whose surface moved in depth,
// rejects the history. A rejected pixel takes the current mask at once; an
// accepted one is a linear blend toward it (gBlend). With no history the pass
// writes the current mask, so the next frame has one.

Texture2D<float4> gMask : register(t0, space2);
Texture2D<float> gDepth : register(t1, space2);
Texture2D<float4> gHistory : register(t2, space2);
// One sampler state per texture: SDL_shadercross's reflection counts a
// sampler shared by several textures as storage textures, which the shader
// build refuses. The pass binds the same linear, clamped sampler to every
// slot, so the history reprojection filters while the current samples land on
// exact pixel centres.
SamplerState gMaskSampler : register(s0, space2);
SamplerState gDepthSampler : register(s1, space2);
SamplerState gHistorySampler : register(s2, space2);

cbuffer ContactShadowTemporalParams : register(b0, space3)
{
    // previous = R * current + t: rows of R in xyz, t in w.
    float4 gCurrentToPrevious0;
    float4 gCurrentToPrevious1;
    float4 gCurrentToPrevious2;
    float gNear;
    float gFar;
    float gDepthNear;
    float gDepthFar;
    float gTanHalfFovX;
    float gTanHalfFovY;
    float gInputWidth;
    float gInputHeight;
    float gBlend;
    uint gHasHistory; // 0 no previous mask, 1 gHistory holds one
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

// The current mask and its view depth, for a pixel that rejects its history.
float4 currentResult(float4 current, float z)
{
    return float4(current.r, z, current.g, 1.0);
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 current = gMask.SampleLevel(gMaskSampler, uv, 0.0);
    float depthTexel = gDepth.SampleLevel(gDepthSampler, uv, 0.0);
    float z = linearViewDepth(depthTexel);

    // Nothing the history could hold: the pixel is not a scene receiver, or
    // there is no previous frame.
    if ( !isSceneDepth(depthTexel) || current.g < 0.5 || gHasHistory == 0u )
    {
        return currentResult(current, z);
    }

    // Reconstruct the view position exactly as the mask shader does, and move
    // it into the previous frame's view space.
    float3 view = float3(
        (uv.x * 2.0 - 1.0) * gTanHalfFovX * z,
        (1.0 - uv.y * 2.0) * gTanHalfFovY * z,
        z);
    float3 previousView = float3(
        dot(gCurrentToPrevious0.xyz, view) + gCurrentToPrevious0.w,
        dot(gCurrentToPrevious1.xyz, view) + gCurrentToPrevious1.w,
        dot(gCurrentToPrevious2.xyz, view) + gCurrentToPrevious2.w);
    if ( previousView.z <= gNear )
    {
        return currentResult(current, z);
    }

    // Project back with the same tangents; the inverse of the reconstruction.
    float2 previousUv = float2(
        0.5 + 0.5 * previousView.x / (gTanHalfFovX * previousView.z),
        0.5 - 0.5 * previousView.y / (gTanHalfFovY * previousView.z));
    if ( previousUv.x < 0.0 || previousUv.x > 1.0
        || previousUv.y < 0.0 || previousUv.y > 1.0 )
    {
        return currentResult(current, z);
    }

    // Reject a history texel that was not a receiver or that belongs to a
    // surface at a different depth: the pixel was disoccluded, so an old shadow
    // must not bleed onto the newly visible surface.
    float4 history = gHistory.SampleLevel(gHistorySampler, previousUv, 0.0);
    if ( history.b < 0.5 || abs(history.g - previousView.z) > 0.02 * previousView.z + 0.002 )
    {
        return currentResult(current, z);
    }

    return float4(lerp(history.r, current.r, gBlend), z, current.g, 1.0);
}
