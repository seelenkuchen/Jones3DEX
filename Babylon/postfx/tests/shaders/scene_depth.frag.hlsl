// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: fills a depth target with a synthetic view-space scene. The
// camera is at the origin looking along +z, with the plane y = gFloorY below
// it and an axis-aligned box standing on that floor. A ray that misses both
// gets the clear depth. The top-left 8x8 pixels get a depth in front of the
// near plane, standing in for a first-person overlay.

cbuffer SceneParams : register(b0, space3)
{
    float gNear;
    float gFar;
    float gDepthNear;
    float gDepthFar;
    float gTanHalfFovX;
    float gTanHalfFovY;
    float gFloorY;
    float gBoxMinX;
    float gBoxMaxX;
    float gBoxTopY;
    float gBoxFrontZ;
    float gBoxBackZ;
    float2 gPadding;
};

struct Output
{
    float depth : SV_Depth;
};

// The inverse of the postfx linear depth formula: z is affine in 1/z between
// the near and far planes, mapped onto depthNear..depthFar.
float depthFromViewZ(float z)
{
    float invNear = 1.0 / gNear;
    float invFar = 1.0 / gFar;
    float t = (1.0 / z - invNear) / (invFar - invNear);
    return gDepthNear + (gDepthFar - gDepthNear) * t;
}

// Narrows the ray interval [enter, exit] to the part inside one slab. The ray
// starts at the origin, so a slab from low to high along one axis is enough.
void intersectSlab(float direction, float low, float high, inout float enter,
    inout float exitT, inout bool hit)
{
    if ( abs(direction) < 1.0e-8 )
    {
        if ( low > 0.0 || high < 0.0 )
        {
            hit = false;
        }
        return;
    }

    float a = low / direction;
    float b = high / direction;
    if ( a > b )
    {
        float temp = a;
        a = b;
        b = temp;
    }
    enter = max(enter, a);
    exitT = min(exitT, b);
    if ( enter > exitT )
    {
        hit = false;
    }
}

Output main(float2 uv : TEXCOORD0, float4 position : SV_Position)
{
    Output output;

    if ( position.x < 8.0 && position.y < 8.0 )
    {
        output.depth = 0.05;
        return output;
    }

    // The view-space ray through this pixel: (dx, dy, 1) * z.
    float dx = (uv.x * 2.0 - 1.0) * gTanHalfFovX;
    float dy = (1.0 - uv.y * 2.0) * gTanHalfFovY;

    float bestZ = gFar;

    if ( dy < -1.0e-6 )
    {
        float floorZ = gFloorY / dy;
        if ( floorZ > gNear && floorZ < bestZ )
        {
            bestZ = floorZ;
        }
    }

    bool hit = true;
    float enter = 0.0;
    float exitT = gFar;
    intersectSlab(dx, gBoxMinX, gBoxMaxX, enter, exitT, hit);
    intersectSlab(dy, gFloorY, gBoxTopY, enter, exitT, hit);
    intersectSlab(1.0, gBoxFrontZ, gBoxBackZ, enter, exitT, hit);
    if ( hit && enter > gNear && enter < bestZ )
    {
        bestZ = enter;
    }

    output.depth = depthFromViewZ(bestZ);
    return output;
}
