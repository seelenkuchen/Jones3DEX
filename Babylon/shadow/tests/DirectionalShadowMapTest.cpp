// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/DirectionalShadowMap.h>
#include <babylon/shadow/ShadowMath.h>
#include <babylon/shadow/StaticCasterSet.h>

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
            if ( const auto* pUpdate = std::get_if<UpdateBuffer>(&record) )
            {
                found.update = pUpdate;
            }
            else if ( const auto* pBegin = std::get_if<BeginPass>(&record) )
            {
                found.begin = pBegin;
            }
            else if ( const auto* pPipeline = std::get_if<SetPipeline>(&record) )
            {
                found.pipeline = pPipeline;
            }
            else if ( const auto* pVertexBuffer = std::get_if<BindVertexBuffer>(&record) )
            {
                found.vertexBuffer = pVertexBuffer;
            }
            else if ( const auto* pUniforms = std::get_if<PushUniforms>(&record) )
            {
                found.uniforms = pUniforms;
            }
            else if ( const auto* pDraw = std::get_if<Draw>(&record) )
            {
                found.draw = pDraw;
            }
            else if ( const auto* pEnd = std::get_if<EndPass>(&record) )
            {
                found.end = pEnd;
            }
        }
        return found;
    }

    // One draw with the vertex buffer and uniform bound just before it.
    struct RecordedDraw
    {
        BufferHandle buffer;
        std::vector<std::byte> uniforms;
        Draw draw;
    };

    std::vector<RecordedDraw> drawsOf(const std::vector<RecordedCommand>& records)
    {
        std::vector<RecordedDraw> draws;
        BufferHandle buffer;
        std::vector<std::byte> uniforms;
        for ( const RecordedCommand& record : records )
        {
            if ( const auto* pBound = std::get_if<BindVertexBuffer>(&record) )
            {
                buffer = pBound->buffer;
            }
            else if ( const auto* pUniforms = std::get_if<PushUniforms>(&record) )
            {
                uniforms = pUniforms->data;
            }
            else if ( const auto* pDraw = std::get_if<Draw>(&record) )
            {
                draws.push_back({ buffer, uniforms, *pDraw });
            }
        }
        return draws;
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
    CHECK_FALSE(created->desc.depthClip);
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

TEST_CASE("open maps clampCastersToNearPlane onto the caster pipeline")
{
    for ( const bool clamp : { true, false } )
    {
        const std::unique_ptr<NullDevice> device = createNullDevice();
        DirectionalShadowMap map(*device);
        ShadowMapDesc desc;
        desc.size = kMapSize;
        desc.clampCastersToNearPlane = clamp;
        INFO("clampCastersToNearPlane = " << clamp);
        REQUIRE(map.open(desc));

        const PipelineCreated* const created = findCreatedPipeline(device->recorded());
        REQUIRE(created != nullptr);
        CHECK(created->desc.depthClip == !clamp);
    }
}

TEST_CASE("record draws a static batch from its set without uploading it")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const std::vector<float> vertices = smallTriangle();
    const CasterBatch dynamic{ vertices.data(), vertices.size() / 3 };

    // A static set in world space, moved into view space by its batch.
    StaticCasterSet set(*device);
    const std::vector<float> staticPositions = { -1.0f, 4.0f, -1.0f, 1.0f, 4.0f, -1.0f,
        0.0f, 4.0f, 1.0f };
    REQUIRE(set.upload(staticPositions));
    Matrix4 toView = identity();
    toView.m[3]  = 2.0f;
    toView.m[7]  = 1.0f;
    toView.m[11] = -3.0f;
    CasterBatch staticBatch;
    staticBatch.pStatic = &set;
    staticBatch.toView = toView;

    const std::array<CasterBatch, 2> batches{ dynamic, staticBatch };
    device->clearRecorded();
    CommandList list = device->beginFrame();
    REQUIRE(map.record(list, kLight, std::span<const CasterBatch>(batches)));
    REQUIRE(device->submit(list));
    CHECK(map.isValid());

    // Only the per-frame batch is uploaded.
    std::size_t updates = 0;
    const UpdateBuffer* pUpdate = nullptr;
    for ( const RecordedCommand& record : device->recorded() )
    {
        if ( const auto* pFound = std::get_if<UpdateBuffer>(&record) )
        {
            ++updates;
            pUpdate = pFound;
        }
    }
    CHECK(updates == 1);
    REQUIRE(pUpdate != nullptr);
    CHECK(pUpdate->offset == 0);
    CHECK(pUpdate->data == bytesOf(vertices));

    const std::vector<RecordedDraw> draws = drawsOf(device->recorded());
    REQUIRE(draws.size() == 2);
    CHECK(draws[0].buffer != set.buffer());
    CHECK(draws[0].draw.vertexCount == vertices.size() / 3);
    CHECK(draws[0].draw.firstVertex == 0);
    CHECK(draws[1].buffer == set.buffer());
    CHECK(draws[1].draw.vertexCount == staticPositions.size() / 3);
    CHECK(draws[1].draw.firstVertex == 0);

    Matrix4 fit{};
    REQUIRE(fitDirectional(kLight, boundsOf(vertices), 0.05f, kMapSize, &fit,
        desc.staticReach));
    CHECK(draws[0].uniforms == bytesOf(fit));
    CHECK(draws[1].uniforms == bytesOf(multiply(fit, toView)));

    // The depth range reaches staticReach toward the light beyond the dynamic
    // box: just inside maps into the map, one unit farther is clipped.
    const Bounds dynamicBounds = boundsOf(vertices);
    const float nearestToLight = dynamicBounds.max[1]; // the light points up
    const float within[3] = { 0.5f, nearestToLight + desc.staticReach - 0.001f, 0.5f };
    const float beyond[3] = { 0.5f, nearestToLight + desc.staticReach + 1.0f, 0.5f };
    float projected[4];
    transformPoint(fit, within, projected);
    CHECK(projected[2] >= 0.0f);
    transformPoint(fit, beyond, projected);
    CHECK(projected[2] < 0.0f);
}

