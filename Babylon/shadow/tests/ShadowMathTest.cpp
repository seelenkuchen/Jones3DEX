// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/ShadowMath.h>

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace
{
    using namespace babylon::shadow;

    void cornerOf(const Bounds& bounds, int index, float out[3])
    {
        out[0] = (index & 1) != 0 ? bounds.max[0] : bounds.min[0];
        out[1] = (index & 2) != 0 ? bounds.max[1] : bounds.min[1];
        out[2] = (index & 4) != 0 ? bounds.max[2] : bounds.min[2];
    }

    float dot3(const float a[3], const float b[3])
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    Bounds unitBox()
    {
        return Bounds{ { -0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f } };
    }

    Bounds translatedX(const Bounds& box, float offset)
    {
        Bounds moved = box;
        moved.min[0] += offset;
        moved.max[0] += offset;
        return moved;
    }
}

TEST_CASE("identity, multiply and transformPoint follow the row-major convention")
{
    const Matrix4 identityMatrix = identity();
    CHECK(identityMatrix.m[0] == 1.0f);
    CHECK(identityMatrix.m[5] == 1.0f);
    CHECK(identityMatrix.m[10] == 1.0f);
    CHECK(identityMatrix.m[15] == 1.0f);
    CHECK(identityMatrix.m[1] == 0.0f);
    CHECK(identityMatrix.m[4] == 0.0f);

    const float point[3] = { 1.0f, -2.0f, 3.0f };
    float transformed[4] = {};
    transformPoint(identityMatrix, point, transformed);
    CHECK(transformed[0] == doctest::Approx(point[0]));
    CHECK(transformed[1] == doctest::Approx(point[1]));
    CHECK(transformed[2] == doctest::Approx(point[2]));
    CHECK(transformed[3] == doctest::Approx(1.0f));

    Matrix4 translate = identity();
    translate.m[3]  = 1.0f;
    translate.m[7]  = 2.0f;
    translate.m[11] = 3.0f;
    Matrix4 scale = identity();
    scale.m[0]  = 2.0f;
    scale.m[5]  = 3.0f;
    scale.m[10] = 4.0f;

    // a * b applies b first.
    Matrix4 scaledThenMoved = multiply(translate, scale);
    transformPoint(scaledThenMoved, point, transformed);
    CHECK(transformed[0] == doctest::Approx(3.0f));
    CHECK(transformed[1] == doctest::Approx(-4.0f));
    CHECK(transformed[2] == doctest::Approx(15.0f));

    Matrix4 movedThenScaled = multiply(scale, translate);
    transformPoint(movedThenScaled, point, transformed);
    CHECK(transformed[0] == doctest::Approx(4.0f));
    CHECK(transformed[1] == doctest::Approx(0.0f));
    CHECK(transformed[2] == doctest::Approx(24.0f));

    Matrix4 projective = identity();
    projective.m[15] = 2.0f;
    transformPoint(projective, point, transformed);
    CHECK(transformed[3] == doctest::Approx(2.0f));
}

TEST_CASE("fitDirectional covers a unit box under a straight-up light")
{
    const float light[3] = { 0.0f, 1.0f, 0.0f };
    const Bounds box = unitBox();
    Matrix4 viewToClip{};
    REQUIRE(fitDirectional(light, box, 0.0f, 64, &viewToClip));

    float smallestZ = 2.0f;
    float largestZ = -1.0f;
    float nearestDot = -1.0e30f;
    for ( int index = 0; index < 8; ++index )
    {
        float corner[3];
        cornerOf(box, index, corner);
        float projected[4];
        transformPoint(viewToClip, corner, projected);
        CHECK(projected[0] >= -1.0f - 1.0e-5f);
        CHECK(projected[0] <= 1.0f + 1.0e-5f);
        CHECK(projected[1] >= -1.0f - 1.0e-5f);
        CHECK(projected[1] <= 1.0f + 1.0e-5f);
        CHECK(projected[2] >= -1.0e-5f);
        CHECK(projected[2] <= 1.0f + 1.0e-5f);
        CHECK(projected[3] == doctest::Approx(1.0f));
        smallestZ = projected[2] < smallestZ ? projected[2] : smallestZ;
        largestZ  = projected[2] > largestZ ? projected[2] : largestZ;
        nearestDot = dot3(corner, light) > nearestDot ? dot3(corner, light) : nearestDot;
    }
    CHECK(smallestZ == doctest::Approx(0.0f));
    CHECK(largestZ == doctest::Approx(1.0f));

    // The corner nearest the light is the deepest-to-shallowest depth 0.
    for ( int index = 0; index < 8; ++index )
    {
        float corner[3];
        cornerOf(box, index, corner);
        if ( dot3(corner, light) == doctest::Approx(nearestDot) )
        {
            float projected[4];
            transformPoint(viewToClip, corner, projected);
            CHECK(projected[2] == doctest::Approx(0.0f));
        }
    }
}

TEST_CASE("A margin enlarges the range fitDirectional projects")
{
    const float light[3] = { 0.0f, 1.0f, 0.0f };
    const Bounds box = unitBox();
    Matrix4 tight{};
    Matrix4 loose{};
    REQUIRE(fitDirectional(light, box, 0.0f, 64, &tight));
    REQUIRE(fitDirectional(light, box, 0.5f, 64, &loose));

    const float corner[3] = { 0.5f, 0.5f, 0.5f };
    float tightProjected[4];
    float looseProjected[4];
    transformPoint(tight, corner, tightProjected);
    transformPoint(loose, corner, looseProjected);

    CHECK(std::fabs(looseProjected[0]) < std::fabs(tightProjected[0]));
    CHECK(std::fabs(looseProjected[1]) < std::fabs(tightProjected[1]));
    CHECK(looseProjected[2] > 0.0f);
    CHECK(looseProjected[2] < 1.0f);
}

