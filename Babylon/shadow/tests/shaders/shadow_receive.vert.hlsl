// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: one triangle covering the target, with a top-left texture
// coordinate, so the receive pass can look up every shadow-map texel.

struct Output
{
    float2 uv : TEXCOORD0;
    float4 position : SV_Position;
};

Output main(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(3.0, -1.0),
        float2(-1.0, 3.0),
    };

    float2 position = positions[vertexId];
    Output output;
    output.uv = float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5);
    output.position = float4(position, 0.0, 1.0);
    return output;
}
