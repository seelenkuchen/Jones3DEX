// Added: the ambient-occlusion pass of stdRenderBabylon's screen-space ambient occlusion stage
// (M9.17b), used with present.vert.hlsl. Written for this project; the depth-reconstruction and
// normal-oriented-hemisphere formulation is the standard screen-space ambient occlusion technique.
// It runs at half the scene size and writes the ambient-occlusion term in red: 1 means no
// occlusion. The following depth-aware blur removes the fixed 4 x 4 rotation pattern.
//
// The classic draws write a constant facing-the-camera normal, so the pass ignores the normal
// target's colour and reconstructs the view-space position from the depth target. The depth target
// holds rdCamera's sz: 0.1 at the near plane and 1.0 at the far plane, 1/z-linear in between; the
// first-person overlay uses 0..0.1 and gets no occlusion. The normal comes from the neighbouring
// depths, taking the smaller-difference neighbour on each axis so a depth edge does not tilt it.
//
// Only ordinary surface-tagged pixels take part: the normal target's alpha is 0 where nothing was
// drawn, 1 for ordinary geometry, 1/3 for water and 2/3 for sky, and water and sky return 1.
//
// Fragment texture slot 0 is the depth target, slot 1 the normal target (both nearest). Uniform
// block 0: x near plane, y far plane, z world-space radius, w bias. Block 1: x intensity, y sample
// count, z and w one half-res texel. Block 2: x horizontal and y vertical tan(fov / 2), already
// scaled for the real aspect (the fov is horizontal at 4:3, Hor+), then z and w the half-res size
// in pixels (the rotation pattern is indexed by them).

Texture2D<float> gDepth : register(t0, space2);
SamplerState gDepthSampler : register(s0, space2);
Texture2D<float4> gNormal : register(t1, space2);
SamplerState gNormalSampler : register(s1, space2);

cbuffer Ssao : register(b0, space3)
{
    float4 gSsao; // x: near, y: far, z: radius, w: bias
};

cbuffer SsaoParams : register(b1, space3)
{
    float4 gSsaoParams; // x: intensity, y: samples, z, w: 1 / half-res size
};

cbuffer SsaoProjection : register(b2, space3)
{
    float4 gProjection; // x, y: aspect-corrected tan half fov; z, w: half-res size
};

// rdCamera's sz is 0.1 at the near plane and 1.0 at the far plane, 1/z-linear in between.
float LinearizeDepth(float sz)
{
    const float nearPlane = max(gSsao.x, 1.0e-5);
    const float farPlane  = max(gSsao.y, nearPlane + 1.0e-5);
    const float depthRange = saturate((sz - 0.1) / 0.9);
    return (nearPlane * farPlane) / max(farPlane - depthRange * (farPlane - nearPlane), 1.0e-5);
}

// View-space position at uv with the given positive distance into the screen. The camera looks
// down +z, y is up and the projection's tangent scales map the pixel to view space.
float3 ViewPosition(float2 uv, float viewZ)
{
    const float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    return float3(ndc.x * gProjection.x * viewZ, ndc.y * gProjection.y * viewZ, viewZ);
}

float ViewDistance(float2 uv)
{
    return LinearizeDepth(gDepth.SampleLevel(gDepthSampler, uv, 0));
}

// The normal from the neighbouring depths, the smaller-difference neighbour per axis. A difference
// is taken toward the +u or +v direction either way, so the cross product has a stable sign.
float3 ReconstructNormal(float2 uv, float3 center, float centerZ)
{
    const float3 left   = ViewPosition(uv - float2(gSsaoParams.z, 0.0), ViewDistance(uv - float2(gSsaoParams.z, 0.0)));
    const float3 right  = ViewPosition(uv + float2(gSsaoParams.z, 0.0), ViewDistance(uv + float2(gSsaoParams.z, 0.0)));
    const float3 upper  = ViewPosition(uv - float2(0.0, gSsaoParams.w), ViewDistance(uv - float2(0.0, gSsaoParams.w)));
    const float3 lower  = ViewPosition(uv + float2(0.0, gSsaoParams.w), ViewDistance(uv + float2(0.0, gSsaoParams.w)));

    const float3 alongU = abs(right.z - centerZ) < abs(centerZ - left.z) ? right - center : center - left;
    const float3 alongV = abs(upper.z - centerZ) < abs(lower.z - centerZ) ? center - upper : lower - center;

    const float3 normal = cross(alongU, alongV);
    const float lengthSquared = dot(normal, normal);
    return lengthSquared > 1.0e-12 ? normal * rsqrt(lengthSquared) : float3(0.0, 0.0, -1.0);
}

