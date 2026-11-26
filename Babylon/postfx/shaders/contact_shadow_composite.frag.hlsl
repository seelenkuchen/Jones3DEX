// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Screen-space contact shadows, composite pass: darkens the input colour where
// the blurred mask says the ray was blocked. It repeats the mask pass's
// receiver test, so a non-receiver pixel returns the input untouched even when
// the blur next to it carries a shadow. A zero mask also returns the input
// untouched, so the copy stays byte-exact. An ignore-masked pixel is a
// non-receiver whose blur value is zero, so it keeps its exact input colour
// here without this pass needing the mask texture.

Texture2D<float> gDepth : register(t0, space2);
Texture2D<float4> gNormals : register(t1, space2);
Texture2D<float4> gColor : register(t2, space2);
Texture2D<float4> gMask : register(t3, space2);
// One sampler state per texture: SDL_shadercross's reflection counts a
// sampler shared by several textures as storage textures, which the shader
// build refuses. The pass binds the same nearest, clamped sampler to every
// slot, so the filtering is identical.
SamplerState gDepthSampler : register(s0, space2);
SamplerState gNormalsSampler : register(s1, space2);
SamplerState gColorSampler : register(s2, space2);
SamplerState gMaskSampler : register(s3, space2);

cbuffer ContactShadowCompositeParams : register(b0, space3)
{
    float gStrength;
    uint gReceiverClasses;
    float gDepthNear;
    float gDepthFar;
};

// True when the texel names a scene surface. Clear depth is exactly gDepthFar
// and must not count; DepthMath.h uses the same far-plane epsilon.
bool isSceneDepth(float depthTexel)
{
    return depthTexel >= gDepthNear && depthTexel < gDepthFar - 1.0e-6;
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
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

    float mask = gMask.Sample(gMaskSampler, uv).r;
    if ( mask <= 0.0 )
    {
        return color;
    }

    float3 rgb = color.rgb * (1.0 - gStrength * mask);
    return float4(rgb, color.a);
}
