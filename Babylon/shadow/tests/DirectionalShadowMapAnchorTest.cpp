// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/DirectionalShadowMap.h>
#include <babylon/shadow/ShadowMath.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <span>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::render;
    using namespace babylon::shadow;

    constexpr std::uint32_t kMapSize = 256;

    // The transform uniform bound before the only draw.
    Matrix4 firstDrawUniforms(const std::vector<RecordedCommand>& records)
    {
        const std::vector<std::byte>* pUniforms = nullptr;
        for ( const RecordedCommand& record : records )
        {
            if ( const auto* pPush = std::get_if<PushUniforms>(&record) )
            {
                pUniforms = &pPush->data;
            }
            else if ( std::get_if<Draw>(&record) != nullptr )
            {
                break;
            }
        }
        REQUIRE(pUniforms != nullptr);
        REQUIRE(pUniforms->size() == sizeof(Matrix4));
        Matrix4 result{};
        std::memcpy(result.m, pUniforms->data(), sizeof(Matrix4));
        return result;
    }

    // The inverse of a rigid rotation: its transpose.
    Matrix4 transpose(const Matrix4& m)
    {
        Matrix4 result{};
        for ( int row = 0; row < 4; ++row )
        {
            for ( int column = 0; column < 4; ++column )
            {
                result.m[row * 4 + column] = m.m[column * 4 + row];
            }
        }
        return result;
    }

    Matrix4 rotationZ(float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        Matrix4 result = identity();
        result.m[0] = c;
        result.m[1] = -s;
        result.m[4] = s;
        result.m[5] = c;
        return result;
    }

    Matrix4 rotationX(float angle)
    {
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        Matrix4 result = identity();
        result.m[5]  = c;
        result.m[6]  = -s;
        result.m[9]  = s;
        result.m[10] = c;
        return result;
    }

    void rotateDirection(const Matrix4& m, const float in[3], float out[3])
    {
        for ( int row = 0; row < 3; ++row )
        {
            out[row] = m.m[row * 4 + 0] * in[0] + m.m[row * 4 + 1] * in[1]
                + m.m[row * 4 + 2] * in[2];
        }
    }

    Bounds boundsOf(const std::array<std::array<float, 3>, 8>& points)
    {
        Bounds bounds = {
            { points[0][0], points[0][1], points[0][2] },
            { points[0][0], points[0][1], points[0][2] },
        };
        for ( const std::array<float, 3>& point : points )
        {
            for ( int axis = 0; axis < 3; ++axis )
            {
                const float value = point[static_cast<std::size_t>(axis)];
                bounds.min[axis] = value < bounds.min[axis] ? value : bounds.min[axis];
                bounds.max[axis] = value > bounds.max[axis] ? value : bounds.max[axis];
            }
        }
        return bounds;
    }
}

