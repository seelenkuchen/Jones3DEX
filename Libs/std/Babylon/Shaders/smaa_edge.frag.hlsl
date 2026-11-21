// Added: SMAA 1x luma edge detection, the first pass of stdRenderBabylon's anti-aliasing stage.
// Adapted from SMAA.hlsl of the SMAA reference implementation (https://github.com/iryoku/smaa),
// preset high. It samples the tone-mapped LDR image and writes the horizontal (r) and vertical (g)
// edges; pixels without an edge are discarded, so the target is cleared to zero first.
//
// Copyright (C) 2013 Jorge Jimenez (jorge@iryoku.com)
// Copyright (C) 2013 Jose I. Echevarria (joseignacioechevarria@gmail.com)
// Copyright (C) 2013 Belen Masia (bmasia@unizar.es)
// Copyright (C) 2013 Fernando Navarro (fernandn@microsoft.com)
// Copyright (C) 2013 Diego Gutierrez (diegog@unizar.es)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software. As clarification, there
// is no requirement that the copyright notice and permission be included in
// binary distributions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

Texture2D<float4> gColor : register(t0, space2);
SamplerState gColorSampler : register(s0, space2);

cbuffer Smaa : register(b0, space3)
{
    float4 gMetrics; // x: 1 / width, y: 1 / height, z: width, w: height
};

#define SMAA_RT_METRICS gMetrics
#define SMAA_THRESHOLD 0.1
#define SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR 2.0

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    // The vertex shader's offset outputs, rebuilt here because this pass uses the shared present
    // vertex shader.
    const float4 texel = gMetrics.xyxy;
    float4 offset[3];
    offset[0] = mad(texel, float4(-1.0, 0.0, 0.0, -1.0), uv.xyxy);
    offset[1] = mad(texel, float4( 1.0, 0.0, 0.0,  1.0), uv.xyxy);
    offset[2] = mad(texel, float4(-2.0, 0.0, 0.0, -2.0), uv.xyxy);

    float2 threshold = float2(SMAA_THRESHOLD, SMAA_THRESHOLD);

    float3 weights = float3(0.2126, 0.7152, 0.0722);
    float L = dot(gColor.SampleLevel(gColorSampler, uv, 0).rgb, weights);

    float Lleft = dot(gColor.SampleLevel(gColorSampler, offset[0].xy, 0).rgb, weights);
    float Ltop  = dot(gColor.SampleLevel(gColorSampler, offset[0].zw, 0).rgb, weights);

    float4 delta;
    delta.xy = abs(L - float2(Lleft, Ltop));
    float2 edges = step(threshold, delta.xy);

    if ( dot(edges, float2(1.0, 1.0)) == 0.0 )
    {
        discard;
    }

    float Lright = dot(gColor.SampleLevel(gColorSampler, offset[1].xy, 0).rgb, weights);
    float Lbottom = dot(gColor.SampleLevel(gColorSampler, offset[1].zw, 0).rgb, weights);
    delta.zw = abs(L - float2(Lright, Lbottom));

    float2 maxDelta = max(delta.xy, delta.zw);

    float Lleftleft = dot(gColor.SampleLevel(gColorSampler, offset[2].xy, 0).rgb, weights);
    float Ltoptop   = dot(gColor.SampleLevel(gColorSampler, offset[2].zw, 0).rgb, weights);
    delta.zw = abs(float2(Lleft, Ltop) - float2(Lleftleft, Ltoptop));

    maxDelta = max(maxDelta.xy, delta.zw);
    float finalDelta = max(maxDelta.x, maxDelta.y);

    edges.xy *= step(finalDelta, SMAA_LOCAL_CONTRAST_ADAPTATION_FACTOR * delta.xy);

    return float4(edges, 0.0, 0.0);
}
