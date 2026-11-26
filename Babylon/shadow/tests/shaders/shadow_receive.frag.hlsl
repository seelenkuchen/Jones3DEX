// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: samples a shadow map with a comparison sampler and writes
// white where the reference passes and black where it is blocked.
// gParams.x is the reference depth.

Texture2D<float> gShadowMap : register(t0, space2);
SamplerComparisonState gShadowSampler : register(s0, space2);

cbuffer ShadowReceiveParams : register(b0, space3)
{
    float4 gParams;
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float lit = gShadowMap.SampleCmpLevelZero(gShadowSampler, uv, gParams.x);
    return float4(lit, lit, lit, 1.0);
}
