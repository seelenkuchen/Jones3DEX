// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: passes a clip-space position and a vertex color through.
// Attributes: location 0 position (float3), location 1 color (float4).

struct Input
{
    [[vk::location(0)]] float3 position : TEXCOORD0;
    [[vk::location(1)]] float4 color : TEXCOORD1;
};

struct Output
{
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.color = input.color;
    output.position = float4(input.position, 1.0);
    return output;
}
