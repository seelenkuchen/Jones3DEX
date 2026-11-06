// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: samples texture slot 0 and multiplies it by the tint in
// fragment uniform slot 0 (SDL GPU: fragment resources in space2, fragment
// uniform blocks in space3).

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer Tint : register(b0, space3)
{
    float4 gTint;
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    return gTexture.Sample(gSampler, uv) * gTint;
}
