// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Depth-only caster: transforms a view-space position into the directional
// shadow map's clip space. The uniform is row-major, matching shadow::Matrix4
// (m[row * 4 + column]); mul(M, v) applies it to the column vector.

struct Input
{
    [[vk::location(0)]] float3 position : TEXCOORD0;
};

struct Output
{
    float4 position : SV_Position;
};

cbuffer ShadowCasterTransform : register(b0, space1)
{
    row_major float4x4 gViewToShadowClip;
};

Output main(Input input)
{
    Output output;
    output.position = mul(gViewToShadowClip, float4(input.position, 1.0));
    return output;
}
