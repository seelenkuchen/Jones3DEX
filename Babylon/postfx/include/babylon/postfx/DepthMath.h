// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <babylon/postfx/FrameInputs.h>

namespace babylon::postfx
{

    // The positive view-space distance along the view axis of a depth texel. The projection's
    // depth is affine in 1/z between depthNear (at the near plane) and depthFar (at the far plane):
    //
    //   t = (depthTexel - depthNear) / (depthFar - depthNear)
    //   z = 1 / (1 / near + t * (1 / far - 1 / near))
    //
    // This form hits the planes exactly. The contact-shadow shader uses the same formula; keep the
    // two in step.
    float linearViewDepth(const Projection& projection, float depthTexel);

    // True when depthTexel names a scene surface: depthNear <= depthTexel < depthFar, less a tiny
    // epsilon at the far plane so cleared depth (exactly depthFar) counts as no surface. Effects
    // that reconstruct positions leave a texel that is not a scene depth unchanged.
    bool isSceneDepth(const Projection& projection, float depthTexel);

}
