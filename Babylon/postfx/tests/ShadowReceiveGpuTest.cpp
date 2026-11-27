// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>
#include <babylon/postfx/ShadowReceiveEffect.h>

#include <babylon/shadow/DirectionalShadowMap.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <babylon/shaders/babylon_postfx_tests.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include "ShadowReceiveGpuSupport.h"

#include <doctest.h>

#include <algorithm>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;
    using namespace babylon::postfx::testsupport;

    TEST_CASE("Shadow receive darkens the floor under a shadow-map caster (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        // The shadow map is a real one, built with the shadow module in the
        // test only.
        babylon::shadow::DirectionalShadowMap map(*device);
        babylon::shadow::ShadowMapDesc mapDesc;
        mapDesc.size = kMapSize;
        if ( !map.open(mapDesc) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }
        REQUIRE(map.isOpen());

        const std::vector<float> vertices = casterVertices();
        const babylon::shadow::CasterBatch batch{ vertices.data(), vertices.size() / 3 };
        const std::array<babylon::shadow::CasterBatch, 1> batches{ batch };
        CommandList shadowList = device->beginFrame();
        REQUIRE(map.record(shadowList, kLight,
            std::span<const babylon::shadow::CasterBatch>(batches), kMapMargin));
        REQUIRE(device->submit(shadowList));
        REQUIRE(map.isValid());

        const TextureUsage depthUsage = TextureUsage::DepthTarget | TextureUsage::Sampled;
        TextureFormat depthFormat = TextureFormat::D32F;
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            depthFormat = TextureFormat::D24S8;
        }
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }

        const TextureHandle color          = makeHalfColor(*device, kSize, kSize);
        const TextureHandle normals        = makeNormals(*device, kSize, kSize);
        const TextureHandle mixedNormals   = makeMixedNormals(*device, kSize, kSize);
        const TextureHandle excludeMask    = makeExcludeMask(*device, kSize, kSize);
        const TextureHandle depth          = device->createTexture(TextureDesc{ kSize, kSize, 1,
            depthFormat, depthUsage });
        REQUIRE_FALSE(depth.isNull());
        renderFloorDepth(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        ShadowReceiveEffect effect;
        ShadowMapInput mapInput;
        mapInput.texture        = map.texture();
        mapInput.compareSampler = map.compareSampler();
        mapInput.mapSize        = map.size();
        for ( int index = 0; index < 16; ++index )
        {
            mapInput.matrix[index] = map.viewToShadowTexture().m[index];
        }
        mapInput.towardLight[0] = kLight[0];
        mapInput.towardLight[1] = kLight[1];
        mapInput.towardLight[2] = kLight[2];
        effect.setShadowMap(mapInput);

        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        REQUIRE(pixels.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        // The floor directly under the caster is shadowed.
        const std::array<int, 4> shadowed =
            pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength)
                + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The floor away from the caster is lit and unchanged.
        const std::array<int, 4> lit = pixelAt(pixels, kSize, kLitColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(lit[channel] - 128) <= 1);
        }
        CHECK(lit[3] == 255);

        // Without a mask the shadow covers both halves of the screen. Column
        // 31 is left of the middle and column 33 right, both under the caster.
        const std::array<int, 4> shadowedLeft =
            pixelAt(pixels, kSize, kShadowedColumn - 1u, kFloorRow);
        const std::array<int, 4> shadowedRight =
            pixelAt(pixels, kSize, kShadowedColumn + 1u, kFloorRow);
        for ( const std::array<int, 4>* pShadowed : { &shadowedLeft, &shadowedRight } )
        {
            for ( std::size_t channel = 0; channel < 3; ++channel )
            {
                const float value = static_cast<float>((*pShadowed)[channel]) / 255.0f;
                CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength)
                    + 1.0f / 255.0f);
            }
            CHECK((*pShadowed)[3] == 255);
        }

        // A mask that is red on the left half keeps those shadowed pixels at
        // their exact input colour, while the right half darkens as before.
        ShadowMapInput maskedMap = mapInput;
        maskedMap.excludeMask = excludeMask;
        effect.setShadowMap(maskedMap);
        std::vector<std::string_view> maskedRan;
        const std::vector<std::byte> masked =
            runScene(*device, &effect, color, depth, normals, projection, &maskedRan);
        REQUIRE(masked.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(maskedRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize / 2u; ++x )
            {
                CHECK(pixelAt(masked, kSize, x, y) == pixelAt(plain, kSize, x, y));
            }
            for ( std::uint32_t x = kSize / 2u; x < kSize; ++x )
            {
                CHECK(pixelAt(masked, kSize, x, y) == pixelAt(pixels, kSize, x, y));
            }
        }
        // The same mask in include mode only lets the red left half receive:
        // the left half darkens like the no-mask run and the right half keeps
        // the plain input colour.
        ShadowMapInput includedMap = mapInput;
        includedMap.excludeMask = excludeMask;
        includedMap.maskMode    = MaskMode::Include;
        effect.setShadowMap(includedMap);
        std::vector<std::string_view> includedRan;
        const std::vector<std::byte> included =
            runScene(*device, &effect, color, depth, normals, projection, &includedRan);
        REQUIRE(included.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(includedRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize / 2u; ++x )
            {
                CHECK(pixelAt(included, kSize, x, y) == pixelAt(pixels, kSize, x, y));
            }
            for ( std::uint32_t x = kSize / 2u; x < kSize; ++x )
            {
                CHECK(pixelAt(included, kSize, x, y) == pixelAt(plain, kSize, x, y));
            }
        }
        effect.setShadowMap(mapInput);

        // Half the screen is tagged class 2 (not in the receiver mask) in the
        // normals alpha; those pixels stay byte-identical to the plain copy.
        std::vector<std::string_view> mixedRan;
        const std::vector<std::byte> mixed =
            runScene(*device, &effect, color, depth, mixedNormals, projection, &mixedRan);
        REQUIRE(mixed.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(mixedRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = kSize / 2u; x < kSize; ++x )
            {
                CHECK(pixelAt(mixed, kSize, x, y) == pixelAt(plain, kSize, x, y));
            }
        }

        // Strength 0 skips the effect, leaving a byte-identical copy.
        ShadowReceiveEffect inactive;
        ShadowReceiveSettings off;
        off.strength = 0.0f;
        inactive.setSettings(off);
        inactive.setShadowMap(mapInput);
        CHECK_FALSE(inactive.active());
        std::vector<std::string_view> inactiveRan;
        const std::vector<std::byte> skipped =
            runScene(*device, &inactive, color, depth, normals, projection, &inactiveRan);
        INFO("log: " << joined(sink));
        REQUIRE(inactiveRan.size() == 1);
        CHECK(inactiveRan[0] == "copy");
        CHECK(skipped == plain);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(mixedNormals);
        device->destroyTexture(excludeMask);
        device->destroyTexture(depth);
    }

    TEST_CASE("Shadow receive still darkens a floor below the map's depth range (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        // The small margin fits the depth range to the caster only; the floor
        // is farther from the light than the map's far end, so its normalized
        // depth is greater than 1.
        babylon::shadow::DirectionalShadowMap map(*device);
        babylon::shadow::ShadowMapDesc mapDesc;
        mapDesc.size = kMapSize;
        if ( !map.open(mapDesc) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }
        REQUIRE(map.isOpen());

        const std::vector<float> vertices = casterVertices();
        const babylon::shadow::CasterBatch batch{ vertices.data(), vertices.size() / 3 };
        const std::array<babylon::shadow::CasterBatch, 1> batches{ batch };
        CommandList shadowList = device->beginFrame();
        REQUIRE(map.record(shadowList, kLight,
            std::span<const babylon::shadow::CasterBatch>(batches), kFarBelowMargin));
        REQUIRE(device->submit(shadowList));
        REQUIRE(map.isValid());

        const TextureUsage depthUsage = TextureUsage::DepthTarget | TextureUsage::Sampled;
        TextureFormat depthFormat = TextureFormat::D32F;
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            depthFormat = TextureFormat::D24S8;
        }
        if ( !device->supportsFormat(depthFormat, depthUsage) )
        {
            babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
            return;
        }

        const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
        const TextureHandle normals = makeNormals(*device, kSize, kSize);
        const TextureHandle depth   = device->createTexture(TextureDesc{ kSize, kSize, 1,
            depthFormat, depthUsage });
        REQUIRE_FALSE(depth.isNull());
        renderFloorDepth(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        ShadowReceiveEffect effect;
        ShadowMapInput mapInput;
        mapInput.texture        = map.texture();
        mapInput.compareSampler = map.compareSampler();
        mapInput.mapSize        = map.size();
        for ( int index = 0; index < 16; ++index )
        {
            mapInput.matrix[index] = map.viewToShadowTexture().m[index];
        }
        mapInput.towardLight[0] = kLight[0];
        mapInput.towardLight[1] = kLight[1];
        mapInput.towardLight[2] = kLight[2];
        effect.setShadowMap(mapInput);

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        REQUIRE(pixels.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        // The floor under the caster is beyond the map's far end but must still
        // be shadowed.
        const std::array<int, 4> shadowed =
            pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength)
                + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The cleared part of the map still passes, so the floor clear of the
        // caster stays lit.
        const std::array<int, 4> lit =
            pixelAt(pixels, kSize, kFarBelowLitColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(lit[channel] - 128) <= 1);
        }
        CHECK(lit[3] == 255);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }
}
