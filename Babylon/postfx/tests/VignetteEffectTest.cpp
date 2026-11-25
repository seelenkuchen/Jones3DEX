// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/Chain.h>
#include <babylon/postfx/FrameInputs.h>
#include <babylon/postfx/VignetteEffect.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    template <typename T>
    std::vector<std::byte> bytesOf(const T& value)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(&value, 1));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    // The cbuffer of shaders/vignette.frag.hlsl, with no padding.
    struct alignas(16) ExpectedVignetteUniforms
    {
        float strength;
        float inner;
        float outer;
        float aspect;
    };
    static_assert(sizeof(ExpectedVignetteUniforms) == 16);

    struct PassRecord
    {
        TextureHandle target;
        std::vector<BindTexture> textures;
        std::vector<PushUniforms> uniforms;
        std::vector<Draw> draws;
    };

    // Each BeginPass and the commands recorded in it, in submit order.
    std::vector<PassRecord> passRecords(const std::vector<RecordedCommand>& record)
    {
        std::vector<PassRecord> passes;
        for ( const RecordedCommand& command : record )
        {
            if ( const auto* pBegin = std::get_if<BeginPass>(&command) )
            {
                PassRecord pass;
                if ( !pBegin->desc.colorTargets.empty() )
                {
                    pass.target = pBegin->desc.colorTargets.front().target;
                }
                passes.push_back(pass);
            }
            else if ( const auto* pBind = std::get_if<BindTexture>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().textures.push_back(*pBind);
                }
            }
            else if ( const auto* pPush = std::get_if<PushUniforms>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().uniforms.push_back(*pPush);
                }
            }
            else if ( const auto* pDraw = std::get_if<Draw>(&command) )
            {
                if ( !passes.empty() )
                {
                    passes.back().draws.push_back(*pDraw);
                }
            }
        }
        return passes;
    }

    void checkNames(const Chain& chain, std::initializer_list<std::string_view> expected)
    {
        const std::vector<std::string_view>& run = chain.lastRun();
        REQUIRE(run.size() == expected.size());
        std::size_t index = 0;
        for ( const std::string_view name : expected )
        {
            CHECK(run[index] == name);
            ++index;
        }
    }

    TextureHandle makeColor(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba16F,
            TextureUsage::Sampled | TextureUsage::ColorTarget });
    }

    TextureHandle makeOutput(NullDevice& device, std::uint32_t width, std::uint32_t height)
    {
        return device.createTexture(TextureDesc{ width, height, 1, TextureFormat::Rgba8,
            TextureUsage::ColorTarget });
    }

    TEST_CASE("Vignette settings clamp and control active")
    {
        VignetteEffect effect;
        CHECK(effect.name() == "vignette");
        CHECK(effect.stage() == Stage::Ldr);
        CHECK(effect.active());
        CHECK(effect.settings().strength == doctest::Approx(0.35f));
        CHECK(effect.settings().inner == doctest::Approx(0.45f));
        CHECK(effect.settings().outer == doctest::Approx(1.0f));

        effect.setSettings(VignetteSettings{ 2.0f, 0.2f, 0.1f });
        CHECK(effect.settings().strength == 1.0f);
        CHECK(effect.settings().inner == doctest::Approx(0.2f));
        CHECK(effect.settings().outer == doctest::Approx(0.201f));

        effect.setSettings(VignetteSettings{ -1.0f, 0.6f, 0.2f });
        CHECK(effect.settings().strength == 0.0f);
        CHECK_FALSE(effect.active());
        CHECK(effect.settings().outer == doctest::Approx(0.601f));

        effect.setSettings(VignetteSettings{ 0.5f, 0.5f, 0.9f });
        CHECK(effect.settings().strength == doctest::Approx(0.5f));
        CHECK(effect.settings().outer == doctest::Approx(0.9f));
    }

    TEST_CASE("Releasing vignette resources before they exist is safe")
    {
        VignetteEffect effect;
        effect.releaseResources();
        effect.releaseResources();
    }

    TEST_CASE("The vignette effect records its full-screen pass")
    {
        babylon::log::CaptureSink sink;
        NullDevice device;
        Chain chain(device);
        REQUIRE(chain.open());

        VignetteEffect effect;
        effect.setSettings(VignetteSettings{ 1.0f, 0.45f, 1.0f });
        REQUIRE(chain.add(effect));

        const TextureHandle color  = makeColor(device, 16, 16);
        const TextureHandle target = makeOutput(device, 16, 16);
        REQUIRE_FALSE(color.isNull());
        REQUIRE_FALSE(target.isNull());

        FrameInputs frame;
        frame.color       = color;
        frame.colorFormat = TextureFormat::Rgba16F;
        frame.width       = 16;
        frame.height      = 16;
        OutputTarget output;
        output.texture = target;
        output.format  = TextureFormat::Rgba8;
        output.width   = 16;
        output.height  = 16;

        device.clearRecorded();
        CommandList list = device.beginFrame();
        REQUIRE(chain.record(list, frame, output));
        REQUIRE(device.submit(list));

        const std::vector<PassRecord> passes = passRecords(device.recorded());
        REQUIRE(passes.size() == 2);
        CHECK(passes[0].target != target);
        CHECK(passes[1].target == target);

        REQUIRE(passes[1].textures.size() == 1);
        const BindTexture& bind = passes[1].textures.front();
        CHECK(bind.stage == ShaderStage::Fragment);
        CHECK(bind.slot == 0);
        CHECK(bind.texture == passes[0].target);
        const std::optional<SamplerDesc> sampler = device.samplerDesc(bind.sampler);
        REQUIRE(sampler.has_value());
        CHECK(sampler->minFilter == Filter::Linear);
        CHECK(sampler->magFilter == Filter::Linear);
        CHECK(sampler->addressU == AddressMode::ClampToEdge);
        CHECK(sampler->addressV == AddressMode::ClampToEdge);

        REQUIRE(passes[1].uniforms.size() == 1);
        const PushUniforms& push = passes[1].uniforms.front();
        CHECK(push.stage == ShaderStage::Fragment);
        CHECK(push.slot == 0);
        CHECK(push.data == bytesOf(ExpectedVignetteUniforms{ 1.0f, 0.45f, 1.0f, 1.0f }));

        REQUIRE(passes[1].draws.size() == 1);
        CHECK(passes[1].draws.front().vertexCount == 3);
        CHECK(passes[1].draws.front().firstVertex == 0);

        checkNames(chain, { "copy", "vignette" });
        CHECK(sink.records().empty());

        // Releasing after use and again is safe; the chain's close is a no-op on top.
        effect.releaseResources();
        effect.releaseResources();
        chain.close();
    }
}
