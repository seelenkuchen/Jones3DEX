// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FrameInputs.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

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

    std::uint16_t halfBits(float value)
    {
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
        const std::uint32_t sign = (bits >> 16) & 0x8000u;
        if ( value == 0.0f )
        {
            return static_cast<std::uint16_t>(sign);
        }
        const std::int32_t exponent =
            static_cast<std::int32_t>((bits >> 23) & 0xFFu) - 127;
        const std::uint32_t mantissa = bits & 0x7FFFFFu;
        // The test's values are normal and exactly representable in half precision.
        const std::uint32_t halfExponent = static_cast<std::uint32_t>(exponent + 15);
        const std::uint32_t halfMantissa = mantissa >> 13;
        return static_cast<std::uint16_t>(sign | (halfExponent << 10) | halfMantissa);
    }

    std::vector<std::byte> encodeHalf(const std::vector<std::array<float, 4>>& texels)
    {
        std::vector<std::byte> data;
        data.reserve(texels.size() * 4u * 2u);
        for ( const std::array<float, 4>& texel : texels )
        {
            for ( const float value : texel )
            {
                const std::uint16_t bits = halfBits(value);
                data.push_back(static_cast<std::byte>(bits & 0xFFu));
                data.push_back(static_cast<std::byte>((bits >> 8) & 0xFFu));
            }
        }
        return data;
    }

    std::array<float, 4> sceneTexel(std::uint32_t x, std::uint32_t y)
    {
        const std::uint32_t index = y * 4u + x;
        // One value above 1.0 checks that the copy saturates.
        const float red = index == 15u ? 2.0f : static_cast<float>(index) / 16.0f;
        const float green = static_cast<float>(15u - index) / 16.0f;
        const float blue = static_cast<float>(x * 4u + y) / 16.0f;
        return { red, green, blue, 1.0f };
    }

    std::array<int, 4> saturatedByte(const std::array<float, 4>& texel)
    {
        std::array<int, 4> result;
        for ( std::size_t channel = 0; channel < result.size(); ++channel )
        {
            float value = texel[channel];
            value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            result[channel] = static_cast<int>(value * 255.0f + 0.5f);
        }
        return result;
    }

    std::array<int, 4> pixelAt(const std::vector<std::byte>& pixels, std::uint32_t width,
        std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4u;
        return { static_cast<int>(pixels[index + 0u]), static_cast<int>(pixels[index + 1u]),
            static_cast<int>(pixels[index + 2u]), static_cast<int>(pixels[index + 3u]) };
    }

    // Runs a no-effect chain from the 4x4 input into a fresh output and reads it back.
    std::vector<std::byte> runCopy(Device& device, TextureHandle input, std::uint32_t width,
        std::uint32_t height)
    {
        const TextureHandle output = device.createTexture(TextureDesc{ width, height, 1,
            TextureFormat::Rgba8, TextureUsage::ColorTarget });
        REQUIRE_FALSE(output.isNull());

        Chain chain(device);
        REQUIRE(chain.open());

        FrameInputs frame;
        frame.color       = input;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 4;
        frame.height      = 4;
        OutputTarget target;
        target.texture = output;
        target.format  = TextureFormat::Rgba8;
        target.width   = width;
        target.height  = height;

        CommandList list = device.beginFrame();
        REQUIRE(chain.record(list, frame, target));
        REQUIRE(device.submit(list));

        std::vector<std::byte> pixels = device.readTexture(output, 0);
        device.destroyTexture(output);
        return pixels;
    }

    TEST_CASE("The chain copies and scales an Rgba16F scene into an Rgba8 target (GPU)")
    {
        babylon::render::test::keepGpuDriverLoaded();
        babylon::log::CaptureSink sink;
        const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
        if ( device == nullptr )
        {
            babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
            return;
        }

        std::vector<std::array<float, 4>> texels;
        texels.reserve(16);
        for ( std::uint32_t y = 0; y < 4; ++y )
        {
            for ( std::uint32_t x = 0; x < 4; ++x )
            {
                texels.push_back(sceneTexel(x, y));
            }
        }

        const TextureHandle input = device->createTexture(TextureDesc{ 4, 4, 1,
            TextureFormat::Rgba16F, TextureUsage::Sampled });
        REQUIRE_FALSE(input.isNull());
        REQUIRE(device->uploadTexture(input, 0, encodeHalf(texels)));

        // The same size: every texel is the saturated input rounded to 8 bits. The
        // per-texel comparison also proves the top-left orientation.
        const std::vector<std::byte> pixels = runCopy(*device, input, 4, 4);
        REQUIRE(pixels.size() == 4u * 4u * 4u);
        for ( std::uint32_t y = 0; y < 4; ++y )
        {
            for ( std::uint32_t x = 0; x < 4; ++x )
            {
                CHECK(pixelAt(pixels, 4, x, y) == saturatedByte(texels[y * 4u + x]));
            }
        }
        CHECK(pixelAt(pixels, 4, 0, 0) == saturatedByte(texels[0]));

        // An 8x8 output scales the image; the four corners come from the input's corners.
        const std::vector<std::byte> scaled = runCopy(*device, input, 8, 8);
        REQUIRE(scaled.size() == 8u * 8u * 4u);
        CHECK(pixelAt(scaled, 8, 0, 0) == saturatedByte(texels[0]));
        CHECK(pixelAt(scaled, 8, 7, 0) == saturatedByte(texels[3]));
        CHECK(pixelAt(scaled, 8, 0, 7) == saturatedByte(texels[12]));
        CHECK(pixelAt(scaled, 8, 7, 7) == saturatedByte(texels[15]));

        INFO("captured log: " << joined(sink));
        CHECK(sink.records().empty());

        device->destroyTexture(input);
    }
}
