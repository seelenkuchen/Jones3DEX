// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/ContactShadowEffect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/Resources.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <babylon/shaders/babylon_postfx_tests.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include "ContactShadowGpuSupport.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;
    using namespace babylon::postfx::testsupport;

    TEST_CASE("Contact shadows darken the floor beside a box (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

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
        renderDepthScene(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        ContactShadowEffect effect;
        effect.setLightDirection(0.5f, 0.85f, 0.0f);

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        REQUIRE(pixels.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "contact shadows");
        CHECK(ran[1] == "copy");

        // The mask is smoothed by the blur, so the shadow over an 8x8 block in
        // its middle has no dither pattern left: a low standard deviation.
        double sum = 0.0;
        double sumSquares = 0.0;
        for ( std::uint32_t y = kSmoothRow; y < kSmoothRow + kSmoothSize; ++y )
        {
            for ( std::uint32_t x = kSmoothColumn; x < kSmoothColumn + kSmoothSize; ++x )
            {
                const double value = static_cast<double>(pixelAt(pixels, kSize, x, y)[0]) / 255.0;
                sum += value;
                sumSquares += value * value;
            }
        }
        const double count = static_cast<double>(kSmoothSize * kSmoothSize);
        const double mean = sum / count;
        const double variance = sumSquares / count - mean * mean;
        CHECK(mean < 0.5);
        CHECK(std::sqrt(std::max(0.0, variance)) < 0.02);

        // Just left of the box base, on the side away from the light: the ray
        // starts toward the box and is blocked by its front face.
        const std::array<int, 4> shadowed = pixelAt(pixels, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(shadowed[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength) + 1.0f / 255.0f);
        }
        CHECK(shadowed[3] == 255);

        // The floor away from the box and the box top are unchanged; the two
        // kinds of non-scene depth stay byte-exact.
        checkUnchanged(pixelAt(pixels, kSize, kFarFloorColumn, kFloorRow));
        checkUnchanged(pixelAt(pixels, kSize, kBoxTopColumn, kBoxTopRow));
        CHECK(pixelAt(pixels, kSize, 2, 2) == std::array<int, 4>{ 128, 128, 128, 255 });
        CHECK(pixelAt(pixels, kSize, 40, 4) == std::array<int, 4>{ 128, 128, 128, 255 });

        // The no-effect copy, for the exact-input comparisons below.
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        // Half the screen is tagged class 2 (sky) in the normals alpha, next to
        // the shadowed ordinary floor. The tagged half takes no shadow at all,
        // byte for byte, even where the blur carries a shadow across the edge.
        const TextureHandle mixedNormals = makeMixedNormals(*device, kSize, kSize);
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
                CHECK(pixelAt(mixed, kSize, x, y) == std::array<int, 4>{ 128, 128, 128, 255 });
            }
        }

        // Just left of the tag edge the ordinary floor still takes its shadow:
        // the tagged taps weigh nothing and cannot lighten it.
        const std::array<int, 4> edgeShadow = pixelAt(mixed, kSize, kSize / 2u - 1u, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            const float value = static_cast<float>(edgeShadow[channel]) / 255.0f;
            CHECK(value <= 0.5f * (1.0f - 0.5f * effect.settings().strength) + 1.0f / 255.0f);
        }

        // A mask on the occluder's pixels: the rays that the box blocked now
        // cross it as gaps, so the floor it shadowed keeps its input colour,
        // and the box itself keeps its input colour too, byte for byte.
        const TextureHandle occluderMask = makeOccluderMask(*device, kSize, kSize);
        effect.setIgnoreMask(occluderMask);
        std::vector<std::string_view> occluderRan;
        const std::vector<std::byte> occluded =
            runScene(*device, &effect, color, depth, normals, projection, &occluderRan);
        REQUIRE(occluded.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(occluderRan.size() == 2);
        CHECK(occluderRan[0] == "contact shadows");
        CHECK(occluderRan[1] == "copy");
        CHECK(pixelAt(occluded, kSize, kShadowedColumn, kFloorRow)
            == pixelAt(plain, kSize, kShadowedColumn, kFloorRow));
        CHECK(pixelAt(occluded, kSize, kBoxTopColumn, kBoxTopRow)
            == pixelAt(plain, kSize, kBoxTopColumn, kBoxTopRow));

        // A mask on floor pixels well away from the shadow: those pixels keep
        // their input colour and every other pixel, including the whole shadow,
        // matches the no-mask run byte for byte.
        const TextureHandle floorMask = makeRectMask(*device, kSize, kSize, 2, 10, 44, 52);
        effect.setIgnoreMask(floorMask);
        std::vector<std::string_view> floorMaskRan;
        const std::vector<std::byte> floorMasked =
            runScene(*device, &effect, color, depth, normals, projection, &floorMaskRan);
        REQUIRE(floorMasked.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(floorMaskRan.size() == 2);
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize; ++x )
            {
                const bool masked = x >= 2 && x < 10 && y >= 44 && y < 52;
                CHECK(pixelAt(floorMasked, kSize, x, y)
                    == pixelAt(masked ? plain : pixels, kSize, x, y));
            }
        }

        // Removing the mask restores the byte-identical no-mask result.
        effect.setIgnoreMask({});
        std::vector<std::string_view> restoredRan;
        const std::vector<std::byte> restored =
            runScene(*device, &effect, color, depth, normals, projection, &restoredRan);
        REQUIRE(restored.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(restoredRan.size() == 2);
        CHECK(restored == pixels);

        // Strength 0 skips the effect, leaving a byte-identical copy.
        ContactShadowEffect inactive;
        ContactShadowSettings off;
        off.strength = 0.0f;
        inactive.setSettings(off);
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
        device->destroyTexture(occluderMask);
        device->destroyTexture(floorMask);
        device->destroyTexture(depth);
    }

    TEST_CASE("Contact shadows leave surfaces that face away from the light unchanged (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

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
        renderDepthScene(*device, depth, depthFormat);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        // The light below the floor: the floor, the box top and the box's
        // visible left face all face away from it, and its front face is edge-on.
        // Nothing lit can be shadowed, so the output is the plain copy.
        ContactShadowEffect effect;
        effect.setLightDirection(0.5f, -0.85f, 0.0f);

        std::vector<std::string_view> ran;
        const std::vector<std::byte> pixels =
            runScene(*device, &effect, color, depth, normals, projection, &ran);
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        INFO("log: " << joined(sink));
        REQUIRE(pixels.size() == plain.size());
        REQUIRE(ran.size() == 2);
        CHECK(ran[0] == "contact shadows");
        CHECK(ran[1] == "copy");

        std::size_t numDarkened = 0;
        for ( std::uint32_t y = 0; y < kSize; ++y )
        {
            for ( std::uint32_t x = 0; x < kSize; ++x )
            {
                if ( pixelAt(pixels, kSize, x, y) != pixelAt(plain, kSize, x, y) )
                {
                    ++numDarkened;
                }
            }
        }
        INFO("darkened pixels: " << numDarkened << " of " << kSize * kSize);
        CHECK(numDarkened == 0);
        checkUnchanged(pixelAt(pixels, kSize, kFarFloorColumn, kFloorRow));
        checkUnchanged(pixelAt(pixels, kSize, kShadowedColumn, kFloorRow));
        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }

}
