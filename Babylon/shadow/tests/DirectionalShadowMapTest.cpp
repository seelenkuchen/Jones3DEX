// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/DirectionalShadowMap.h>
#include <babylon/shadow/ShadowMath.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Pipeline.h>
#include <babylon/render/Sampler.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <doctest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace
{
    using namespace babylon::render;
    using namespace babylon::shadow;

    const float kLight[] = { 0.0f, 1.0f, 0.0f };
    constexpr std::uint32_t kMapSize = 256;

    // A triangle that spans all three axes, so the light fit is not degenerate.
    std::vector<float> smallTriangle()
    {
        return { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f };
    }

    Bounds boundsOf(const std::vector<float>& vertices)
    {
        Bounds bounds = {
            { vertices[0], vertices[1], vertices[2] },
            { vertices[0], vertices[1], vertices[2] },
        };
        for ( std::size_t index = 0; index + 2 < vertices.size(); index += 3 )
        {
            for ( int axis = 0; axis < 3; ++axis )
            {
                const float value = vertices[index + static_cast<std::size_t>(axis)];
                bounds.min[axis] = value < bounds.min[axis] ? value : bounds.min[axis];
                bounds.max[axis] = value > bounds.max[axis] ? value : bounds.max[axis];
            }
        }
        return bounds;
    }

    template <typename T>
    std::vector<std::byte> bytesOf(const T& value)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(&value, 1));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    std::vector<std::byte> bytesOf(const std::vector<float>& values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(values));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    const PipelineCreated* findCreatedPipeline(const std::vector<RecordedCommand>& records)
    {
        for ( const RecordedCommand& record : records )
        {
            if ( const auto* pCreated = std::get_if<PipelineCreated>(&record) )
            {
                return pCreated;
            }
        }
        return nullptr;
    }

    // The commands of one shadow pass, in submit order.
    struct PassCommands
    {
        const UpdateBuffer* update = nullptr;
        const BeginPass* begin = nullptr;
        const SetPipeline* pipeline = nullptr;
        const BindVertexBuffer* vertexBuffer = nullptr;
        const PushUniforms* uniforms = nullptr;
        const Draw* draw = nullptr;
        const EndPass* end = nullptr;
    };

    PassCommands findPass(const std::vector<RecordedCommand>& records)
    {
        PassCommands found;
        for ( const RecordedCommand& record : records )
        {
            if ( const auto* pValue = std::get_if<UpdateBuffer>(&record) )
            {
                found.update = pValue;
            }
            else if ( const auto* pValue = std::get_if<BeginPass>(&record) )
            {
                found.begin = pValue;
            }
            else if ( const auto* pValue = std::get_if<SetPipeline>(&record) )
            {
                found.pipeline = pValue;
            }
            else if ( const auto* pValue = std::get_if<BindVertexBuffer>(&record) )
            {
                found.vertexBuffer = pValue;
            }
            else if ( const auto* pValue = std::get_if<PushUniforms>(&record) )
            {
                found.uniforms = pValue;
            }
            else if ( const auto* pValue = std::get_if<Draw>(&record) )
            {
                found.draw = pValue;
            }
            else if ( const auto* pValue = std::get_if<EndPass>(&record) )
            {
                found.end = pValue;
            }
        }
        return found;
    }

    // Records one batch and submits the frame, returning the pass commands.
    PassCommands recordAndSubmit(NullDevice& device, DirectionalShadowMap& map,
        const std::vector<float>& vertices)
    {
        device.clearRecorded();
        const CasterBatch batch{ vertices.data(), vertices.size() / 3 };
        const std::array<CasterBatch, 1> batches{ batch };
        CommandList list = device.beginFrame();
        REQUIRE(map.record(list, kLight, std::span<const CasterBatch>(batches)));
        REQUIRE(device.submit(list));
        return findPass(device.recorded());
    }
}

TEST_CASE("DirectionalShadowMap opens and closes")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);

    map.close();
    CHECK_FALSE(map.isOpen());
    CHECK_FALSE(map.isValid());
    CHECK(map.texture().isNull());
    CHECK(map.compareSampler().isNull());
    CHECK(map.size() == 0);

    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));
    CHECK(map.isOpen());
    CHECK_FALSE(map.isValid());
    CHECK_FALSE(map.texture().isNull());
    CHECK_FALSE(map.compareSampler().isNull());
    CHECK(map.size() == kMapSize);

    const std::optional<SamplerDesc> sampler = device->samplerDesc(map.compareSampler());
    REQUIRE(sampler.has_value());
    CHECK(sampler->minFilter == Filter::Linear);
    CHECK(sampler->magFilter == Filter::Linear);
    CHECK(sampler->addressU == AddressMode::ClampToEdge);
    CHECK(sampler->addressV == AddressMode::ClampToEdge);
    REQUIRE(sampler->compare.has_value());
    CHECK(*sampler->compare == CompareOp::LessOrEqual);

    map.close();
    CHECK_FALSE(map.isOpen());
    CHECK_FALSE(map.isValid());
    CHECK(map.texture().isNull());
    map.close();

    ShadowMapDesc tooSmall;
    tooSmall.size = 128;
    CHECK_FALSE(map.open(tooSmall));
    CHECK_FALSE(map.isOpen());
}

TEST_CASE("record with no vertices fails and records nothing")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    CommandList list = device->beginFrame();
    CHECK_FALSE(map.record(list, kLight, {}));
    CHECK(list.commands().empty());
    CHECK_FALSE(map.isValid());

    const CasterBatch empty{ nullptr, 0 };
    const std::array<CasterBatch, 1> batches{ empty };
    CommandList second = device->beginFrame();
    CHECK_FALSE(map.record(second, kLight, std::span<const CasterBatch>(batches)));
    CHECK(second.commands().empty());
    CHECK_FALSE(map.isValid());
}

