// Added: the half-resolution gather pass of stdRenderBabylon's depth-of-field stage (M9.14),
// used with present.vert.hlsl. Written for this project; the circle-of-confusion and disc-gather
// technique is the standard depth-of-field formulation. It samples the HDR scene in fragment
// texture slot 0 and the scene depth target in slot 1 with the nearest, clamping sampler.
//
// gDof.x is the smoothed focus distance in view space, gDof.y and gDof.z the frame's near and far
// planes, gDof.w the mode (0 gameplay, 1 cutscene). gDofParams.x is the option's strength,
// gDofParams.y the largest gather radius in half-resolution texels, and gDofParams.zw the size of
// one half-resolution texel. gDofShape holds the ramp the host worked out from the same constants:
// where the blur starts (gameplay) or how wide the dead zone is (cutscene) in x, its span in y.
//
// Gameplay mode keeps everything nearer than four times the focus distance (at least 1 world unit)
// sharp and ramps the blur in over the next twelve times that (at least 3 units), so only the far
// scene softens. Cutscene mode ramps
// from a small dead zone on either side of the focus, blurring both nearer and farther geometry.
// Each tap is weighted by its own circle of confusion, so a sharp foreground sample never bleeds
// into a blurred background.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float> gDepth : register(t1, space2);
SamplerState gDepthSampler : register(s1, space2);

cbuffer Dof : register(b0, space3)
{
    float4 gDof; // x: focus distance, y: near plane, z: far plane, w: mode (0 gameplay, 1 cutscene)
};

cbuffer DofParams : register(b1, space3)
{
    float4 gDofParams; // x: strength, y: max radius (half-res texels), z, w: 1 / half-res size
};

cbuffer DofShape : register(b2, space3)
{
    // Gameplay: x where the blur starts, y its span. Cutscene: x the dead zone around the focus,
    // y its span.
    float4 gDofShape;
};


// The depth target is 0 at the near plane and 1 at the far plane (the projection std3D sets up).
float LinearizeDepth(float depth)
{
    const float nearPlane = max(gDof.y, 1.0e-4);
    const float farPlane  = max(gDof.z, nearPlane + 1.0e-3);
    return (nearPlane * farPlane) / max(farPlane - depth * (farPlane - nearPlane), 1.0e-4);
}

// The circle of confusion in 0..1; the gather multiplies it by the maximum radius.
float CircleOfConfusion(float depth)
{
    const float focus = max(gDof.x, 1.0e-3);
    if ( gDof.w < 0.5 )
    {
        return saturate((depth - gDofShape.x) / max(gDofShape.y, 1.0e-4));
    }

    return saturate((abs(depth - focus) - gDofShape.x) / max(gDofShape.y, 1.0e-4));
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);
    const float centerDepth = LinearizeDepth(gDepth.SampleLevel(gDepthSampler, uv, 0).r);
    const float centerCoc = CircleOfConfusion(centerDepth);
    if ( centerCoc <= 1.0e-4 )
    {
        return float4(scene.rgb, 1.0);
    }

    const float radius = gDofParams.y * gDofParams.x * centerCoc;
    const float2 texel = gDofParams.zw;

    float3 color = scene.rgb * centerCoc;
    float weight = centerCoc;

    for ( int i = 0; i < 20; ++i )
    {
        // A Vogel disc: an even area coverage with a small, fixed tap count.
        const float fraction = sqrt((float(i) + 0.5) / 20.0);
        const float angle    = float(i) * 2.39996323;
        const float2 offset  = float2(cos(angle), sin(angle)) * radius * fraction * texel;
        const float2 tapUv   = uv + offset;

        const float4 tapColor = gScene.SampleLevel(gSceneSampler, tapUv, 0);
        const float tapDepth = LinearizeDepth(gDepth.SampleLevel(gDepthSampler, tapUv, 0).r);
        const float tapCoc = CircleOfConfusion(tapDepth);

        color += tapColor.rgb * tapCoc;
        weight += tapCoc;
    }

    return float4(color / max(weight, 1.0e-4), 1.0);
}
