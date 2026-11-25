// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FilmGrainEffect.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/VignetteEffect.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    constexpr std::uint32_t kGrainSize = 64;

    std::string joined(const babylon::log::CaptureSink& sink)
    {
        std::string text;
        for ( const auto& record : sink.records() )
        {
            text += text.empty() ? "" : "; ";
            text += record.message;
        }
        return text;
    }

    std::array<int, 4> pixelAt(const std::vector<std::byte>& pixels, std::uint32_t width,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
        return { static_cast<int>(pixels[index + 0u]), static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]), static_cast<int>(pixels[index + 3u]) };
    }

    // An Rgba8 texture filled with one grey value.
    TextureHandle makeGrey(Device& device, std::uint32_t width, std::uint32_t height,
        std::uint8_t value)
    {
        std::vector<std::byte> texels(static_cast<std::size_t>(width) * height * 4u);
        for ( std::size_t index = 0; index < texels.size(); index += 4u )
        {
            texels[index + 0u] = static_cast<std::byte>(value);
            texels[index + 1u] = static_cast<std::byte>(value);
            texels[index + 2u] = static_cast<std::byte>(value);
            texels[index + 3u] = static_cast<std::byte>(255);
        }

        const TextureHandle texture = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba8, TextureUsage::Sampled });
        REQUIRE_FALSE(texture.isNull());
        REQUIRE(device.uploadTexture(texture, 0, texels));
        return texture;
    }

    // Runs one effect through a chain from input into a fresh output and reads it back.
    std::vector<std::byte> runEffect(Device& device, Effect& effect, TextureHandle input,
        std::uint32_t width, std::uint32_t height, std::uint64_t frameIndex)
    {
        const TextureHandle output = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba8, TextureUsage::ColorTarget });
        REQUIRE_FALSE(output.isNull());

        Chain chain(device);
        REQUIRE(chain.open());
        REQUIRE(chain.add(effect));

        FrameInputs frame;
        frame.color       = input;
        frame.colorFormat = TextureFormat::Rgba8;
        frame.width       = width;
        frame.height      = height;
        frame.frameIndex  = frameIndex;
        OutputTarget target;
        target.texture = output;
        target.format  = TextureFormat::Rgba8;
        target.width   = width;
        target.height  = height;

        CommandList list = device.beginFrame();
        REQUIRE(chain.record(list, frame, target));
        REQUIRE(device.submit(list));

        std::vector<std::byte> pixels = device.readTexture(output, 0);
        chain.close();
        device.destroyTexture(output);
        return pixels;
    }

    TEST_CASE("The vignette darkens the corners but not the centre (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        const TextureHandle input = makeGrey(*device, 64, 32, 128);

        VignetteEffect effect;
        effect.setSettings(VignetteSettings{ 1.0f, 0.45f, 1.0f });
        const std::vector<std::byte> pixels = runEffect(*device, effect, input, 64, 32, 0);
        REQUIRE(pixels.size() == 64u * 32u * 4u);

        const std::array<int, 4> centre = pixelAt(pixels, 64, 32, 16);
        const std::array<int, 4> corner = pixelAt(pixels, 64, 0, 0);
        const std::array<int, 4> left   = pixelAt(pixels, 64, 8, 16);
        const std::array<int, 4> right  = pixelAt(pixels, 64, 55, 16);

        CHECK(std::abs(centre[0] - 128) <= 1);
        CHECK(corner[0] <= 5);
        for ( std::size_t channel = 0; channel < 3; ++channel )
        {
            CHECK(std::abs(left[channel] - right[channel]) <= 1);
        }

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(input);
    }

    TEST_CASE("The film grain adds frame-varying mid-tone noise (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        const TextureHandle grey = makeGrey(*device, kGrainSize, kGrainSize, 128);
        FilmGrainEffect effect;
        effect.setSettings(FilmGrainSettings{ 0.1f });

        const std::vector<std::byte> first =
            runEffect(*device, effect, grey, kGrainSize, kGrainSize, 0x1111u);
        REQUIRE(first.size() == std::size_t{ kGrainSize } * kGrainSize * 4u);

        double sum = 0.0;
        int maxDelta = 0;
        std::size_t different = 0;
        for ( std::size_t index = 0; index < first.size(); index += 4u )
        {
            const int value = static_cast<int>(first[index]);
            const int delta = value - 128;
            sum += static_cast<double>(value) / 255.0;
            maxDelta = std::max(maxDelta, std::abs(delta));
            if ( value != 128 )
            {
                ++different;
            }
        }
        constexpr std::size_t kTexels = std::size_t{ kGrainSize } * kGrainSize;
        const double mean = sum / static_cast<double>(kTexels);

        CHECK(mean >= 0.5 - 0.01);
        CHECK(mean <= 0.5 + 0.01);
        CHECK(static_cast<double>(maxDelta) / 255.0 <= 0.1 + 1.0 / 255.0);
        CHECK(different * 10 >= kTexels * 9);

        // The same frame index is deterministic; a different one changes the image.
        const std::vector<std::byte> again =
            runEffect(*device, effect, grey, kGrainSize, kGrainSize, 0x1111u);
        CHECK(again == first);
        const std::vector<std::byte> other =
            runEffect(*device, effect, grey, kGrainSize, kGrainSize, 0x2222u);
        CHECK(other != first);

        // On black the mid-tone weighting halves the peak noise.
        const TextureHandle black = makeGrey(*device, kGrainSize, kGrainSize, 0);
        const std::vector<std::byte> dark =
            runEffect(*device, effect, black, kGrainSize, kGrainSize, 0x1111u);
        REQUIRE(dark.size() == first.size());
        int maxDark = 0;
        for ( std::size_t index = 0; index < dark.size(); index += 4u )
        {
            maxDark = std::max(maxDark, static_cast<int>(dark[index]));
        }
        CHECK(static_cast<double>(maxDark) / 255.0 <= 0.05 + 1.0 / 255.0);

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(grey);
        device->destroyTexture(black);
    }
}