TEST_CASE("record fits the light, uploads once and draws the casters")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const PipelineCreated* const created = findCreatedPipeline(device->recorded());
    REQUIRE(created != nullptr);
    CHECK(created->desc.colorTargets.empty());
    REQUIRE(created->desc.depthFormat.has_value());
    CHECK(*created->desc.depthFormat == TextureFormat::D32F);
    CHECK(created->desc.depth.testEnabled);
    CHECK(created->desc.depth.writeEnabled);
    CHECK(created->desc.depth.compare == CompareOp::LessOrEqual);
    CHECK(created->desc.depth.depthBiasConstant == doctest::Approx(desc.depthBiasConstant));
    CHECK(created->desc.depth.depthBiasSlope == doctest::Approx(desc.depthBiasSlope));
    CHECK(created->desc.cull == CullMode::None);
    REQUIRE(created->desc.vertexLayout.bufferStrides.size() == 1);
    CHECK(created->desc.vertexLayout.bufferStrides[0] == 3 * sizeof(float));
    REQUIRE(created->desc.vertexLayout.attributes.size() == 1);
    CHECK(created->desc.vertexLayout.attributes[0].location == 0);
    CHECK(created->desc.vertexLayout.attributes[0].format == VertexFormat::Float3);
    const PipelineHandle createdPipeline = created->pipeline;

    const std::vector<float> vertices = smallTriangle();
    const std::vector<std::byte> vertexBytes = bytesOf(vertices);
    const PassCommands pass = recordAndSubmit(*device, map, vertices);
    CHECK(map.isValid());

    REQUIRE(pass.update != nullptr);
    CHECK(pass.update->offset == 0);
    CHECK(pass.update->data == vertexBytes);

    REQUIRE(pass.begin != nullptr);
    CHECK(pass.begin->desc.colorTargets.empty());
    REQUIRE(pass.begin->desc.depthTarget.has_value());
    const DepthAttachment& depth = *pass.begin->desc.depthTarget;
    CHECK(depth.target == map.texture());
    CHECK(depth.load == LoadOp::Clear);
    CHECK(depth.store == StoreOp::Store);
    CHECK(depth.clearDepth == doctest::Approx(1.0f));

    REQUIRE(pass.pipeline != nullptr);
    CHECK(pass.pipeline->pipeline == createdPipeline);

    REQUIRE(pass.vertexBuffer != nullptr);
    CHECK(pass.vertexBuffer->slot == 0);
    CHECK(pass.vertexBuffer->offset == 0);
    CHECK_FALSE(pass.vertexBuffer->buffer.isNull());

    REQUIRE(pass.uniforms != nullptr);
    CHECK(pass.uniforms->stage == ShaderStage::Vertex);
    CHECK(pass.uniforms->slot == 0);
    Matrix4 expected{};
    REQUIRE(fitDirectional(kLight, boundsOf(vertices), 0.05f, kMapSize, &expected));
    CHECK(pass.uniforms->data == bytesOf(expected));

    REQUIRE(pass.draw != nullptr);
    CHECK(pass.draw->vertexCount == vertices.size() / 3);
    CHECK(pass.draw->firstVertex == 0);
    CHECK(pass.end != nullptr);

    const Matrix4 expectedTexture = multiply(shadowClipToTexture(), expected);
    const Matrix4 actualTexture = map.viewToShadowTexture();
    for ( int index = 0; index < 16; ++index )
    {
        CHECK(actualTexture.m[index] == doctest::Approx(expectedTexture.m[index]));
    }
}

TEST_CASE("a larger frame grows the vertex buffer")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const std::vector<float> vertices = smallTriangle();
    const PassCommands first = recordAndSubmit(*device, map, vertices);
    REQUIRE(first.vertexBuffer != nullptr);
    const BufferHandle firstBuffer = first.vertexBuffer->buffer;

    // 2000 copies of the triangle: 6000 vertices, past the initial capacity.
    std::vector<float> large;
    large.reserve(vertices.size() * 2000);
    for ( int copy = 0; copy < 2000; ++copy )
    {
        large.insert(large.end(), vertices.begin(), vertices.end());
    }
    const PassCommands second = recordAndSubmit(*device, map, large);
    REQUIRE(second.vertexBuffer != nullptr);
    CHECK(second.vertexBuffer->buffer != firstBuffer);
    REQUIRE(second.draw != nullptr);
    CHECK(second.draw->vertexCount == large.size() / 3);
    CHECK(map.isValid());
}

TEST_CASE("isValid follows the last record and failures warn once")
{
    babylon::log::CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));
    sink.clear();
    CHECK_FALSE(map.isValid());

    CommandList first = device->beginFrame();
    CHECK_FALSE(map.record(first, kLight, {}));
    CHECK_FALSE(map.isValid());
    CommandList second = device->beginFrame();
    CHECK_FALSE(map.record(second, kLight, {}));
    CHECK(sink.records().size() == 1);

    const std::vector<float> vertices = smallTriangle();
    const CasterBatch batch{ vertices.data(), vertices.size() / 3 };
    const std::array<CasterBatch, 1> batches{ batch };
    CommandList third = device->beginFrame();
    REQUIRE(map.record(third, kLight, std::span<const CasterBatch>(batches)));
    CHECK(map.isValid());

    sink.clear();
    CommandList fourth = device->beginFrame();
    CHECK_FALSE(map.record(fourth, kLight, {}));
    CommandList fifth = device->beginFrame();
    CHECK_FALSE(map.record(fifth, kLight, {}));
    CHECK(sink.records().size() == 1);
    CHECK_FALSE(map.isValid());

    map.close();
    CHECK_FALSE(map.isValid());
}
