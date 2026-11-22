// Added: the depth-aware blur pass of stdRenderBabylon's screen-space ambient occlusion stage
// (M9.17b), used with present.vert.hlsl. Written for this project. A 4 x 4 bilateral box over the
// half-res occlusion term: each tap is weighted by how close its linearised depth is to the
// centre's, so the crease stays sharp while the fixed rotation pattern is smoothed away. Tap and
// centre depths below 0.1 are the first-person overlay range and carry no occlusion.
//
// Fragment texture slot 0 is the raw half-res occlusion term (nearest), slot 1 the depth target
// (nearest). Uniform block 0: x and y one half-res texel, z near plane, w far plane. Block 1: x
// the inverse depth scale that sets how quickly a tap loses weight across a depth edge.

Texture2D<float4> gSsao : register(t0, space2);
SamplerState gSsaoSampler : register(s0, space2);
Texture2D<float> gDepth : register(t1, space2);
SamplerState gDepthSampler : register(s1, space2);

cbuffer SsaoBlur : register(b0, space3)
{
    float4 gBlur; // x, y: 1 / half-res size; z: near; w: far
};

cbuffer SsaoBlurDepth : register(b1, space3)
{
    float4 gBlurDepth; // x: inverse depth scale
};

// rdCamera's sz is 0.1 at the near plane and 1.0 at the far plane, 1/z-linear in between.
float LinearizeDepth(float sz)
{
    const float nearPlane = max(gBlur.z, 1.0e-5);
    const float farPlane  = max(gBlur.w, nearPlane + 1.0e-5);
    const float depthRange = saturate((sz - 0.1) / 0.9);
    return (nearPlane * farPlane) / max(farPlane - depthRange * (farPlane - nearPlane), 1.0e-5);
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float centerDepth = gDepth.SampleLevel(gDepthSampler, uv, 0);
    if ( centerDepth < 0.1 )
    {
        return float4(1.0, 0.0, 0.0, 1.0);
    }

    const float centerZ = LinearizeDepth(centerDepth);
    const float inverseScale = max(gBlurDepth.x, 1.0e-5);

    float sum = 0.0;
    float weightSum = 0.0;
    for ( int y = -1; y <= 2; ++y )
    {
        for ( int x = -1; x <= 2; ++x )
        {
            const float2 tapUv = uv + float2((float)x, (float)y) * gBlur.xy;
            const float tapDepth = gDepth.SampleLevel(gDepthSampler, tapUv, 0);
            if ( tapDepth < 0.1 )
            {
                continue;
            }
            const float tapZ = LinearizeDepth(tapDepth);
            const float depthWeight = 1.0 / (1.0 + abs(tapZ - centerZ) * inverseScale);
            sum += gSsao.SampleLevel(gSsaoSampler, tapUv, 0).r * depthWeight;
            weightSum += depthWeight;
        }
    }

    return float4(sum / max(weightSum, 1.0e-5), 0.0, 0.0, 1.0);
}
