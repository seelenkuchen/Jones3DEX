// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace babylon::font
{

    // One glyph the caller asked bake() for, in pixel units. x, y, width and
    // height locate its coverage rectangle in the atlas; a glyph with no
    // pixels (a space, for example) has a zero width or height. offsetX and
    // offsetY move the rectangle from the pen position at the baseline: x
    // grows right, y grows down, so offsetY is normally negative (above the
    // baseline). advance is the horizontal distance to the next pen
    // position.
    struct Glyph
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float advance = 0.0f;
    };

    // The result of baking one font at one pixel height. coverage holds
    // width * height bytes, one per texel, row by row from the top, 0 for
    // transparent and 255 for fully covered. glyphs has one entry per
    // codepoint passed to bake(), in the same order. ascent, descent and
    // lineGap are the font's vertical metrics in pixels; descent is
    // negative and lineGap may be zero or negative.
    struct FontAtlas
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> coverage;
        float ascent = 0.0f;
        float descent = 0.0f;
        float lineGap = 0.0f;
        std::vector<Glyph> glyphs;
    };

    // A TrueType font held in memory. load() copies the bytes in, so the
    // caller may release its buffer afterwards. A font that isn't a valid
    // TrueType file, or whose tables point outside the buffer, is rejected
    // with nullopt; a bad file never crashes the process.
    class Font
    {
    public:
        // Parses and validates a whole TrueType font file (a plain sfnt or
        // the first font of a collection). Returns nullopt for empty,
        // truncated, malformed or unsupported bytes.
        static std::optional<Font> load(std::vector<std::byte> bytes);

        // Rasterizes the requested codepoints at pixelHeight. Codepoints the
        // font does not map use the font's glyph 0. The atlas is packed on
        // shelves with one pixel between glyphs; its width is a power of two
        // and it grows until everything fits. Returns nullopt, with one
        // warning, for a non-positive or non-finite height or when the
        // glyphs do not fit in 4096 x 4096.
        std::optional<FontAtlas> bake(float pixelHeight,
            std::span<const char32_t> codepoints) const;

        // True when the font's character map maps codepoint to a real glyph
        // (a glyph index other than 0). A font without a usable character
        // map, or a codepoint outside Unicode, reports false.
        bool hasGlyph(char32_t codepoint) const;

        // The horizontal kerning adjustment between two codepoints at
        // pixelHeight, in pixels; 0 when the font has no pair or the height
        // is invalid. The caller adds it to the second glyph's advance.
        float kerning(char32_t a, char32_t b, float pixelHeight) const;

        Font(const Font&) = default;
        Font(Font&&) noexcept = default;
        Font& operator=(const Font&) = default;
        Font& operator=(Font&&) noexcept = default;

    private:
        Font() = default;

        std::vector<std::byte> m_bytes;
        int m_fontOffset = -1;
    };

}
