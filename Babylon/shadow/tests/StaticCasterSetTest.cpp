// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/shadow/StaticCasterSet.h>

#include <babylon/render/Buffer.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>

#include <babylon/log/CaptureSink.h>

#include <doctest.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace
{
    using namespace babylon::render;
    using namespace babylon::shadow;

    // Two triangles: a quad from (-1, 0, -2) to (1, 3, 2).
    std::vector<float> quad()
    {
        return {
            -1.0f, 0.0f, -2.0f,
             1.0f, 0.0f, -2.0f,
             1.0f, 3.0f,  2.0f,
            -1.0f, 0.0f, -2.0f,
             1.0f, 3.0f,  2.0f,
            -1.0f, 3.0f,  2.0f,
        };
    }

    std::vector<std::byte> bytesOf(const std::vector<float>& values)
    {
        const std::span<const std::byte> bytes = std::as_bytes(std::span(values));
        return std::vector<std::byte>(bytes.begin(), bytes.end());
    }

    bool boundsAreZero(const Bounds& bounds)
    {
        for ( int axis = 0; axis < 3; ++axis )
        {
            if ( bounds.min[axis] != 0.0f || bounds.max[axis] != 0.0f )
            {
                return false;
            }
        }
        return true;
    }
}

TEST_CASE("StaticCasterSet uploads triangles and reports their world bounds")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    StaticCasterSet set(*device);

    CHECK(set.numVertices() == 0);
    CHECK(set.buffer().isNull());
    CHECK(boundsAreZero(set.bounds()));

    const std::vector<float> vertices = quad();
    REQUIRE(set.upload(vertices));
    CHECK(set.numVertices() == vertices.size() / 3);
    REQUIRE_FALSE(set.buffer().isNull());

    const Bounds bounds = set.bounds();
    CHECK(bounds.min[0] == doctest::Approx(-1.0f));
    CHECK(bounds.min[1] == doctest::Approx(0.0f));
    CHECK(bounds.min[2] == doctest::Approx(-2.0f));
    CHECK(bounds.max[0] == doctest::Approx(1.0f));
    CHECK(bounds.max[1] == doctest::Approx(3.0f));
    CHECK(bounds.max[2] == doctest::Approx(2.0f));

    const std::vector<std::byte> stored = device->readBuffer(set.buffer(), 0,
        static_cast<std::uint32_t>(vertices.size() * sizeof(float)));
    CHECK(stored == bytesOf(vertices));

    // A second upload replaces the contents and the bounds.
    const std::vector<float> triangle = { 4.0f, 5.0f, 6.0f, 7.0f, 5.0f, 6.0f, 4.0f, 9.0f, 6.0f };
    REQUIRE(set.upload(triangle));
    CHECK(set.numVertices() == 3);
    const Bounds replaced = set.bounds();
    CHECK(replaced.min[0] == doctest::Approx(4.0f));
    CHECK(replaced.min[1] == doctest::Approx(5.0f));
    CHECK(replaced.max[1] == doctest::Approx(9.0f));
    const std::vector<std::byte> replacedBytes = device->readBuffer(set.buffer(), 0,
        static_cast<std::uint32_t>(triangle.size() * sizeof(float)));
    CHECK(replacedBytes == bytesOf(triangle));
}

TEST_CASE("StaticCasterSet refuses a size that is not whole triangles and keeps its contents")
{
    babylon::log::CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();
    StaticCasterSet set(*device);

    const std::vector<float> vertices = quad();
    REQUIRE(set.upload(vertices));
    const BufferHandle originalBuffer = set.buffer();

    const std::vector<float> notWhole = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    sink.clear();
    CHECK_FALSE(set.upload(notWhole));
    CHECK(sink.records().size() == 1);
    CHECK(set.numVertices() == vertices.size() / 3);
    CHECK(set.buffer() == originalBuffer);
    const std::vector<std::byte> stored = device->readBuffer(set.buffer(), 0,
        static_cast<std::uint32_t>(vertices.size() * sizeof(float)));
    CHECK(stored == bytesOf(vertices));
}

TEST_CASE("An empty upload clears a StaticCasterSet and clear releases its buffer")
{
    const std::unique_ptr<NullDevice> device = createNullDevice();
    StaticCasterSet set(*device);

    const std::vector<float> vertices = quad();
    REQUIRE(set.upload(vertices));

    CHECK(set.upload({}));
    CHECK(set.numVertices() == 0);
    CHECK(set.buffer().isNull());
    CHECK(boundsAreZero(set.bounds()));

    REQUIRE(set.upload(vertices));
    REQUIRE_FALSE(set.buffer().isNull());
    set.clear();
    CHECK(set.numVertices() == 0);
    CHECK(set.buffer().isNull());
    CHECK(boundsAreZero(set.bounds()));
    set.clear();
}

TEST_CASE("A destroyed StaticCasterSet releases its buffer")
{
    babylon::log::CaptureSink sink;
    const std::unique_ptr<NullDevice> device = createNullDevice();

    BufferHandle buffer;
    {
        StaticCasterSet set(*device);
        REQUIRE(set.upload(quad()));
        buffer = set.buffer();
        REQUIRE_FALSE(buffer.isNull());
    }

    sink.clear();
    CHECK(device->readBuffer(buffer, 0, 4).empty());
    CHECK(sink.records().size() == 1);
}
