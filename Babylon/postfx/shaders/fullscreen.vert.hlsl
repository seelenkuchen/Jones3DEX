// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Rule for the fragment shaders drawn with this vertex shader: every one of
// them must read uv (TEXCOORD0). A fragment shader that ignores it loses
// TEXCOORD0 from its input signature, because the compiler drops the unused
// input, and D3D12 then rejects the pipeline state: the pixel shader's inputs
// no longer match this shader's outputs, and CreateGraphicsPipelineState fails
// with "The parameter is incorrect". Read uv even when the pass works in pixel
// coordinates, and derive those from it (uv * size at a pixel centre is
// pixel + 0.5).

// Draws one triangle that covers the target, without a vertex buffer. The
// three vertices are (-1,-1), (3,-1) and (-1,3) in clip space; clipping leaves
// a full-screen quad. The texture coordinate has its origin at the top left,
// matching uploadTexture()'s top-row-first texels.

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
