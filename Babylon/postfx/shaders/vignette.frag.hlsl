// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Darkens the image towards the corners. The radius is measured in a
// normalized space whose corner has radius 1 at every aspect ratio.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer VignetteParams : register(b0, space3)
{
    float gStrength;
    float gInner;
    float gOuter;
    float gAspect;
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float4 color = gTexture.Sample(gSampler, uv);
    float2 d = (uv - 0.5) * float2(gAspect, 1.0) * 2.0 / length(float2(gAspect, 1.0));
    float r = length(d);
    float f = 1.0 - gStrength * smoothstep(gInner, gOuter, r);
    return float4(color.rgb * f, color.a);
}