TEST_CASE("a fit-only batch fits like a drawn batch without uploading or drawing it")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const std::vector<float> vertices = smallTriangle();
    const CasterBatch drawn{ vertices.data(), vertices.size() / 3 };
    const std::array<CasterBatch, 1> drawnBatches{ drawn };
    CommandList first = device->beginFrame();
    REQUIRE(map.record(first, kLight, std::span<const CasterBatch>(drawnBatches)));
    REQUIRE(device->submit(first));
    const std::vector<std::byte> drawnFit = bytesOf(map.viewToShadowTexture());

    CasterBatch fitOnly = drawn;
    fitOnly.fitOnly = true;
    const std::array<CasterBatch, 1> fitOnlyBatches{ fitOnly };
    device->clearRecorded();
    CommandList second = device->beginFrame();
    REQUIRE(map.record(second, kLight, std::span<const CasterBatch>(fitOnlyBatches)));
    REQUIRE(device->submit(second));
    CHECK(map.isValid());

    // The same vertices produce the same fit, but none of them is uploaded or
    // drawn: the pass is empty apart from its clear.
    CHECK(bytesOf(map.viewToShadowTexture()) == drawnFit);
    const PassCommands pass = findPass(device->recorded());
    CHECK(pass.update == nullptr);
    CHECK(pass.begin != nullptr);
    CHECK(pass.end != nullptr);
    CHECK(pass.draw == nullptr);
    CHECK(drawsOf(device->recorded()).empty());
}

TEST_CASE("a fit-only batch with a static set draws only the static set")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const std::vector<float> vertices = smallTriangle();
    CasterBatch fitOnly{ vertices.data(), vertices.size() / 3 };
    fitOnly.fitOnly = true;

    StaticCasterSet set(*device);
    const std::vector<float> staticPositions = { -1.0f, 4.0f, -1.0f, 1.0f, 4.0f, -1.0f,
        0.0f, 4.0f, 1.0f };
    REQUIRE(set.upload(staticPositions));
    CasterBatch staticBatch;
    staticBatch.pStatic = &set;

    const std::array<CasterBatch, 2> batches{ fitOnly, staticBatch };
    device->clearRecorded();
    CommandList list = device->beginFrame();
    REQUIRE(map.record(list, kLight, std::span<const CasterBatch>(batches)));
    REQUIRE(device->submit(list));
    CHECK(map.isValid());

    std::size_t updates = 0;
    for ( const RecordedCommand& record : device->recorded() )
    {
        if ( std::get_if<UpdateBuffer>(&record) != nullptr )
        {
            ++updates;
        }
    }
    CHECK(updates == 0);

    const std::vector<RecordedDraw> draws = drawsOf(device->recorded());
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].buffer == set.buffer());
    CHECK(draws[0].draw.vertexCount == staticPositions.size() / 3);
    CHECK(draws[0].draw.firstVertex == 0);

    // The fit-only batch still defines the fit, and the static set widens the
    // depth range by staticReach.
    Matrix4 fit{};
    REQUIRE(fitDirectional(kLight, boundsOf(vertices), 0.05f, kMapSize, &fit,
        desc.staticReach));
    CHECK(draws[0].uniforms == bytesOf(fit));
}

