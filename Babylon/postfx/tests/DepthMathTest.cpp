// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/DepthMath.h>
#include <babylon/postfx/FrameInputs.h>

#include <doctest.h>

namespace
{
    using namespace babylon::postfx;

    Projection makeProjection(float depthNear, float depthFar)
    {
        Projection projection;
        projection.nearPlane = 0.1f;
        projection.farPlane  = 100.0f;
        projection.depthNear = depthNear;
        projection.depthFar  = depthFar;
        return projection;
    }
}

TEST_CASE("linearViewDepth maps the near plane, the far plane and the 1/z midpoint")
{
    // The depth is affine in 1/z, so the midpoint depth is the harmonic mean of the planes.
    const float nearPlane = 0.1f;
    const float farPlane  = 100.0f;
    const float midpoint  = 2.0f * nearPlane * farPlane / (nearPlane + farPlane);

    SUBCASE("the standard 0..1 convention")
    {
        const Projection projection = makeProjection(0.0f, 1.0f);
        CHECK(linearViewDepth(projection, 0.0f) == doctest::Approx(nearPlane));
        CHECK(linearViewDepth(projection, 1.0f) == doctest::Approx(farPlane));
        CHECK(linearViewDepth(projection, 0.5f) == doctest::Approx(midpoint));
    }

    SUBCASE("a convention whose near plane is the depth texel 0.1")
    {
        const Projection projection = makeProjection(0.1f, 1.0f);
        CHECK(linearViewDepth(projection, 0.1f) == doctest::Approx(nearPlane));
        CHECK(linearViewDepth(projection, 1.0f) == doctest::Approx(farPlane));
        CHECK(linearViewDepth(projection, 0.55f) == doctest::Approx(midpoint));
    }
}

TEST_CASE("isSceneDepth rejects the near side, the far plane and cleared depth")
{
    SUBCASE("the standard 0..1 convention")
    {
        const Projection projection = makeProjection(0.0f, 1.0f);
        CHECK_FALSE(isSceneDepth(projection, -0.5f));
        CHECK_FALSE(isSceneDepth(projection, -0.0001f));
        CHECK(isSceneDepth(projection, 0.0f));
        CHECK(isSceneDepth(projection, 0.5f));
        CHECK_FALSE(isSceneDepth(projection, 1.0f));
    }

    SUBCASE("a convention whose near plane is the depth texel 0.1")
    {
        const Projection projection = makeProjection(0.1f, 1.0f);
        CHECK_FALSE(isSceneDepth(projection, 0.05f));
        CHECK(isSceneDepth(projection, 0.1f));
        CHECK(isSceneDepth(projection, 0.55f));
        CHECK_FALSE(isSceneDepth(projection, 1.0f));
    }
}
