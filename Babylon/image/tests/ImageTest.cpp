// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/image/Image.h>

#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <vector>

#ifndef BABYLON_IMAGE_TEST_PNG
#error "BABYLON_IMAGE_TEST_PNG must name the committed Xelu prompt"
#endif

namespace
{

    std::vector<std::byte> readPng()
    {
        // fopen_s on MSVC (fopen is deprecated there and warnings are errors), fopen elsewhere.
        std::FILE* pFile = nullptr;
#ifdef _MSC_VER
        REQUIRE(fopen_s(&pFile, BABYLON_IMAGE_TEST_PNG, "rb") == 0);
#else
        pFile = std::fopen(BABYLON_IMAGE_TEST_PNG, "rb");
#endif
        REQUIRE(pFile);
        REQUIRE(std::fseek(pFile, 0, SEEK_END) == 0);
        const long size = std::ftell(pFile);
        REQUIRE(size >= 0);
        REQUIRE(std::fseek(pFile, 0, SEEK_SET) == 0);

        std::vector<std::byte> data((std::size_t)size);
        if ( !data.empty() )
        {
            REQUIRE(std::fread(data.data(), 1, data.size(), pFile) == data.size());
        }
        std::fclose(pFile);
        return data;
    }

    // A PNG signature and IHDR naming 9000 x 1 pixels. stb_image reads the
    // header before it decodes anything, so this exercises the size limit.
    std::vector<std::byte> oversizedPngHeader()
    {
        const unsigned char header[] = {
            0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 'I', 'H', 'D', 'R',
            0x00, 0x00, 0x23, 0x28,
            0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        };

        std::vector<std::byte> data(sizeof(header));
        for ( std::size_t index = 0; index < sizeof(header); ++index )
        {
            data[index] = (std::byte)header[index];
        }
        return data;
    }

}

TEST_CASE("the committed Xelu prompt decodes to 100 x 100 RGBA8")
{
    const std::vector<std::byte> bytes = readPng();
    const std::optional<babylon::image::Image> image = babylon::image::decodePng(bytes);
    REQUIRE(image.has_value());

    CHECK(image->width == 100u);
    CHECK(image->height == 100u);
    REQUIRE(image->pixels.size() == 100u * 100u * 4u);

    bool hasTransparent = false;
    bool hasOpaque = false;
    for ( std::size_t index = 3; index < image->pixels.size(); index += 4 )
    {
        hasTransparent = hasTransparent || image->pixels[index] == 0;
        hasOpaque      = hasOpaque || image->pixels[index] == 255;
    }
    CHECK(hasTransparent);
    CHECK(hasOpaque);
}

TEST_CASE("truncated, garbage and oversized PNGs are rejected")
{
    std::vector<std::byte> bytes = readPng();
    bytes.resize(bytes.size() / 2u);
    CHECK_FALSE(babylon::image::decodePng(bytes).has_value());

    std::vector<std::byte> garbage(256, (std::byte)0x5A);
    CHECK_FALSE(babylon::image::decodePng(garbage).has_value());

    const std::vector<std::byte> oversized = oversizedPngHeader();
    CHECK_FALSE(babylon::image::decodePng(oversized).has_value());

    CHECK_FALSE(babylon::image::decodePng({}).has_value());
}
