// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Passes a clip-space position, a texture coordinate and a straight-alpha
// vertex color to the fragment stage.
// Attributes: location 0 position (float2), location 1 uv (float2),
// location 2 color (UByte4Norm).

struct Input
{
    [[vk::location(0)]] float2 position : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
    [[vk::location(2)]] float4 color : TEXCOORD2;
};

struct Output
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.uv = input.uv;
    output.color = input.color;
    output.position = float4(input.position, 0.0, 1.0);
    return output;
}
