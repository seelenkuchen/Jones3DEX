// Added: SMAA 1x neighbourhood blending, the last pass of stdRenderBabylon's anti-aliasing stage.
// Adapted from SMAA.hlsl of the SMAA reference implementation (https://github.com/iryoku/smaa).
// It combines the tone-mapped LDR image with its neighbours along the detected edges and writes
// the presented frame.
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
Texture2D<float4> gBlend : register(t1, space2);
SamplerState gBlendSampler : register(s1, space2);

cbuffer Smaa : register(b0, space3)
{
    float4 gMetrics; // x: 1 / width, y: 1 / height, z: width, w: height
};

#define SMAA_RT_METRICS gMetrics
#define SMAA_FLATTEN

void SMAAMovc(bool2 cond, inout float2 variable, float2 value)
{
    SMAA_FLATTEN if ( cond.x ) variable.x = value.x;
    SMAA_FLATTEN if ( cond.y ) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value)
{
    SMAA_FLATTEN if ( cond.x ) variable.x = value.x;
    SMAA_FLATTEN if ( cond.y ) variable.y = value.y;
    SMAA_FLATTEN if ( cond.z ) variable.z = value.z;
    SMAA_FLATTEN if ( cond.w ) variable.w = value.w;
}

float4 SampleColor(float2 uv)
{
    return gColor.SampleLevel(gColorSampler, uv, 0);
}

float4 SampleBlend(float2 uv)
{
    return gBlend.SampleLevel(gBlendSampler, uv, 0);
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 offset = mad(SMAA_RT_METRICS.xyxy, float4(1.0, 0.0, 0.0, 1.0), uv.xyxy);

    float4 a;
    a.x = SampleBlend(offset.xy).a;
    a.y = SampleBlend(offset.zw).g;
    a.wz = SampleBlend(uv).xz;

    if ( dot(a, float4(1.0, 1.0, 1.0, 1.0)) < 1e-5 )
    {
        return SampleColor(uv);
    }

    bool h = max(a.x, a.z) > max(a.y, a.w);

    float4 blendingOffset = float4(0.0, a.y, 0.0, a.w);
    float2 blendingWeight = a.yw;
    SMAAMovc(bool4(h, h, h, h), blendingOffset, float4(a.x, 0.0, a.z, 0.0));
    SMAAMovc(bool2(h, h), blendingWeight, a.xz);
    blendingWeight /= dot(blendingWeight, float2(1.0, 1.0));

    float4 blendingCoord = mad(blendingOffset, float4(SMAA_RT_METRICS.xy, -SMAA_RT_METRICS.xy), uv.xyxy);

    float4 color = blendingWeight.x * SampleColor(blendingCoord.xy);
    color += blendingWeight.y * SampleColor(blendingCoord.zw);
    return color;
}