// A deterministic hemisphere sample: a golden-angle spiral with a cosine-ish height, no time noise.
float3 HemiSample(int index, int count, float angle)
{
    const float fraction = (float(index) + 0.5) / max((float)count, 1.0);
    const float phi      = (float(index) * 2.39996323) + angle;
    const float radius   = sqrt(fraction);
    const float height   = sqrt(max(1.0 - fraction, 0.0));
    float2 sampleOffset = float2(cos(phi), sin(phi)) * radius;
    const float2 rotated = float2(sampleOffset.x * cos(angle) - sampleOffset.y * sin(angle),
        sampleOffset.x * sin(angle) + sampleOffset.y * cos(angle));
    return float3(rotated, height);
}

float4 main(float2 uv : TEXCOORD0) : SV_Target0
{
    const float4 normalSample = gNormal.SampleLevel(gNormalSampler, uv, 0);
    const float depthSample = gDepth.SampleLevel(gDepthSampler, uv, 0);

    // Sky, water, the first-person overlay (sz < 0.1) and empty pixels get no occlusion.
    if ( abs(normalSample.a - 1.0) > 0.02 || depthSample < 0.1 || depthSample >= 0.999999 )
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const float centerZ = LinearizeDepth(depthSample);
    if ( centerZ <= 0.0 )
    {
        return float4(1.0, 1.0, 1.0, 1.0);
    }

    const float3 center = ViewPosition(uv, centerZ);
    const float3 normal = ReconstructNormal(uv, center, centerZ);

    // The fixed 4 x 4 interleaved rotation pattern: a per-pixel angle from its half-res position.
    const uint2 pixel = (uint2)(uv * gProjection.zw);
    const float angle = (float)((pixel.x & 3u) + ((pixel.y & 3u) << 2u)) * 0.39269908;

    const float3 up = abs(normal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    const float3 tangent = normalize(cross(up, normal));
    const float3 bitangent = cross(normal, tangent);

    const int numSamples = max((int)gSsaoParams.y, 1);
    float occlusion = 0.0;
    for ( int i = 0; i < numSamples; ++i )
    {
        const float3 local = HemiSample(i, numSamples, angle);
        const float3 direction = tangent * local.x + bitangent * local.y + normal * local.z;
        const float3 samplePosition = center + direction * gSsao.z;
        if ( samplePosition.z <= 1.0e-5 )
        {
            continue;
        }

        const float2 ndc = float2(samplePosition.x / (samplePosition.z * gProjection.x),
            samplePosition.y / (samplePosition.z * gProjection.y));
        const float2 sampleUv = float2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if ( sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0 )
        {
            continue;
        }

        const float sceneZ = ViewDistance(sampleUv);
        // Only geometry in front of the sampled point occludes it, and the weight must fade to zero
        // by twice the radius: a much nearer surface (a character behind the shaded one) is outside
        // the kernel's reach and must not darken it, unlike saturate(radius / |dz|), which never
        // reaches zero.
        const float range = saturate(1.0 - abs(centerZ - sceneZ) / max(2.0 * gSsao.z, 1.0e-5));
        if ( sceneZ < samplePosition.z - gSsao.w )
        {
            occlusion += range * range;
        }
    }

    return float4(saturate(1.0 - gSsaoParams.x * occlusion / (float)numSamples), 0.0, 0.0, 1.0);
}
