// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/font/Font.h>

#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#ifndef BABYLON_FONT_TEST_FONT
#error "BABYLON_FONT_TEST_FONT must name the committed Jost font"
#endif

namespace
{

    std::vector<std::byte> readFont()
    {
        std::FILE* pFile = std::fopen(BABYLON_FONT_TEST_FONT, "rb");
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

    bool overlaps(const babylon::font::Glyph& a, const babylon::font::Glyph& b)
    {
        if ( a.width == 0 || a.height == 0 || b.width == 0 || b.height == 0 )
        {
            return false;
        }

        return a.x < b.x + b.width && b.x < a.x + a.width
            && a.y < b.y + b.height && b.y < a.y + a.height;
    }

}

TEST_CASE("Jost at 32 px bakes a coverage atlas with usable metrics")
{
    const std::vector<std::byte> bytes = readFont();
    const std::optional<babylon::font::Font> font = babylon::font::Font::load(bytes);
    REQUIRE(font.has_value());

    std::vector<char32_t> codepoints;
    for ( char32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint )
    {
        codepoints.push_back(codepoint);
    }
    for ( char32_t codepoint = 0xA0; codepoint <= 0xFF; ++codepoint )
    {
        codepoints.push_back(codepoint);
    }

    const std::optional<babylon::font::FontAtlas> atlas =
        font->bake(32.0f, codepoints);
    REQUIRE(atlas.has_value());

    CHECK(atlas->width > 0);
    CHECK(atlas->height > 0);
    CHECK((atlas->width & (atlas->width - 1u)) == 0u);
    CHECK(atlas->width <= 4096u);
    CHECK(atlas->height <= 4096u);
    CHECK(atlas->ascent > 0.0f);
    CHECK(atlas->coverage.size() == (std::size_t)atlas->width * atlas->height);
    REQUIRE(atlas->glyphs.size() == codepoints.size());
    MESSAGE("Jost 32 px atlas: " << atlas->width << " x " << atlas->height);

    const auto glyphFor = [&codepoints, &atlas](char32_t codepoint)
        -> const babylon::font::Glyph&
    {
        const auto it = std::find(codepoints.begin(), codepoints.end(), codepoint);
        REQUIRE(it != codepoints.end());
        return atlas->glyphs[(std::size_t)(it - codepoints.begin())];
    };

    for ( char32_t codepoint : { U'A', U'g' } )
    {
        const babylon::font::Glyph& glyph = glyphFor(codepoint);
        CHECK(glyph.width > 0);
        CHECK(glyph.height > 0);
        CHECK(glyph.advance > 0.0f);
        CHECK(glyph.x + glyph.width <= atlas->width);
        CHECK(glyph.y + glyph.height <= atlas->height);
    }

    bool anyOverlap = false;
    for ( std::size_t a = 0; a < atlas->glyphs.size(); ++a )
    {
        const babylon::font::Glyph& glyph = atlas->glyphs[a];
        CHECK(glyph.advance >= 0.0f);
        CHECK(glyph.x + glyph.width <= atlas->width);
        CHECK(glyph.y + glyph.height <= atlas->height);
        for ( std::size_t b = a + 1; b < atlas->glyphs.size(); ++b )
        {
            anyOverlap = anyOverlap || overlaps(glyph, atlas->glyphs[b]);
        }
    }
    CHECK_FALSE(anyOverlap);
}

TEST_CASE("a truncated or garbage font is rejected instead of crashing")
{
    std::vector<std::byte> bytes = readFont();
    bytes.resize(bytes.size() / 2u);
    CHECK_FALSE(babylon::font::Font::load(std::move(bytes)).has_value());

    std::vector<std::byte> garbage(256, (std::byte)0xAB);
    CHECK_FALSE(babylon::font::Font::load(std::move(garbage)).has_value());

    CHECK_FALSE(babylon::font::Font::load({}).has_value());
}
