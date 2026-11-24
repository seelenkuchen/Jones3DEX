// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: passes a clip-space position, with its z as the depth, to a
// depth-only caster. Attributes: location 0 position (float3).

struct Input
{
    [[vk::location(0)]] float3 position : TEXCOORD0;
};

struct Output
{
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position, 1.0);
    return output;
}
