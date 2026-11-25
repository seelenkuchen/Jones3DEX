// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Copies the source image to the target, clamping HDR values to 0..1. When
// the target is larger or smaller than the source, the sampler scales it.

Texture2D<float4> gTexture : register(t0, space2);
SamplerState gSampler : register(s0, space2);

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    return saturate(gTexture.Sample(gSampler, uv));
}
