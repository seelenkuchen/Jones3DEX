// Added: the camera motion-blur pass of stdRenderBabylon (M9.19), used with present.vert.hlsl.
// Written for this project; the depth reprojection and directional gather are the standard
// screen-space motion-blur formulation. It reconstructs the current view-space position from the
// depth target exactly as the ambient-occlusion pass does, transforms it into the world with the
// current camera and into the previous frame's camera, projects it with the same projection
// tangents and gathers the HDR scene along the resulting screen-space velocity.
//
// Fragment texture slot 0 is the HDR scene (linear), slot 1 the depth target (nearest) and slot 2
// the normal target (nearest). The normal target's alpha says whether anything was drawn (0 is
// empty); sky pixels (2/3) take part, so a camera rotation blurs the sky too.
//
// Uniform block 0 holds the current camera (three world-space basis columns, each with the
// translation component in w) and then the previous frame's. Blocks 1 to 3 hold the depth planes,
// shutter fraction and maximum blur length in pixels; the target size, horizontal tangent and tap
// count; and the depth falloff.
//
// Shader view space is x right, y up and z forward. The cameras arrive already permuted from
// rdCamera's x right, y forward, z up, so the basis maps shader view space to world directly.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float> gDepth : register(t1, space2);
SamplerState gDepthSampler : register(s1, space2);
Texture2D<float4> gNormal : register(t2, space2);
SamplerState gNormalSampler : register(s2, space2);

cbuffer MotionBlurCamera : register(b0, space3)
{
    float4 gCurrent0;   // xyz: current right, w: current position x
    float4 gCurrent1;   // xyz: current up, w: current position y
    float4 gCurrent2;   // xyz: current forward, w: current position z
    float4 gPrevious0;  // xyz: previous right, w: previous position x
    float4 gPrevious1;  // xyz: previous up, w: previous position y
    float4 gPrevious2;  // xyz: previous forward, w: previous position z
};

cbuffer MotionBlurParams0 : register(b1, space3)
{
    float4 gParams0; // x: near, y: far, z: shutter fraction, w: maximum blur length in pixels
};

cbuffer MotionBlurParams1 : register(b2, space3)
{
    float4 gParams1; // x, y: target size, z: tan(fov / 2) horizontal, w: tap count
};

cbuffer MotionBlurParams2 : register(b3, space3)
{
    float4 gParams2; // x: depth falloff, then unused
};

// rdCamera's sz is 0.1 at the near plane and 1.0 at the far plane, 1/z-linear in between.
float LinearizeDepth(float sz)
{
    const float nearPlane = max(gParams0.x, 1.0e-5);
    const float farPlane  = max(gParams0.y, nearPlane + 1.0e-5);
    const float depthRange = saturate((sz - 0.1) / 0.9);
    return (nearPlane * farPlane) / max(farPlane - depthRange * (farPlane - nearPlane), 1.0e-5);
}

// View-space position at uv with the given positive distance into the screen.
float3 ViewPosition(float2 uv, float viewZ)
{
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    const float tanHalfX = max(gParams1.z, 1.0e-5);
    const float tanHalfY = tanHalfX * gParams1.y / max(gParams1.x, 1.0);
    return float3(ndc.x * tanHalfX * viewZ, ndc.y * tanHalfY * viewZ, viewZ);
}

float3 CurrentToWorld(float3 view)
{
    return gCurrent0.xyz * view.x + gCurrent1.xyz * view.y + gCurrent2.xyz * view.z
        + float3(gCurrent0.w, gCurrent1.w, gCurrent2.w);
}

float3 WorldToPrevious(float3 world)
{
    const float3 relative = world - float3(gPrevious0.w, gPrevious1.w, gPrevious2.w);
    return float3(dot(gPrevious0.xyz, relative), dot(gPrevious1.xyz, relative),
        dot(gPrevious2.xyz, relative));
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    const float sz = gDepth.SampleLevel(gDepthSampler, uv, 0);
    const float tag = gNormal.SampleLevel(gNormalSampler, uv, 0).a;

    // Nothing drawn and the first-person overlay (sz < 0.1) get zero velocity.
    if ( tag < 0.02 || sz < 0.1 )
    {
        return scene;
    }

    const float centerZ = LinearizeDepth(sz);
    const float3 previousView = WorldToPrevious(CurrentToWorld(ViewPosition(uv, centerZ)));
    if ( previousView.z <= 1.0e-4 )
    {
        return scene;
    }

    const float tanHalfX = max(gParams1.z, 1.0e-5);
    const float tanHalfY = tanHalfX * gParams1.y / max(gParams1.x, 1.0);
    const float2 ndcPrevious = float2(previousView.x / (previousView.z * tanHalfX),
        previousView.y / (previousView.z * tanHalfY));
    const float2 uvPrevious = float2(ndcPrevious.x * 0.5 + 0.5, 0.5 - ndcPrevious.y * 0.5);

    // The shutter fraction scales the frame's motion; the length is clamped in output pixels.
    const float2 size = max(gParams1.xy, float2(1.0, 1.0));
    float2 velocity = (uv - uvPrevious) * gParams0.z;
    float2 velocityPixels = velocity * size;
    const float lengthPixels = length(velocityPixels);
    const float maxPixels = max(gParams0.w, 0.0);
    if ( lengthPixels > maxPixels && lengthPixels > 1.0e-5 )
    {
        velocityPixels *= maxPixels / lengthPixels;
    }

    // Less than half a pixel of motion leaves the pixel alone, so a static camera is byte-identical.
    if ( dot(velocityPixels, velocityPixels) < 0.25 )
    {
        return scene;
    }

    velocity = velocityPixels / size;
    const int taps = max((int)gParams1.w, 1);
    const float falloff = max(gParams2.x, 0.0);

    // A single gather along the velocity. A tap whose depth is much nearer than the centre's is
    // weighted down so a background pixel does not smear over a nearer edge.
    float3 sum = scene.rgb;
    float weightSum = 1.0;
    for ( int i = 0; i < taps; ++i )
    {
        const float fraction = ((float)i + 0.5) / (float)taps - 0.5;
        const float2 tapUv = uv + velocity * fraction;
        const float tapSz = gDepth.SampleLevel(gDepthSampler, tapUv, 0);
        float weight = 1.0;
        if ( tapSz < 0.1 )
        {
            // The overlay range carries no scene depth; do not pull it into the blur.
            weight = 0.0;
        }
        else
        {
            const float tapZ = LinearizeDepth(tapSz);
            const float nearer = max(centerZ - tapZ, 0.0);
            weight = saturate(1.0 - nearer * falloff / max(centerZ, 1.0e-3));
        }

        sum += gScene.SampleLevel(gSceneSampler, tapUv, 0).rgb * weight;
        weightSum += weight;
    }

    return float4(sum / max(weightSum, 1.0e-4), scene.a);
}
