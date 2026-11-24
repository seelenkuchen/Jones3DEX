// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: samples one chosen layer of a shadow-map array with a
// comparison sampler and writes white where the comparison passes and black
// where it fails. Fragment resources in space2, uniform blocks in space3 as
// SDL GPU expects. gShadowParams.x is the layer, .y the reference depth.

Texture2DArray<float> gShadow : register(t0, space2);
SamplerComparisonState gShadowSampler : register(s0, space2);

cbuffer ShadowParams : register(b0, space3)
{
    float4 gShadowParams;
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float lit = gShadow.SampleCmpLevelZero(gShadowSampler, float3(uv, gShadowParams.x),
        gShadowParams.y);
    return float4(lit, lit, lit, 1.0);
}
