// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Adds luminance noise that varies with the frame index and is strongest in
// the mid-tones. The hash is the PCG hash (M. E. O'Neill; Jarzynski and
// Olano, "Hash Functions for GPU Rendering", 2020, public domain).

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer FilmGrainParams : register(b0, space3)
{
    uint gFrameIndexLow;
    float gAmplitude;
    float2 gPadding;
};

uint pcg(uint v)
{
    uint s = v * 747796405u + 2891336453u;
    uint w = ((s >> ((s >> 28u) + 4u)) ^ s) * 277803737u;
    return (w >> 22u) ^ w;
}

float4 main(float2 uv : TEXCOORD0, float4 position : SV_Position) : SV_Target0
{
    float4 color = gTexture.Sample(gSampler, uv);
    uint2 px = uint2(position.xy);
    uint h = pcg(px.x + pcg(px.y + pcg(gFrameIndexLow)));
    float n = (float(h) / 4294967295.0) * 2.0 - 1.0;
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    float w = 1.0 - abs(luma * 2.0 - 1.0);
    float3 rgb = saturate(color.rgb + gAmplitude * n * (0.5 + 0.5 * w));
    return float4(rgb, color.a);
}