TEST_CASE("A world-anchored fit ignores camera rotation")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    // A long, thin box, turned away from every world axis and moved off the
    // origin. Its view-space AABB changes shape as the camera turns, so taking
    // that AABB into the world would still make the fit change with the view.
    const std::array<std::array<float, 3>, 8> localCorners = { {
        { -1.6f, -0.18f, -0.35f }, { 1.6f, -0.18f, -0.35f },
        { -1.6f, 0.18f, -0.35f }, { 1.6f, 0.18f, -0.35f },
        { -1.6f, -0.18f, 0.35f }, { 1.6f, -0.18f, 0.35f },
        { -1.6f, 0.18f, 0.35f }, { 1.6f, 0.18f, 0.35f },
    } };
    const Matrix4 casterTilt = multiply(rotationZ(0.63f), rotationX(0.41f));
    const float casterOffset[3] = { 0.37f, -0.29f, 1.13f };
    std::array<std::array<float, 3>, 8> worldCorners{};
    for ( std::size_t index = 0; index < worldCorners.size(); ++index )
    {
        float tilted[4];
        transformPoint(casterTilt, localCorners[index].data(), tilted);
        for ( int axis = 0; axis < 3; ++axis )
        {
            worldCorners[index][static_cast<std::size_t>(axis)] =
                tilted[axis] + casterOffset[axis];
        }
    }

    const float worldLight[3] = { 0.27f, -0.81f, 0.52f };
    const float fixedWorldPoint[3] = { 0.11f, 0.73f, -0.42f };
    const std::array<std::array<float, 2>, 5> cameraAngles = { {
        { 0.0f, 0.0f }, { 0.35f, 0.2f }, { -0.7f, 0.45f }, { 1.1f, -0.3f },
        { 2.3f, 0.6f },
    } };

    Matrix4 firstLightClip{};
    float firstU = 0.0f;
    float firstV = 0.0f;
    float firstDepth = 0.0f;
    bool firstFit = true;
    bool firstSample = true;
    for ( const std::array<float, 2>& angles : cameraAngles )
    {
        // Yaw around world Z, then pitch around world X.
        const Matrix4 viewToWorld = multiply(rotationZ(angles[0]), rotationX(angles[1]));
        const Matrix4 worldToView = transpose(viewToWorld);

        // The same world casters, in the new view space.
        std::vector<float> viewVertices;
        viewVertices.reserve(worldCorners.size() * 3);
        for ( const std::array<float, 3>& worldCorner : worldCorners )
        {
            float viewPoint[4];
            transformPoint(worldToView, worldCorner.data(), viewPoint);
            viewVertices.push_back(viewPoint[0]);
            viewVertices.push_back(viewPoint[1]);
            viewVertices.push_back(viewPoint[2]);
        }
        float viewLight[3];
        rotateDirection(worldToView, worldLight, viewLight);

        map.setViewToWorld(viewToWorld);
        device->clearRecorded();
        const CasterBatch batch{ viewVertices.data(), viewVertices.size() / 3 };
        const std::array<CasterBatch, 1> batches{ batch };
        CommandList list = device->beginFrame();
        REQUIRE(map.record(list, viewLight, std::span<const CasterBatch>(batches)));
        REQUIRE(device->submit(list));
        CHECK(map.isValid());

        // Recover the world-anchored fit from the view-space uniform.
        const Matrix4 viewToShadowClip = firstDrawUniforms(device->recorded());
        const Matrix4 lightClipFromWorld = multiply(viewToShadowClip, worldToView);
        if ( firstFit )
        {
            firstLightClip = lightClipFromWorld;
            firstFit = false;
        }
        else
        {
            for ( int index = 0; index < 16; ++index )
            {
                CHECK(lightClipFromWorld.m[index]
                    == doctest::Approx(firstLightClip.m[index]).epsilon(1.0e-6));
            }
        }

        // A fixed world point lands on the same shadow-map uv.
        float viewFixedPoint[4];
        transformPoint(worldToView, fixedWorldPoint, viewFixedPoint);
        float uv[4];
        transformPoint(map.viewToShadowTexture(), viewFixedPoint, uv);
        if ( firstSample )
        {
            firstU = uv[0];
            firstV = uv[1];
            firstDepth = uv[2];
            firstSample = false;
        }
        else
        {
            CHECK(uv[0] == doctest::Approx(firstU).epsilon(1.0e-6));
            CHECK(uv[1] == doctest::Approx(firstV).epsilon(1.0e-6));
            CHECK(uv[2] == doctest::Approx(firstDepth).epsilon(1.0e-6));
        }
    }

    // clearViewToWorld() returns to the view-relative fit.
    map.clearViewToWorld();
    std::vector<float> viewVertices;
    viewVertices.reserve(worldCorners.size() * 3);
    for ( const std::array<float, 3>& worldCorner : worldCorners )
    {
        viewVertices.push_back(worldCorner[0]);
        viewVertices.push_back(worldCorner[1]);
        viewVertices.push_back(worldCorner[2]);
    }
    device->clearRecorded();
    const CasterBatch viewBatch{ viewVertices.data(), viewVertices.size() / 3 };
    const std::array<CasterBatch, 1> viewBatches{ viewBatch };
    CommandList checkList = device->beginFrame();
    REQUIRE(map.record(checkList, worldLight, std::span<const CasterBatch>(viewBatches)));
    REQUIRE(device->submit(checkList));
    CHECK(map.isValid());

    Matrix4 expected{};
    REQUIRE(fitDirectional(worldLight, boundsOf(worldCorners), 0.05f, kMapSize, &expected));
    const Matrix4 viewToShadowClip = firstDrawUniforms(device->recorded());
    for ( int index = 0; index < 16; ++index )
    {
        CHECK(viewToShadowClip.m[index] == expected.m[index]);
    }
}
