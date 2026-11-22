// Added: the composite pass of stdRenderBabylon's depth-of-field stage (M9.14), used with
// present.vert.hlsl. Written for this project. It samples the HDR scene in fragment texture slot 0,
// the half-resolution gather blur in slot 1 (with the linear, clamping sampler) and the scene depth
// in slot 2, recomputes the circle of confusion and blends the blur over the scene by it. The scene
// alpha is kept. The mode and ramp match dof_gather.frag.hlsl, so the blend uses the same weight the
// gather used.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gBlur : register(t1, space2);
SamplerState gBlurSampler : register(s1, space2);
Texture2D<float> gDepth : register(t2, space2);
SamplerState gDepthSampler : register(s2, space2);

cbuffer Dof : register(b0, space3)
{
    float4 gDof; // x: focus distance, y: near plane, z: far plane, w: mode (0 gameplay, 1 cutscene)
};

cbuffer DofShape : register(b1, space3)
{
    // Gameplay: x where the blur starts, y its span. Cutscene: x the dead zone around the focus,
    // y its span.
    float4 gDofShape;
};

// rdCamera's sz is 0.1 at the near plane and 1.0 at the far plane, 1/z-linear in between.
float LinearizeDepth(float sz)
{
    const float nearPlane = max(gDof.y, 1.0e-5);
    const float farPlane  = max(gDof.z, nearPlane + 1.0e-5);
    const float depthRange = saturate((sz - 0.1) / 0.9);
    return (nearPlane * farPlane) / max(farPlane - depthRange * (farPlane - nearPlane), 1.0e-5);
}

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
    const float depth = LinearizeDepth(gDepth.SampleLevel(gDepthSampler, uv, 0).r);
    const float coc = CircleOfConfusion(depth);
    if ( coc <= 1.0e-4 )
    {
        return scene;
    }

    const float3 blurred = gBlur.SampleLevel(gBlurSampler, uv, 0).rgb;
    return float4(lerp(scene.rgb, blurred, coc), scene.a);
}
