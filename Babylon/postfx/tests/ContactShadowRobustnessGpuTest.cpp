// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/ContactShadowEffect.h>

#include <babylon/render/Device.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include "ContactShadowGpuSupport.h"

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

    // The floor normal in view space is +y, so a normalised light's y component
    // is its dot with the floor normal. 0.25 is grazing: at kShadowedColumn the
    // box front still sits within the thickness, so the floor would shadow
    // under the old 0..0.2 facing ramp. 0.82 is the fixed overhead light at
    // about 35 degrees and keeps the floor shadowed.
    constexpr float kGrazingLightY  = 0.25f;
    constexpr float kOverheadLightY = 0.82f;

    // The mean red channel of a rectangle, 0..255.
    double meanBrightness(const std::vector<std::byte>& pixels, std::uint32_t minX,
        std::uint32_t minY, std::uint32_t maxX, std::uint32_t maxY)
    {
        double sum = 0.0;
        for ( std::uint32_t y = minY; y < maxY; ++y )
        {
            for ( std::uint32_t x = minX; x < maxX; ++x )
            {
                sum += static_cast<double>(pixelAt(pixels, kSize, x, y)[0]);
            }
        }
        return sum / static_cast<double>((maxX - minX) * (maxY - minY));
    }

    TEST_CASE("Contact shadows skip surfaces nearly edge-on to the light (GPU)")
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

        // The plain copy, for exact-input comparisons.
        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        const std::array<int, 4> plainPixel =
            pixelAt(plain, kSize, kShadowedColumn, kFloorRow);

        // The large x component keeps the ray pointing across the box from the
        // floor to its left, so the box blocks it whenever the ramp lets the
        // pixel through.
        ContactShadowEffect effect;

        // Almost edge-on to the floor: the new wide band skips this pixel, so
        // it matches the plain copy. The old 0..0.2 ramp would shadow it.
        const float grazingX = std::sqrt(1.0f - kGrazingLightY * kGrazingLightY);
        effect.setLightDirection(grazingX, kGrazingLightY, 0.0f);

        std::vector<std::string_view> grazingRan;
        const std::vector<std::byte> grazing =
            runScene(*device, &effect, color, depth, normals, projection, &grazingRan);
        REQUIRE(grazing.size() == plain.size());
        INFO("log: " << joined(sink));
        REQUIRE(grazingRan.size() == 2);
        CHECK(grazingRan[0] == "contact shadows");
        CHECK(grazingRan[1] == "copy");

        const std::array<int, 4> grazingPixel =
            pixelAt(grazing, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(grazingPixel[channel] - plainPixel[channel]) <= 1);
        }
        CHECK(grazingPixel[3] == 255);

        // The overhead light at about 35 degrees, pointing the same way across
        // the box: the same pixel is shadowed well past the 10/255 threshold.
        const float overheadX = std::sqrt(1.0f - kOverheadLightY * kOverheadLightY);
        effect.setLightDirection(overheadX, kOverheadLightY, 0.0f);

        std::vector<std::string_view> overheadRan;
        const std::vector<std::byte> overhead =
            runScene(*device, &effect, color, depth, normals, projection, &overheadRan);
        REQUIRE(overhead.size() == plain.size());
        INFO("log: " << joined(sink));
        REQUIRE(overheadRan.size() == 2);
        CHECK(overheadRan[0] == "contact shadows");
        CHECK(overheadRan[1] == "copy");

        const std::array<int, 4> overheadPixel =
            pixelAt(overhead, kSize, kShadowedColumn, kFloorRow);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(overheadPixel[channel] <= plainPixel[channel] - 10);
        }
        CHECK(overheadPixel[3] == 255);

        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }

    TEST_CASE("Contact shadows leave a steep open floor facing the light unchanged (GPU)")
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
        renderDepthScene(*device, depth, depthFormat, false);

        Projection projection;
        projection.nearPlane   = kNear;
        projection.farPlane    = kFar;
        projection.tanHalfFovX = kHalfFovX;
        projection.tanHalfFovY = kHalfFovY;
        projection.depthNear   = kDepthNear;
        projection.depthFar    = kDepthFar;

        // A flat open floor with no occluder. The light is almost straight up
        // in view space, so the floor faces it (N.L about 0.99), and a small
        // forward component keeps the ray in the floor. Near the bottom of the
        // screen the floor is steep in screen depth: at the first ray step the
        // sample still lands in the receiver's own pixel, so the stored depth
        // is the receiver's own surface, quantised. The old depth window took
        // that as a blocker; the plane test rejects it as the receiver itself.
        ContactShadowEffect effect;
        effect.setLightDirection(0.0f, 0.99f, 0.141f);

        std::vector<std::string_view> plainRan;
        const std::vector<std::byte> plain =
            runScene(*device, nullptr, color, depth, normals, projection, &plainRan);
        REQUIRE(plain.size() == std::size_t{ kSize } * kSize * 4u);
        INFO("log: " << joined(sink));
        REQUIRE(plainRan.size() == 1);
        CHECK(plainRan[0] == "copy");

        std::vector<std::string_view> onRan;
        const std::vector<std::byte> on =
            runScene(*device, &effect, color, depth, normals, projection, &onRan);
        REQUIRE(on.size() == plain.size());
        INFO("log: " << joined(sink));
        REQUIRE(onRan.size() == 2);
        CHECK(onRan[0] == "contact shadows");
        CHECK(onRan[1] == "copy");

        // The open floor is not its own occluder, so it stays nearly unchanged.
        const double plainMean = meanBrightness(plain, 14, 49, 26, 58);
        const double onMean    = meanBrightness(on, 14, 49, 26, 58);
        INFO("floor mean " << onMean << " on, " << plainMean << " plain");
        CHECK(std::abs(onMean - plainMean) <= 1.0);

        CHECK(sink.records().empty());

        device->destroyTexture(color);
        device->destroyTexture(normals);
        device->destroyTexture(depth);
    }
}
