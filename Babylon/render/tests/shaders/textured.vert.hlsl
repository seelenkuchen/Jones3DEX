// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: passes a clip-space position and a texture coordinate through.
// Attributes: location 0 position (float2), location 1 uv (float2).

struct Input
{
    [[vk::location(0)]] float2 position : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
};

struct Output
{
    float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.uv = input.uv;
    output.position = float4(input.position, 0.0, 1.0);
    return output;
}
