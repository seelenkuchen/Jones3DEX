// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/DepthMath.h>

namespace babylon::postfx
{
    namespace
    {
        // Matches the shader's far-plane epsilon: clear depth is exactly depthFar and must not be
        // mistaken for a surface.
        constexpr float kSceneDepthEpsilon = 1.0e-6f;
    }

    float linearViewDepth(const Projection& projection, float depthTexel)
    {
        const double nearPlane = projection.nearPlane;
        const double farPlane  = projection.farPlane;
        const double depthNear = projection.depthNear;
        const double depthFar  = projection.depthFar;

        // depth = a + b/z with depthNear at z = near and depthFar at z = far, solved for z.
        // Double intermediates keep the planes exact in the float result.
        const double t = (depthTexel - depthNear) / (depthFar - depthNear);
        const double inverseZ = 1.0 / nearPlane + t * (1.0 / farPlane - 1.0 / nearPlane);
        return static_cast<float>(1.0 / inverseZ);
    }

    bool isSceneDepth(const Projection& projection, float depthTexel)
    {
        return depthTexel >= projection.depthNear && depthTexel < projection.depthFar - kSceneDepthEpsilon;
    }

}
