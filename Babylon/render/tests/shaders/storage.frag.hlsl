// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: samples texture slot 0 and multiplies it by element 1 of the
// storage buffer in fragment storage buffer slot 0. In SDL GPU's convention
// the storage buffer follows the stage's one texture: register t1, space2.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

StructuredBuffer<float4> gData : register(t1, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    return gTexture.Sample(gSampler, uv) * gData[1];
}
