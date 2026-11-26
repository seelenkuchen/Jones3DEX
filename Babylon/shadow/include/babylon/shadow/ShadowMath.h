// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstdint>

namespace babylon::shadow
{

    // A 4x4 matrix, row-major: m[row * 4 + column]; transforms column
    // vectors: p' = M * (x, y, z, 1).
    struct Matrix4
    {
        float m[16];
    };

    // The matrix that leaves a point unchanged.
    Matrix4 identity();

    // a * b: the transform of b followed by the transform of a.
    Matrix4 multiply(const Matrix4& a, const Matrix4& b);

    // p' = m * (x, y, z, 1). out has all four components, w included.
    void transformPoint(const Matrix4& m, const float in[3], float out[4]);

    // A view-space axis-aligned box.
    struct Bounds
    {
        float min[3];
        float max[3];
    };

    // The light's view and orthographic projection that cover bounds, looking
    // along -towardLight. The result maps view-space positions to shadow clip
    // space: x and y in -1..1 across the map (y up), z in 0..1 depth (0 at the
    // surface nearest the light). The box is expanded by margin on every side;
    // the projection's x/y origin is snapped to whole shadow-map texels
    // (mapSize) so a moving camera does not make the shadow edges shimmer.
    // towardLight need not be normalized; a zero vector, a non-finite or empty
    // box, a negative margin or a mapSize of 2 or less returns false.
    [[nodiscard]] bool fitDirectional(const float towardLight[3], const Bounds& casters,
        float margin, std::uint32_t mapSize, Matrix4* pViewToShadowClip);

    // The shadow clip to texture transform: u = x * 0.5 + 0.5,
    // v = 0.5 - y * 0.5, z unchanged.
    Matrix4 shadowClipToTexture();

}
