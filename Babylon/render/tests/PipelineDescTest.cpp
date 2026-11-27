// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/render/Device.h>
#include <babylon/render/Handle.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Pipeline.h>

#include <babylon/log/CaptureSink.h>

#include <doctest.h>

#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

namespace
{
    using babylon::log::CaptureSink;
    using namespace babylon::render;

    ShaderDesc shader(unsigned tag)
    {
        ShaderDesc desc;
        desc.blobs.push_back(ShaderBlob{ ShaderFormat::SpirV,
            { std::byte{ 0x03 }, std::byte{ 0x02 }, std::byte{ 0x23 },
                static_cast<std::byte>(tag) }, "main" });
        return desc;
    }

    // A depth-only pipeline, so a pass needs no color target.
    PipelineDesc depthPipeline()
    {
        PipelineDesc desc;
        desc.vertexShader = shader(1);
        desc.fragmentShader = shader(2);
        desc.depthFormat = TextureFormat::D32F;
        desc.depth = DepthState{ true, true, CompareOp::LessOrEqual };
        return desc;
    }
}

TEST_CASE("Pipeline desc equality and the Null record include depth clip")
{
    CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    const PipelineDesc clipped = depthPipeline();
    PipelineDesc unclipped = clipped;
    unclipped.depthClip = false;
    CHECK(clipped.depthClip);
    CHECK_FALSE(unclipped.depthClip);
    CHECK_FALSE(clipped == unclipped);

    const PipelineHandle clippedPipeline = device->createPipeline(clipped);
    const PipelineHandle unclippedPipeline = device->createPipeline(unclipped);
    REQUIRE_FALSE(clippedPipeline.isNull());
    REQUIRE_FALSE(unclippedPipeline.isNull());
    CHECK(clippedPipeline != unclippedPipeline);
    CHECK(sink.records().empty());

    REQUIRE(device->recorded().size() == 2);
    const auto* const pClipped = std::get_if<PipelineCreated>(&device->recorded()[0]);
    const auto* const pUnclipped = std::get_if<PipelineCreated>(&device->recorded()[1]);
    REQUIRE(pClipped != nullptr);
    REQUIRE(pUnclipped != nullptr);
    CHECK(pClipped->desc.depthClip);
    CHECK_FALSE(pUnclipped->desc.depthClip);
    CHECK_FALSE(pClipped->desc == pUnclipped->desc);
}
