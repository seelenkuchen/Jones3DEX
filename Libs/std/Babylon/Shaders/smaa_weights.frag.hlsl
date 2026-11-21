// Added: SMAA 1x blending weight calculation, the second pass of stdRenderBabylon's anti-aliasing
// stage. Adapted from SMAA.hlsl of the SMAA reference implementation
// (https://github.com/iryoku/smaa), preset high, with the area and search textures from that
// project. It searches the edges texture and writes the horizontal and vertical blend weights.
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

Texture2D<float4> gEdges : register(t0, space2);
SamplerState gEdgesSampler : register(s0, space2);
Texture2D<float4> gArea : register(t1, space2);
SamplerState gAreaSampler : register(s1, space2);
Texture2D<float4> gSearch : register(t2, space2);
SamplerState gSearchSampler : register(s2, space2);

cbuffer Smaa : register(b0, space3)
{
    float4 gMetrics; // x: 1 / width, y: 1 / height, z: width, w: height
};

#define SMAA_RT_METRICS gMetrics
#define SMAA_MAX_SEARCH_STEPS 16
#define SMAA_MAX_SEARCH_STEPS_DIAG 8
#define SMAA_CORNER_ROUNDING 25
#define SMAA_AREATEX_MAX_DISTANCE 16
#define SMAA_AREATEX_MAX_DISTANCE_DIAG 20
#define SMAA_AREATEX_PIXEL_SIZE (1.0 / float2(160.0, 560.0))
#define SMAA_AREATEX_SUBTEX_SIZE (1.0 / 7.0)
#define SMAA_SEARCHTEX_SIZE float2(66.0, 33.0)
#define SMAA_SEARCHTEX_PACKED_SIZE float2(64.0, 16.0)
#define SMAA_CORNER_ROUNDING_NORM (float(SMAA_CORNER_ROUNDING) / 100.0)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define SMAA_AREATEX_SELECT(sample) sample.rg
#define SMAA_SEARCHTEX_SELECT(sample) sample.r

float4 SampleEdges(float2 uv)
{
    return gEdges.SampleLevel(gEdgesSampler, uv, 0);
}

float4 SampleEdgesOffset(float2 uv, int2 offset)
{
    return gEdges.SampleLevel(gEdgesSampler, uv, 0, offset);
}

float4 SampleArea(float2 uv)
{
    return gArea.SampleLevel(gAreaSampler, uv, 0);
}

float4 SampleSearch(float2 uv)
{
    return gSearch.SampleLevel(gSearchSampler, uv, 0);
}

void SMAAMovc(bool2 cond, inout float2 variable, float2 value)
{
    SMAA_FLATTEN if ( cond.x ) variable.x = value.x;
    SMAA_FLATTEN if ( cond.y ) variable.y = value.y;
}

void SMAAMovc(bool4 cond, inout float4 variable, float4 value)
{
    SMAAMovc(cond.xy, variable.xy, value.xy);
    SMAAMovc(cond.zw, variable.zw, value.zw);
}

float2 SMAADecodeDiagBilinearAccess(float2 e)
{
    e.r = e.r * abs(5.0 * e.r - 5.0 * 0.75);
    return round(e);
}

float4 SMAADecodeDiagBilinearAccess(float4 e)
{
    e.rb = e.rb * abs(5.0 * e.rb - 5.0 * 0.75);
    return round(e);
}

float2 SMAASearchDiag1(float2 texcoord, float2 dir, out float2 e)
{
    float4 coord = float4(texcoord, -1.0, 1.0);
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    while ( coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9 )
    {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = SampleEdges(coord.xy).rg;
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAASearchDiag2(float2 texcoord, float2 dir, out float2 e)
{
    float4 coord = float4(texcoord, -1.0, 1.0);
    coord.x += 0.25 * SMAA_RT_METRICS.x;
    float3 t = float3(SMAA_RT_METRICS.xy, 1.0);
    while ( coord.z < float(SMAA_MAX_SEARCH_STEPS_DIAG - 1) && coord.w > 0.9 )
    {
        coord.xyz = mad(t, float3(dir, 1.0), coord.xyz);
        e = SampleEdges(coord.xy).rg;
        e = SMAADecodeDiagBilinearAccess(e);
        coord.w = dot(e, float2(0.5, 0.5));
    }
    return coord.zw;
}

float2 SMAAAreaDiag(float2 dist, float2 e, float offset)
{
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE_DIAG, SMAA_AREATEX_MAX_DISTANCE_DIAG), e, dist);

    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    texcoord.x += 0.5;
    texcoord.y += SMAA_AREATEX_SUBTEX_SIZE * offset;

    return SMAA_AREATEX_SELECT(SampleArea(texcoord));
}

