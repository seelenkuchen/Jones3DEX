// Added: the water-ripple pass of stdRenderBabylon (M9.17), used with present.vert.hlsl. Written
// for this project. Only pixels whose normal-target alpha carries the water surface tag are changed:
// the scene is sampled at a small offset from the slope of two scrolling sine layers, and a faint
// view-dependent highlight is added. The offset is a constant fraction of the output height, so its
// pixel size follows the resolution. If the offset sample is not water (it crossed a shoreline), the
// unoffset sample is used, so geometry next to the water never smears in. Every other pixel is
// copied through unchanged.
//
// Fragment texture slot 0 is the pixel's own scene sample (nearest), slot 1 the same scene sampled
// at the offset (linear), slot 2 the normal target (nearest).
// Uniform block 0: x accumulated game time (s), y amplitude as a fraction of the height, z wave
// scale, w the water tag. Block 1: x highlight strength, y highlight power, z and w the two layer
// speeds. Block 2: x and y the target size, z the slope-to-normal scale.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gSceneLinear : register(t1, space2);
SamplerState gSceneLinearSampler : register(s1, space2);
Texture2D<float4> gNormal : register(t2, space2);
SamplerState gNormalSampler : register(s2, space2);

cbuffer WaterRipples : register(b0, space3)
{
    float4 gWater;
};

cbuffer WaterRipplesShape : register(b1, space3)
{
    float4 gShape;
};

cbuffer WaterRipplesSize : register(b2, space3)
{
    float4 gSize;
};

float IsWater(float2 uv)
{
    const float alpha = gNormal.SampleLevel(gNormalSampler, uv, 0).a;
    return 1.0 - step(0.02, abs(alpha - gWater.w));
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    if ( IsWater(uv) < 0.5 )
    {
        return scene;
    }

    // Two sine layers scrolling in different directions; their slopes make the animated normal.
    const float2 dir1 = float2(1.0, 0.6);
    const float2 dir2 = float2(-0.7, 1.0);
    const float2 p = uv * gWater.z;
    const float2 slope = cos(dot(p, dir1) + gWater.x * gShape.z) * dir1
        + cos(dot(p, dir2) - gWater.x * gShape.w) * dir2;

    // Displace by a fraction of the output height, isotropically in pixels.
    const float2 offsetPixels = slope * (gWater.y * gSize.w);
    const float2 offsetUv = offsetPixels / max(gSize.xy, float2(1.0, 1.0));
    const float2 sampleUv = IsWater(uv + offsetUv) > 0.5 ? uv + offsetUv : uv;
    const float3 color = gSceneLinear.SampleLevel(gSceneLinearSampler, sampleUv, 0).rgb;

    // A faint highlight from the screen-space normal and the view, so the water catches the light
    // as the camera moves.
    const float3 normal = normalize(float3(slope * gSize.z, 1.0));
    const float3 viewDir = normalize(float3((uv - 0.5) * float2(1.0, -1.0), 1.0));
    const float3 lightDir = normalize(float3(-0.4, 0.6, 0.7));
    const float3 halfVec = normalize(viewDir + lightDir);
    const float highlight = pow(saturate(dot(normal, halfVec)), gShape.y);

    return float4(color + highlight * gShape.x, scene.a);
}
