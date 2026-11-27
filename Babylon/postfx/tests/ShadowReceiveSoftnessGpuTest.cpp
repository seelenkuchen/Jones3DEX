// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/ShadowReceiveEffect.h>

#include <babylon/render/Device.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include "ShadowReceiveGpuSupport.h"

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;
    using namespace babylon::postfx::testsupport;

    // The shadowed floor directly under the caster. A row through it crosses
    // both shadow edges, where a wider filter makes more pixels partially lit.
    int partialEdgePixels(const std::vector<std::byte>& pixels, int fullyShadowed)
    {
        int count = 0;
        for ( std::uint32_t x = 20; x <= 45; ++x )
        {
            const int value = pixelAt(pixels, kSize, x, kFloorRow)[0];
            if ( value > fullyShadowed + 1 && value < 127 )
            {
                ++count;
            }
        }
        return count;
    }

    TEST_CASE("Shadow receive penumbra widens the caster's shadow edge (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
        const TextureHandle normals = makeNormals(*device, kSize, kSize);

        std::vector<std::string_view> ran;
        std::vector<std::byte> narrow;
        const ShadowReceiveSettings defaults;
        if ( !runCasterFloorScene(*device, kMapMargin, defaults, color, normals, &narrow, &ran) )
        {
            babylon::render::test::noGpuDevice("no caster scene: " + joined(sink));
            return;
        }
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        ShadowReceiveSettings penumbra = defaults;
        penumbra.penumbra = 0.1f;
        std::vector<std::byte> wide;
        if ( !runCasterFloorScene(*device, kMapMargin, penumbra, color, normals, &wide, &ran) )
        {
            babylon::render::test::noGpuDevice("no caster scene: " + joined(sink));
            return;
        }
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        // The fully shadowed centre is still darkened with the wider filter;
        // its value is the lower bound for a partially shadowed edge pixel.
        const std::array<int, 4> centre =
            pixelAt(wide, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(centre[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * penumbra.strength) + 1.0f / 255.0f);
        }
        CHECK(centre[3] == 255);

        const int narrowPartial = partialEdgePixels(narrow, centre[0]);
        const int widePartial   = partialEdgePixels(wide, centre[0]);
        INFO("partially shadowed pixels: narrow " << narrowPartial << ", wide "
            << widePartial);
        CHECK(widePartial > narrowPartial);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
    }

    TEST_CASE("Shadow receive distance fade leaves a far receiver unchanged (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        const TextureHandle color   = makeHalfColor(*device, kSize, kSize);
        const TextureHandle normals = makeNormals(*device, kSize, kSize);

        // The floor is 0.6 view units below the caster. A margin of 0.55 leaves
        // it 0.05 view units behind the map's far side; a margin of 0.3 leaves
        // it 0.3 behind.
        ShadowReceiveSettings fade;
        fade.distanceFadeStart = 0.1f;
        fade.distanceFadeEnd   = 0.2f;

        std::vector<std::string_view> ran;
        std::vector<std::byte> nearPixels;
        if ( !runCasterFloorScene(*device, 0.55f, fade, color, normals, &nearPixels, &ran) )
        {
            babylon::render::test::noGpuDevice("no caster scene: " + joined(sink));
            return;
        }
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);

        std::vector<std::byte> farPixels;
        if ( !runCasterFloorScene(*device, kFarBelowMargin, fade, color, normals, &farPixels, &ran) )
        {
            babylon::render::test::noGpuDevice("no caster scene: " + joined(sink));
            return;
        }
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);

        // Just behind the map the shadow still darkens the floor.
        const std::array<int, 4> nearShadow =
            pixelAt(nearPixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(nearShadow[channel] < 127);
        }
        CHECK(nearShadow[3] == 255);

        // Beyond the fade end the receiver keeps its exact input colour.
        const std::array<int, 4> farShadow =
            pixelAt(farPixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(farShadow[channel] - 128) <= 1);
        }
        CHECK(farShadow[3] == 255);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
    }

    TEST_CASE("Shadow receive scales the shadow by the receiver's luminance (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        // The left half is 0.25 (half 0x3400) and the right half 0.5 (0x3800).
        // With this gate the dark left half is below minLight and the bright
        // right half is between minLight and fullLight.
        const TextureHandle color = makeSplitHalfColor(*device, kSize, kSize, 0x3400u, 0x3800u);
        const TextureHandle normals = makeNormals(*device, kSize, kSize);

        ShadowReceiveSettings off;
        off.strength = 0.0f;
        std::vector<std::string_view> ran;
        std::vector<std::byte> plain;
        if ( !runCasterFloorScene(*device, kMapMargin, off, color, normals, &plain, &ran) )
        {
            babylon::render::test::noGpuDevice("no caster scene: " + joined(sink));
            return;
        }
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 1);
        CHECK(ran[0] == "copy");

        ShadowReceiveSettings lit;
        lit.minLight  = 0.4f;
        lit.fullLight = 0.6f;
        std::vector<std::byte> pixels;
        if ( !runCasterFloorScene(*device, kMapMargin, lit, color, normals, &pixels, &ran) )
        {
            babylon::render::test::noGpuDevice("no caster scene: " + joined(sink));
            return;
        }
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "shadow receive");
        CHECK(ran[1] == "copy");

        // The dark receiver under the caster is unchanged.
        const std::array<int, 4> dark =
            pixelAt(pixels, kSize, kShadowedColumn - 1u, kFloorRow);
        const std::array<int, 4> darkPlain =
            pixelAt(plain, kSize, kShadowedColumn - 1u, kFloorRow);
        CHECK(dark == darkPlain);

        // The bright receiver under the caster is darkened.
        const std::array<int, 4> bright =
            pixelAt(pixels, kSize, kShadowedColumn + 1u, kFloorRow);
        const std::array<int, 4> brightPlain =
            pixelAt(plain, kSize, kShadowedColumn + 1u, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(bright[channel] < brightPlain[channel]);
        }
        CHECK(bright[3] == 255);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
    }
}