float2 SMAACalculateDiagWeights(float2 texcoord, float2 e, float4 subsampleIndices)
{
    float2 weights = float2(0.0, 0.0);

    float4 d;
    float2 end;
    if ( e.r > 0.0 )
    {
        d.xz = SMAASearchDiag1(texcoord, float2(-1.0, 1.0), end);
        d.x += float(end.y > 0.9);
    }
    else
    {
        d.xz = float2(0.0, 0.0);
    }
    d.yw = SMAASearchDiag1(texcoord, float2(1.0, -1.0), end);

    SMAA_BRANCH
    if ( d.x + d.y > 2.0 )
    {
        float4 coords = mad(float4(-d.x + 0.25, d.x, d.y, -d.y - 0.25), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        float4 c;
        c.xy = SampleEdgesOffset(coords.xy, int2(-1, 0)).rg;
        c.zw = SampleEdgesOffset(coords.zw, int2( 1, 0)).rg;
        c.yxwz = SMAADecodeDiagBilinearAccess(c.xyzw);

        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        weights += SMAAAreaDiag(d.xy, cc, subsampleIndices.z);
    }

    d.xz = SMAASearchDiag2(texcoord, float2(-1.0, -1.0), end);
    if ( SampleEdgesOffset(texcoord, int2(1, 0)).r > 0.0 )
    {
        d.yw = SMAASearchDiag2(texcoord, float2(1.0, 1.0), end);
        d.y += float(end.y > 0.9);
    }
    else
    {
        d.yw = float2(0.0, 0.0);
    }

    SMAA_BRANCH
    if ( d.x + d.y > 2.0 )
    {
        float4 coords = mad(float4(-d.x, -d.x, d.y, d.y), SMAA_RT_METRICS.xyxy, texcoord.xyxy);
        float4 c;
        c.x  = SampleEdgesOffset(coords.xy, int2(-1, 0)).g;
        c.y  = SampleEdgesOffset(coords.xy, int2( 0, -1)).r;
        c.zw = SampleEdgesOffset(coords.zw, int2( 1, 0)).gr;
        float2 cc = mad(float2(2.0, 2.0), c.xz, c.yw);

        SMAAMovc(bool2(step(0.9, d.zw)), cc, float2(0.0, 0.0));

        weights += SMAAAreaDiag(d.xy, cc, subsampleIndices.w).gr;
    }

    return weights;
}

float SMAASearchLength(float2 e, float offset)
{
    float2 scale = SMAA_SEARCHTEX_SIZE * float2(0.5, -1.0);
    float2 bias = SMAA_SEARCHTEX_SIZE * float2(offset, 1.0);

    scale += float2(-1.0, 1.0);
    bias  += float2( 0.5, -0.5);

    scale *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;
    bias *= 1.0 / SMAA_SEARCHTEX_PACKED_SIZE;

    return SMAA_SEARCHTEX_SELECT(SampleSearch(mad(scale, e, bias)));
}

float SMAASearchXLeft(float2 texcoord, float end)
{
    float2 e = float2(0.0, 1.0);
    while ( texcoord.x > end && e.g > 0.8281 && e.r == 0.0 )
    {
        e = SampleEdges(texcoord).rg;
        texcoord = mad(-float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }

    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchXRight(float2 texcoord, float end)
{
    float2 e = float2(0.0, 1.0);
    while ( texcoord.x < end && e.g > 0.8281 && e.r == 0.0 )
    {
        e = SampleEdges(texcoord).rg;
        texcoord = mad(float2(2.0, 0.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.x, offset, texcoord.x);
}

float SMAASearchYUp(float2 texcoord, float end)
{
    float2 e = float2(1.0, 0.0);
    while ( texcoord.y > end && e.r > 0.8281 && e.g == 0.0 )
    {
        e = SampleEdges(texcoord).rg;
        texcoord = mad(-float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.0), 3.25);
    return mad(SMAA_RT_METRICS.y, offset, texcoord.y);
}

float SMAASearchYDown(float2 texcoord, float end)
{
    float2 e = float2(1.0, 0.0);
    while ( texcoord.y < end && e.r > 0.8281 && e.g == 0.0 )
    {
        e = SampleEdges(texcoord).rg;
        texcoord = mad(float2(0.0, 2.0), SMAA_RT_METRICS.xy, texcoord);
    }
    float offset = mad(-(255.0 / 127.0), SMAASearchLength(e.gr, 0.5), 3.25);
    return mad(-SMAA_RT_METRICS.y, offset, texcoord.y);
}

float2 SMAAArea(float2 dist, float e1, float e2, float offset)
{
    float2 texcoord = mad(float2(SMAA_AREATEX_MAX_DISTANCE, SMAA_AREATEX_MAX_DISTANCE), round(4.0 * float2(e1, e2)), dist);

    texcoord = mad(SMAA_AREATEX_PIXEL_SIZE, texcoord, 0.5 * SMAA_AREATEX_PIXEL_SIZE);

    texcoord.y = mad(SMAA_AREATEX_SUBTEX_SIZE, offset, texcoord.y);

    return SMAA_AREATEX_SELECT(SampleArea(texcoord));
}

void SMAADetectHorizontalCornerPattern(inout float2 weights, float4 texcoord, float2 d)
{
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SampleEdgesOffset(texcoord.xy, int2(0,  1)).r;
    factor.x -= rounding.y * SampleEdgesOffset(texcoord.zw, int2(1,  1)).r;
    factor.y -= rounding.x * SampleEdgesOffset(texcoord.xy, int2(0, -2)).r;
    factor.y -= rounding.y * SampleEdgesOffset(texcoord.zw, int2(1, -2)).r;

    weights *= saturate(factor);
}

void SMAADetectVerticalCornerPattern(inout float2 weights, float4 texcoord, float2 d)
{
    float2 leftRight = step(d.xy, d.yx);
    float2 rounding = (1.0 - SMAA_CORNER_ROUNDING_NORM) * leftRight;

    rounding /= leftRight.x + leftRight.y;

    float2 factor = float2(1.0, 1.0);
    factor.x -= rounding.x * SampleEdgesOffset(texcoord.xy, int2( 1, 0)).g;
    factor.x -= rounding.y * SampleEdgesOffset(texcoord.zw, int2( 1, 1)).g;
    factor.y -= rounding.x * SampleEdgesOffset(texcoord.xy, int2(-2, 0)).g;
    factor.y -= rounding.y * SampleEdgesOffset(texcoord.zw, int2(-2, 1)).g;

    weights *= saturate(factor);
}

float4 SMAABlendingWeightCalculationPS(float2 texcoord, float2 pixcoord, float4 offset[3], float4 subsampleIndices)
{
    float4 weights = float4(0.0, 0.0, 0.0, 0.0);

    float2 e = SampleEdges(texcoord).rg;

    SMAA_BRANCH
    if ( e.g > 0.0 )
    {
        weights.rg = SMAACalculateDiagWeights(texcoord, e, subsampleIndices);

        SMAA_BRANCH
        if ( weights.r == -weights.g )
        {
            float2 d;

            float3 coords;
            coords.x = SMAASearchXLeft(offset[0].xy, offset[2].x);
            coords.y = offset[1].y;
            d.x = coords.x;

            float e1 = SampleEdges(coords.xy).r;

            coords.z = SMAASearchXRight(offset[0].zw, offset[2].y);
            d.y = coords.z;

            d = abs(round(mad(SMAA_RT_METRICS.zz, d, -pixcoord.xx)));

            float2 sqrt_d = sqrt(d);

            float e2 = SampleEdgesOffset(coords.zy, int2(1, 0)).r;

            weights.rg = SMAAArea(sqrt_d, e1, e2, subsampleIndices.y);

            coords.y = texcoord.y;
            SMAADetectHorizontalCornerPattern(weights.rg, coords.xyzy, d);
        }
        else
        {
            e.r = 0.0;
        }
    }

    SMAA_BRANCH
    if ( e.r > 0.0 )
    {
        float2 d;

        float3 coords;
        coords.y = SMAASearchYUp(offset[1].xy, offset[2].z);
        coords.x = offset[0].x;
        d.x = coords.y;

        float e1 = SampleEdges(coords.xy).g;

        coords.z = SMAASearchYDown(offset[1].zw, offset[2].w);
        d.y = coords.z;

        d = abs(round(mad(SMAA_RT_METRICS.ww, d, -pixcoord.yy)));

        float2 sqrt_d = sqrt(d);

        float e2 = SampleEdgesOffset(coords.xz, int2(0, 1)).g;

        weights.ba = SMAAArea(sqrt_d, e1, e2, subsampleIndices.x);

        coords.x = texcoord.x;
        SMAADetectVerticalCornerPattern(weights.ba, coords.xyxz, d);
    }

    return weights;
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    float2 pixcoord = uv * SMAA_RT_METRICS.zw;

    float4 offset[3];
    offset[0] = mad(SMAA_RT_METRICS.xyxy, float4(-0.25, -0.125, 1.25, -0.125), uv.xyxy);
    offset[1] = mad(SMAA_RT_METRICS.xyxy, float4(-0.125, -0.25, -0.125, 1.25), uv.xyxy);
    offset[2] = mad(SMAA_RT_METRICS.xxyy,
        float4(-2.0, 2.0, -2.0, 2.0) * float(SMAA_MAX_SEARCH_STEPS),
        float4(offset[0].xz, offset[1].yw));

    // SMAA 1x passes zero subsample indices.
    return SMAABlendingWeightCalculationPS(uv, pixcoord, offset, float4(0.0, 0.0, 0.0, 0.0));
}
