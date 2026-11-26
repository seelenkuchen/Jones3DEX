// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Test shader: fills a depth target with a view-space floor plane. The camera
// is at the origin looking along +z, with the plane y = gFloorY below it. A
// ray that does not meet the floor gets the far plane's depth, which is not a
// scene depth.

cbuffer FloorParams : register(b0, space3)
{
    float gNear;
    float gFar;
    float gDepthNear;
    float gDepthFar;
    float gTanHalfFovX;
    float gTanHalfFovY;
    float gFloorY;
    float gPadding;
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

Output main(float2 uv : TEXCOORD0)
{
    Output output;

    float dy = (1.0 - uv.y * 2.0) * gTanHalfFovY;
    float z = gFar;
    if ( dy < -1.0e-6 )
    {
        float floorZ = gFloorY / dy;
        if ( floorZ > gNear && floorZ < z )
        {
            z = floorZ;
        }
    }

    output.depth = depthFromViewZ(z);
    return output;
}
