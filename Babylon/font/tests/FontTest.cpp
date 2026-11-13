// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/font/Font.h>

#include <doctest.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
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
        std::ifstream file(BABYLON_FONT_TEST_FONT, std::ios::binary | std::ios::ate);
        REQUIRE(file);
        const std::streamsize size = file.tellg();
        REQUIRE(size >= 0);
        file.seekg(0, std::ios::beg);

        std::vector<std::byte> data((std::size_t)size);
        if ( !data.empty() )
        {
            REQUIRE(file.read(reinterpret_cast<char*>(data.data()), size));
        }
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

TEST_CASE("Jost maps a UI arrow pair and the focus bullet")
{
    const std::vector<std::byte> bytes = readFont();
    const std::optional<babylon::font::Font> font = babylon::font::Font::load(bytes);
    REQUIRE(font.has_value());

    CHECK(font->hasGlyph(U'A'));
    CHECK_FALSE(font->hasGlyph(0x10FFFD));

    const bool primary = font->hasGlyph(0x25C0) && font->hasGlyph(0x25B6);
    const bool secondary = font->hasGlyph(0x2039) && font->hasGlyph(0x203A);
    const bool ascii = font->hasGlyph(U'<') && font->hasGlyph(U'>');
    CHECK((primary || secondary || ascii));

    // The same order JonesUiPainter uses when it bakes a font.
    if ( primary )
    {
        CHECK(font->hasGlyph(0x25C0));
        CHECK(font->hasGlyph(0x25B6));
    }
    else if ( secondary )
    {
        CHECK(font->hasGlyph(0x2039));
        CHECK(font->hasGlyph(0x203A));
    }
    else
    {
        CHECK(font->hasGlyph(U'<'));
        CHECK(font->hasGlyph(U'>'));
    }

    CHECK(font->hasGlyph(0x2022));
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
