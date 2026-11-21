// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Samples the painter's texture (white for fills, an image or a font atlas
// otherwise) and tints it by the interpolated vertex color.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0, float4 color : TEXCOORD1) : SV_Target0
{
    return gTexture.Sample(gSampler, uv) * color;
}
