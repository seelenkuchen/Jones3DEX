// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/postfx/TargetPool.h>

#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/Texture.h>

#include <babylon/log/CaptureSink.h>

#include <doctest.h>

namespace
{
    using namespace babylon::postfx;
    using namespace babylon::render;

    TEST_CASE("The target pool hands out distinct targets in one frame")
    {
        NullDevice device;
        TargetPool pool(device);

        const TextureHandle first  = pool.acquire(16, 16, TextureFormat::Rgba8);
        const TextureHandle second = pool.acquire(16, 16, TextureFormat::Rgba8);
        CHECK_FALSE(first.isNull());
        CHECK_FALSE(second.isNull());
        CHECK(first != second);
        CHECK(pool.textureCount() == 2);
    }

    TEST_CASE("The target pool reuses an idle target after endFrame")
    {
        NullDevice device;
        TargetPool pool(device);

        const TextureHandle first = pool.acquire(16, 16, TextureFormat::Rgba8);
        REQUIRE_FALSE(first.isNull());
        pool.endFrame();

        const TextureHandle second = pool.acquire(16, 16, TextureFormat::Rgba8);
        CHECK(second == first);
        CHECK(pool.textureCount() == 1);
    }

    TEST_CASE("The target pool destroys a target unused for a whole frame")
    {
        NullDevice device;
        TargetPool pool(device);

        REQUIRE_FALSE(pool.acquire(16, 16, TextureFormat::Rgba8).isNull());
        pool.endFrame();
        CHECK(pool.textureCount() == 1);

        // No acquire during this frame: the next endFrame() frees the target.
        pool.endFrame();
        CHECK(pool.textureCount() == 0);
    }

    TEST_CASE("The target pool refuses bad sizes and depth formats")
    {
        babylon::log::CaptureSink sink;
        NullDevice device;
        TargetPool pool(device);

        CHECK(pool.acquire(0, 16, TextureFormat::Rgba8).isNull());
        CHECK(pool.acquire(16, 0, TextureFormat::Rgba8).isNull());
        CHECK(pool.acquire(16, 16, TextureFormat::D24S8).isNull());
        CHECK(pool.acquire(16, 16, TextureFormat::D32F).isNull());
        CHECK(pool.textureCount() == 0);
        CHECK(sink.records().size() == 4);
    }

    TEST_CASE("The target pool releaseAll empties it and stays usable")
    {
        NullDevice device;
        TargetPool pool(device);

        REQUIRE_FALSE(pool.acquire(16, 16, TextureFormat::Rgba8).isNull());
        REQUIRE_FALSE(pool.acquire(8, 8, TextureFormat::Rgba16F).isNull());
        CHECK(pool.textureCount() == 2);

        pool.releaseAll();
        CHECK(pool.textureCount() == 0);

        const TextureHandle fresh = pool.acquire(16, 16, TextureFormat::Rgba8);
        CHECK_FALSE(fresh.isNull());
        CHECK(pool.textureCount() == 1);
    }
}