TEST_CASE("a fit-only batch beside a drawn one fits without being uploaded")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const std::vector<float> fitVertices = smallTriangle();
    const std::vector<float> drawnVertices = { 2.0f, 0.0f, 0.0f, 3.0f, 0.0f, 0.0f, 2.0f,
        1.0f, 1.0f };
    CasterBatch fitOnly{ fitVertices.data(), fitVertices.size() / 3 };
    fitOnly.fitOnly = true;
    const CasterBatch drawn{ drawnVertices.data(), drawnVertices.size() / 3 };

    const std::array<CasterBatch, 2> batches{ fitOnly, drawn };
    device->clearRecorded();
    CommandList list = device->beginFrame();
    REQUIRE(map.record(list, kLight, std::span<const CasterBatch>(batches)));
    REQUIRE(device->submit(list));
    CHECK(map.isValid());

    const PassCommands pass = findPass(device->recorded());
    REQUIRE(pass.update != nullptr);
    CHECK(pass.update->data == bytesOf(drawnVertices));

    const std::vector<RecordedDraw> draws = drawsOf(device->recorded());
    REQUIRE(draws.size() == 1);
    CHECK(draws[0].draw.vertexCount == drawnVertices.size() / 3);
    CHECK(draws[0].draw.firstVertex == 0);

    Bounds unionBounds = boundsOf(fitVertices);
    const Bounds drawnBounds = boundsOf(drawnVertices);
    for ( int axis = 0; axis < 3; ++axis )
    {
        unionBounds.min[axis] = drawnBounds.min[axis] < unionBounds.min[axis]
            ? drawnBounds.min[axis] : unionBounds.min[axis];
        unionBounds.max[axis] = drawnBounds.max[axis] > unionBounds.max[axis]
            ? drawnBounds.max[axis] : unionBounds.max[axis];
    }
    Matrix4 fit{};
    REQUIRE(fitDirectional(kLight, unionBounds, 0.05f, kMapSize, &fit));
    CHECK(draws[0].uniforms == bytesOf(fit));
}

TEST_CASE("a static batch cannot be fit-only")
{
    babylon::log::CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    StaticCasterSet set(*device);
    REQUIRE(set.upload(smallTriangle()));
    CasterBatch staticBatch;
    staticBatch.pStatic = &set;
    staticBatch.fitOnly = true;
    const std::array<CasterBatch, 1> batches{ staticBatch };

    sink.clear();
    CommandList list = device->beginFrame();
    CHECK_FALSE(map.record(list, kLight, std::span<const CasterBatch>(batches)));
    CHECK(list.commands().empty());
    CHECK_FALSE(map.isValid());
    CHECK(sink.records().size() == 1);

    // The usual one-time warning: a second failure stays silent.
    CommandList second = device->beginFrame();
    CHECK_FALSE(map.record(second, kLight, std::span<const CasterBatch>(batches)));
    CHECK(sink.records().size() == 1);
}

TEST_CASE("staticReach extends the depth range only when a static batch is present")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    DirectionalShadowMap map(*device);
    ShadowMapDesc desc;
    desc.size = kMapSize;
    REQUIRE(map.open(desc));

    const std::vector<float> vertices = smallTriangle();
    const CasterBatch dynamic{ vertices.data(), vertices.size() / 3 };

    StaticCasterSet set(*device);
    const std::vector<float> staticPositions = { 2.0f, 4.0f, 2.0f, 3.0f, 4.0f, 2.0f, 2.0f,
        4.0f, 3.0f };
    REQUIRE(set.upload(staticPositions));
    CasterBatch staticBatch;
    staticBatch.pStatic = &set;

    const std::array<CasterBatch, 1> dynamicOnly{ dynamic };
    device->clearRecorded();
    CommandList first = device->beginFrame();
    REQUIRE(map.record(first, kLight, std::span<const CasterBatch>(dynamicOnly)));
    REQUIRE(device->submit(first));
    const std::vector<RecordedDraw> firstDraws = drawsOf(device->recorded());

    const std::array<CasterBatch, 2> withStatic{ dynamic, staticBatch };
    device->clearRecorded();
    CommandList second = device->beginFrame();
    REQUIRE(map.record(second, kLight, std::span<const CasterBatch>(withStatic)));
    REQUIRE(device->submit(second));
    const std::vector<RecordedDraw> secondDraws = drawsOf(device->recorded());

    REQUIRE(firstDraws.size() == 1);
    REQUIRE(secondDraws.size() == 2);
    Matrix4 tight{};
    Matrix4 extended{};
    REQUIRE(fitDirectional(kLight, boundsOf(vertices), 0.05f, kMapSize, &tight));
    REQUIRE(fitDirectional(kLight, boundsOf(vertices), 0.05f, kMapSize, &extended,
        desc.staticReach));
    CHECK(firstDraws[0].uniforms == bytesOf(tight));
    CHECK(secondDraws[0].uniforms == bytesOf(extended));
    CHECK(firstDraws[0].uniforms != secondDraws[0].uniforms);

    // A point beyond the dynamic-only range but inside staticReach is clipped
    // by the tight map and kept by the extended one.
    const Bounds dynamicBounds = boundsOf(vertices);
    const float nearestToLight = dynamicBounds.max[1]; // the light points up
    const float point[3] = { 0.5f, nearestToLight + desc.staticReach - 1.0f, 0.5f };
    float projected[4];
    transformPoint(tight, point, projected);
    CHECK(projected[2] < 0.0f);
    transformPoint(extended, point, projected);
    CHECK(projected[2] >= 0.0f);
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
