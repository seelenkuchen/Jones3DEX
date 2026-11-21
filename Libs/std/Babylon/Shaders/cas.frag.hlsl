// Added: Contrast Adaptive Sharpening (CAS), the optional sharpening pass of stdRenderBabylon's
// presented image. It runs after SMAA (or after the tone map when anti-aliasing is off) and before
// the HUD composite, so it sharpens only the 3D scene. Adapted from the sharpen-only, no-scaling
// path of ffx_cas.h of AMD's FidelityFX CAS (https://github.com/GPUOpen-Effects/FidelityFX-CAS,
// commit 9fabcc9a2c45f958aff55ddfda337e74ef894b7f); see CAS-LICENSE.txt beside this file. It
// samples the four axis neighbours and the centre, shapes the local contrast into a negative lobe
// and keeps the result in 0..1.
//
// Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

Texture2D<float4> gSource : register(t0, space2);
SamplerState gSampler : register(s0, space2);

cbuffer Sharpen : register(b0, space3)
{
    // x: 1 / width, y: 1 / height, z: sharpness 0..1, w: unused.
    float4 gSharpen;
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float2 texel = gSharpen.xy;

    // The sharpen-only 3x3 window of ffx_cas.h: the four axis neighbours and the centre.
    const float4 e = gSource.SampleLevel(gSampler, uv, 0);
    const float3 b = gSource.SampleLevel(gSampler, uv + float2(0.0, -1.0) * texel, 0).rgb;
    const float3 d = gSource.SampleLevel(gSampler, uv + float2(-1.0, 0.0) * texel, 0).rgb;
    const float3 f = gSource.SampleLevel(gSampler, uv + float2(1.0, 0.0) * texel, 0).rgb;
    const float3 h = gSource.SampleLevel(gSampler, uv + float2(0.0, 1.0) * texel, 0).rgb;

    // Soft minimum and maximum of the axis neighbours (the extra multiply is factored out).
    const float3 mn = min(min(min(d, e.rgb), min(f, b)), h);
    const float3 mx = max(max(max(d, e.rgb), max(f, b)), h);

    // Smooth minimum distance to the signal limit divided by the smooth maximum. A zero maximum
    // is a flat black neighbourhood, which has no contrast to sharpen.
    const float3 limit = min(mn, 1.0 - mx);
    const float3 amp = saturate(limit / max(mx, 1.0e-8));
    const float3 shaped = sqrt(amp);

    // Sharpness 0 is the gentlest negative lobe (-1/8); 1 is the strongest (-1/5).
    const float peak = -1.0 / lerp(8.0, 5.0, saturate(gSharpen.z));
    const float3 weight = shaped * peak;
    const float3 rcpWeight = 1.0 / (1.0 + 4.0 * weight);

    const float3 color = saturate((b * weight + d * weight + f * weight + h * weight + e.rgb)
        * rcpWeight);
    return float4(color, e.a);
}
