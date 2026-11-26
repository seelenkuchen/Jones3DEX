// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/DirectionalShadowMap.h>

#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <babylon/shaders/babylon_shadow_tests.h>

#include "../../render/backends/sdl/tests/GpuRequirement.h"
#include "../../render/backends/sdl/tests/KeepGpuDriverLoaded.h"

#include <doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace babylon::render;
    using namespace babylon::shadow;

    const float kLight[] = { 0.0f, 1.0f, 0.0f };
    constexpr std::uint32_t kSize = 256;
    constexpr float kMargin = 0.2f;

    // The receive pass compares every map texel with a reference depth of 1,
    // so a stored caster texel (nearer the light) is blocked and a cleared
    // texel is lit.
    constexpr std::array<float, 4> kReference = { 1.0f, 0.0f, 0.0f, 0.0f };

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

    std::uint8_t redAt(const std::vector<std::byte>& pixels, std::uint32_t x, std::uint32_t y)
    {
        const std::size_t index = (static_cast<std::size_t>(y) * kSize + x) * 4u;
        return static_cast<std::uint8_t>(pixels[index]);
    }

    // One horizontal quad as two triangles, hovering above the receive area.
    // Its x and z extents differ, so a transposed matrix would block a
    // different region of the map; the margin gives the depth its range.
    std::vector<float> quadVertices()
    {
        const std::array<std::array<float, 3>, 4> corners = { {
            { -0.8f, 1.0f, -0.2f },
            { 0.8f, 1.0f, -0.2f },
            { 0.8f, 1.0f, 0.2f },
            { -0.8f, 1.0f, 0.2f },
        } };
        constexpr std::array<std::array<int, 3>, 2> triangles = { {
            { 0, 1, 2 },
            { 0, 2, 3 },
        } };

        std::vector<float> vertices;
        vertices.reserve(triangles.size() * 3 * 3);
        for ( const std::array<int, 3>& triangle : triangles )
        {
            for ( const int corner : triangle )
            {
                vertices.push_back(corners[static_cast<std::size_t>(corner)][0]);
                vertices.push_back(corners[static_cast<std::size_t>(corner)][1]);
                vertices.push_back(corners[static_cast<std::size_t>(corner)][2]);
            }
        }
        return vertices;
    }
}

TEST_CASE("A directional shadow map blocks texels under a caster (GPU)")
{
    babylon::render::test::keepGpuDriverLoaded();
    babylon::log::CaptureSink sink;
    const std::unique_ptr<Device> device = createSdlGpuDevice(nullptr);
    if ( device == nullptr )
    {
        babylon::render::test::noGpuDevice("no GPU device: " + joined(sink));
        return;
    }

    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kSize;
    if ( !map.open(desc) )
    {
        babylon::render::test::noGpuDevice("no sampled depth format: " + joined(sink));
        return;
    }
    REQUIRE(map.isOpen());
    CHECK(map.size() == kSize);

    // A quad far wider along view x than along view z, hovering above the
    // receive area.
    const std::vector<float> vertices = quadVertices();
    const CasterBatch batch{ vertices.data(), vertices.size() / 3 };
    const std::array<CasterBatch, 1> batches{ batch };

    const TextureHandle receive = device->createTexture(TextureDesc{ kSize, kSize, 1,
        TextureFormat::Rgba8, TextureUsage::ColorTarget });
    REQUIRE_FALSE(receive.isNull());

    PipelineDesc receiveDesc;
    receiveDesc.vertexShader   = babylon::shadow::test::shaders::shadowReceiveVert();
    receiveDesc.fragmentShader = babylon::shadow::test::shaders::shadowReceiveFrag();
    receiveDesc.colorTargets   = { ColorTargetDesc{ TextureFormat::Rgba8, {} } };
    const PipelineHandle receivePipeline = device->createPipeline(receiveDesc);
    REQUIRE_FALSE(receivePipeline.isNull());

    CommandList list = device->beginFrame();
    REQUIRE(map.record(list, kLight, std::span<const CasterBatch>(batches), kMargin));

    PassDesc pass;
    pass.colorTargets = { ColorAttachment{ receive, 0, LoadOp::Clear, StoreOp::Store,
        ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f } } };
    REQUIRE(list.beginPass(pass));
    list.setPipeline(receivePipeline);
    list.bindTexture(ShaderStage::Fragment, 0, map.texture(), map.compareSampler());
    list.pushUniforms(ShaderStage::Fragment, 0, std::as_bytes(std::span(kReference)));
    list.draw(3);
    REQUIRE(list.endPass());
    REQUIRE(device->submit(list));
    CHECK(map.isValid());

    const std::vector<std::byte> pixels = device->readTexture(receive, 0);
    REQUIRE(pixels.size() == static_cast<std::size_t>(kSize) * kSize * 4u);

    // Under the caster the stored depth is nearer than the reference, so the
    // comparison fails and the receive pass writes black.
    CHECK(redAt(pixels, kSize / 2, kSize / 2) < 64);
    CHECK(redAt(pixels, 180, 128) < 64);

    // Outside the caster the map is cleared to 1, which passes, so it is lit.
    // (128, 230) is outside the narrow z footprint; (12, 128) is outside the
    // wide x footprint.
    CHECK(redAt(pixels, 128, 230) > 191);
    CHECK(redAt(pixels, 12, 128) > 191);

    device->destroyPipeline(receivePipeline);
    device->destroyTexture(receive);

    INFO("captured log: " << joined(sink));
    CHECK(sink.records().empty());
}
