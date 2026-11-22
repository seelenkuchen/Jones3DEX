// Added: the lens-flare composite of stdRenderBabylon's M9.18 camera effects, used with
// present.vert.hlsl. Written for this project. It runs on the HDR scene after the light-shaft
// stage and before bloom, so the flare blooms with the rest of the bright image. The light
// position is the light-shaft stage's GPU-reduced 1 x 1 sky centroid (mean weight and weighted
// position) and the smoothed visible-sky strength; nothing is read back to the CPU. A few tinted
// ghosts sit along the line from the light through the screen centre, and a soft halo surrounds
// the light. Both scale by the smoothed strength, so the flare fades out with the sky, and the
// whole pass is a no-op when no sky is visible or the centroid is off screen.
//
// Fragment texture slot 0 is the HDR scene (nearest so a pixel is copied exactly), slot 1 the
// 1 x 1 centroid (nearest), slot 2 the 1 x 1 smoothed strength (nearest).
// Uniform block 0: gParams.x the global flare gain, y the screen aspect, z the halo radius,
// w the halo intensity; gGhosts[i] is (t along the light-centre line, radius, intensity, unused)
// and gTints[i] the ghost's rgb tint.

Texture2D<float4> gScene : register(t0, space2);
SamplerState gSceneSampler : register(s0, space2);
Texture2D<float4> gCentroid : register(t1, space2);
SamplerState gCentroidSampler : register(s1, space2);
Texture2D<float4> gStrength : register(t2, space2);
SamplerState gStrengthSampler : register(s2, space2);

cbuffer LensFlare : register(b0, space3)
{
    float4 gParams;
    float4 gGhosts[4];
    float4 gTints[4];
};

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 scene = gScene.SampleLevel(gSceneSampler, uv, 0);

    const float4 centroidSample = gCentroid.SampleLevel(gCentroidSampler, float2(0.5, 0.5), 0);
    const float weight = centroidSample.x;
    const float strength = gStrength.SampleLevel(gStrengthSampler, float2(0.5, 0.5), 0).x;
    if ( weight <= 1.0e-6 || strength <= 1.0e-4 )
    {
        return scene;
    }

    const float2 light = centroidSample.yz / weight;
    if ( light.x < 0.0 || light.x > 1.0 || light.y < 0.0 || light.y > 1.0 )
    {
        return scene;
    }

    const float2 center = float2(0.5, 0.5);
    const float2 aspect = float2(gParams.y, 1.0);
    const float2 dir = center - light;

    float3 flare = 0.0;
    for ( int i = 0; i < 4; ++i )
    {
        const float2 position = light + dir * gGhosts[i].x;
        const float2 offset = (uv - position) * aspect;
        const float radius = max(gGhosts[i].y, 1.0e-4);
        const float glow = saturate(1.0 - length(offset) / radius);
        flare += gTints[i].rgb * (glow * glow) * gGhosts[i].z;
    }

    // A wide, faint halo around the light itself.
    const float halo = saturate(1.0 - length((uv - light) * aspect) / max(gParams.z, 1.0e-4));
    flare += gTints[0].rgb * (halo * halo) * gParams.w;

    return float4(scene.rgb + flare * strength * gParams.x, scene.a);
}