TEST_CASE("fitDirectional snaps its x origin to whole shadow-map texels")
{
    const float light[3] = { 0.0f, 1.0f, 0.0f };
    const Bounds box = unitBox();
    const std::uint32_t mapSize = 16;
    const float fixedPoint[3] = { 0.0f, 0.0f, 0.0f };

    float previousU = 0.0f;
    bool sawStep = false;
    for ( int step = 0; step < 31; ++step )
    {
        // Steps are much smaller than a texel; the whole sweep crosses at
        // least two texel boundaries.
        const float offset = static_cast<float>(step) * 0.005f;
        Matrix4 viewToClip{};
        REQUIRE(fitDirectional(light, translatedX(box, offset), 0.0f, mapSize, &viewToClip));

        float projected[4];
        transformPoint(viewToClip, fixedPoint, projected);
        const float u = (projected[0] * 0.5f + 0.5f) * static_cast<float>(mapSize);
        if ( step > 0 )
        {
            const float delta = std::fabs(u - previousU);
            const bool same = delta < 1.0e-3f;
            const bool oneTexel = std::fabs(delta - 1.0f) < 1.0e-2f;
            CHECK((same || oneTexel));
            sawStep = sawStep || oneTexel;
        }
        previousU = u;
    }
    CHECK(sawStep);
}

TEST_CASE("A flat box fits once the margin gives it depth")
{
    const float light[3] = { 0.0f, 1.0f, 0.0f };
    const Bounds flat = { { -1.0f, -0.5f, -1.0f }, { 1.0f, -0.5f, 1.0f } };
    Matrix4 viewToClip{};

    CHECK_FALSE(fitDirectional(light, flat, 0.0f, 64, &viewToClip));
    REQUIRE(fitDirectional(light, flat, 0.5f, 64, &viewToClip));
    for ( int index = 0; index < 8; ++index )
    {
        float corner[3];
        cornerOf(flat, index, corner);
        float projected[4];
        transformPoint(viewToClip, corner, projected);
        CHECK(projected[0] >= -1.0f - 1.0e-5f);
        CHECK(projected[0] <= 1.0f + 1.0e-5f);
        CHECK(projected[1] >= -1.0f - 1.0e-5f);
        CHECK(projected[1] <= 1.0f + 1.0e-5f);
        CHECK(projected[2] >= -1.0e-5f);
        CHECK(projected[2] <= 1.0f + 1.0e-5f);
    }
}

TEST_CASE("fitDirectional normalizes the light direction")
{
    const float unitLight[3] = { 0.0f, 1.0f, 0.0f };
    const float scaledLight[3] = { 0.0f, 5.0f, 0.0f };
    const Bounds box = unitBox();
    Matrix4 unit{};
    Matrix4 scaled{};
    REQUIRE(fitDirectional(unitLight, box, 0.0f, 64, &unit));
    REQUIRE(fitDirectional(scaledLight, box, 0.0f, 64, &scaled));
    for ( int index = 0; index < 16; ++index )
    {
        CHECK(scaled.m[index] == doctest::Approx(unit.m[index]));
    }
}

TEST_CASE("fitDirectional refuses a zero light and an invalid box")
{
    const float light[3] = { 0.0f, 1.0f, 0.0f };
    const float zeroLight[3] = { 0.0f, 0.0f, 0.0f };
    const Bounds box = unitBox();
    Matrix4 result{};

    CHECK_FALSE(fitDirectional(zeroLight, box, 0.0f, 64, &result));
    CHECK_FALSE(fitDirectional(light, Bounds{ { 0.0f, 0.0f, 0.0f }, { -1.0f, 1.0f, 1.0f } },
        0.0f, 64, &result));
    CHECK_FALSE(fitDirectional(light, Bounds{ { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        0.0f, 64, &result));
    CHECK_FALSE(fitDirectional(light, box, -1.0f, 64, &result));
    CHECK_FALSE(fitDirectional(light, box, 0.0f, 2, &result));
    CHECK_FALSE(fitDirectional(light, box, 0.0f, 64, nullptr));
}

TEST_CASE("shadowClipToTexture maps clip space to the map's texture space")
{
    const Matrix4 texture = shadowClipToTexture();
    const float center[3] = { 0.0f, 0.0f, 0.25f };
    const float topLeft[3] = { -1.0f, 1.0f, 0.0f };
    const float bottomRight[3] = { 1.0f, -1.0f, 1.0f };
    float out[4];

    transformPoint(texture, center, out);
    CHECK(out[0] == doctest::Approx(0.5f));
    CHECK(out[1] == doctest::Approx(0.5f));
    CHECK(out[2] == doctest::Approx(0.25f));

    transformPoint(texture, topLeft, out);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[1] == doctest::Approx(0.0f));
    CHECK(out[2] == doctest::Approx(0.0f));

    transformPoint(texture, bottomRight, out);
    CHECK(out[0] == doctest::Approx(1.0f));
    CHECK(out[1] == doctest::Approx(1.0f));
    CHECK(out[2] == doctest::Approx(1.0f));
}
