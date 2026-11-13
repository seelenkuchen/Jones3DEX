// SPDX-License-Identifier: MPL-2.0
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include <babylon/font/Font.h>

#include <babylon/log/Log.h>

#include <stb_truetype.h>

#include <algorithm>
#include <cmath>

namespace babylon::font
{

namespace
{

    constexpr std::uint32_t tagOf(char a, char b, char c, char d)
    {
        return ((std::uint32_t)(std::uint8_t)a << 24) | ((std::uint32_t)(std::uint8_t)b << 16)
            | ((std::uint32_t)(std::uint8_t)c << 8) | (std::uint32_t)(std::uint8_t)d;
    }

    constexpr std::uint32_t kTagCmap = tagOf('c', 'm', 'a', 'p');
    constexpr std::uint32_t kTagHead = tagOf('h', 'e', 'a', 'd');
    constexpr std::uint32_t kTagHhea = tagOf('h', 'h', 'e', 'a');
    constexpr std::uint32_t kTagHmtx = tagOf('h', 'm', 't', 'x');
    constexpr std::uint32_t kTagMaxp = tagOf('m', 'a', 'x', 'p');
    constexpr std::uint32_t kTagLoca = tagOf('l', 'o', 'c', 'a');
    constexpr std::uint32_t kTagGlyf = tagOf('g', 'l', 'y', 'f');
    constexpr std::uint32_t kTagKern = tagOf('k', 'e', 'r', 'n');

    constexpr std::uint32_t kSfntTrueType = 0x00010000u;
    constexpr std::uint32_t kSfntTrue = tagOf('t', 'r', 'u', 'e');
    constexpr std::uint32_t kSfntTyp1 = tagOf('t', 'y', 'p', '1');
    constexpr std::uint32_t kSfntOtto = tagOf('O', 'T', 'T', 'O');
    constexpr std::uint32_t kCollection = tagOf('t', 't', 'c', 'f');

    constexpr std::uint32_t kMaxAtlasSize = 4096;
    constexpr std::uint32_t kAtlasPadding = 1;

    std::uint16_t read16(const std::uint8_t* pData, std::size_t offset)
    {
        return (std::uint16_t)(((std::uint16_t)pData[offset] << 8) | pData[offset + 1]);
    }

    std::uint32_t read32(const std::uint8_t* pData, std::size_t offset)
    {
        return ((std::uint32_t)pData[offset] << 24) | ((std::uint32_t)pData[offset + 1] << 16)
            | ((std::uint32_t)pData[offset + 2] << 8) | (std::uint32_t)pData[offset + 3];
    }

    struct TableRecord
    {
        std::uint32_t tag = 0;
        std::uint32_t offset = 0;
        std::uint32_t length = 0;
    };

    bool validateCmapSubtable(const std::uint8_t* pData, std::size_t size,
        std::uint32_t subtable, std::uint32_t tableOffset, std::uint32_t tableLength)
    {
        if ( subtable < tableOffset || subtable - tableOffset > tableLength
            || tableLength - (subtable - tableOffset) < 2 || subtable + 2 > size )
        {
            return false;
        }

        const std::uint32_t available = tableLength - (subtable - tableOffset);
        const std::uint16_t format    = read16(pData, subtable);
        switch ( format )
        {
            case 0:
            {
                if ( available < 6 )
                {
                    return false;
                }
                const std::uint16_t length = read16(pData, subtable + 2);
                return length >= 6 && length <= available;
            }
            case 4:
            {
                if ( available < 16 )
                {
                    return false;
                }
                const std::uint16_t length = read16(pData, subtable + 2);
                const std::uint16_t segCountX2 = read16(pData, subtable + 6);
                if ( length < 16 || segCountX2 == 0 || (segCountX2 & 1u) != 0 )
                {
                    return false;
                }
                const std::uint32_t segments = segCountX2 / 2u;
                return length <= available && length >= 16u + 8u * segments;
            }
            case 6:
            {
                if ( available < 10 )
                {
                    return false;
                }
                const std::uint16_t length = read16(pData, subtable + 2);
                return length >= 10 && length <= available;
            }
            case 12:
            case 13:
            {
                if ( available < 16 )
                {
                    return false;
                }
                const std::uint32_t length = read32(pData, subtable + 4);
                const std::uint32_t groups = read32(pData, subtable + 12);
                return length >= 16 && length <= available
                    && groups <= (length - 16u) / 12u;
            }
            default:
                return false;
        }
    }

    bool validateCmap(const std::uint8_t* pData, std::size_t size,
        const TableRecord& cmap)
    {
        if ( cmap.length < 4 || cmap.offset + (std::size_t)cmap.length > size )
        {
            return false;
        }

        const std::uint16_t numTables = read16(pData, cmap.offset + 2);
        if ( (std::size_t)numTables > (cmap.length - 4u) / 8u )
        {
            return false;
        }

        std::uint32_t selected = 0;
        std::uint32_t symbol   = 0;
        for ( std::uint16_t index = 0; index < numTables; ++index )
        {
            const std::size_t record = cmap.offset + 4 + (std::size_t)index * 8;
            const std::uint16_t platform = read16(pData, record);
            const std::uint16_t encoding = read16(pData, record + 2);
            const std::uint32_t offset   = read32(pData, record + 4);
            if ( offset >= cmap.length )
            {
                return false;
            }

            const std::uint32_t absolute = cmap.offset + offset;
            if ( platform == 0 )
            {
                selected = absolute;
            }
            else if ( platform == 3 && (encoding == 1 || encoding == 10) )
            {
                selected = absolute;
            }
            else if ( platform == 3 && encoding == 0 )
            {
                symbol = absolute;
            }
        }

        if ( selected == 0 )
        {
            selected = symbol;
        }

        return selected != 0
            && validateCmapSubtable(pData, size, selected, cmap.offset, cmap.length);
    }

    bool validateKern(const std::uint8_t* pData, std::size_t size,
        const TableRecord& kern)
    {
        if ( kern.length < 4 || kern.offset + (std::size_t)kern.length > size )
        {
            return false;
        }

        const std::uint16_t numTables = read16(pData, kern.offset + 2);
        if ( numTables == 0 )
        {
            return true;
        }
        if ( kern.length < 10 )
        {
            return false;
        }

        const std::uint32_t subtable = kern.offset + 4;
        const std::uint16_t length   = read16(pData, subtable + 2);
        const std::uint16_t coverage = read16(pData, subtable + 4);
        if ( length < 14 || length > kern.length - 4u )
        {
            return false;
        }

        // stb_truetype only reads the first subtable, and only when it is
        // horizontal format 0; validate the pair array it would index.
        if ( coverage == 1 )
        {
            const std::uint16_t pairCount = read16(pData, subtable + 6);
            if ( (std::uint32_t)length < 14u + 6u * pairCount )
            {
                return false;
            }
        }

        return true;
    }

    bool validateFont(const std::uint8_t* pData, std::size_t size, int& fontOffset)
    {
        if ( size < 12 )
        {
            return false;
        }

        const std::uint32_t outer = read32(pData, 0);
        std::size_t offset        = 0;
        if ( outer == kCollection )
        {
            const std::uint32_t version = read32(pData, 4);
            if ( version != 0x00010000u && version != 0x00020000u )
            {
                return false;
            }

            const std::uint32_t numFonts = read32(pData, 8);
            if ( numFonts == 0 || numFonts > (size - 12u) / 4u )
            {
                return false;
            }

            offset = read32(pData, 12);
            if ( offset + 12u > size )
            {
                return false;
            }
        }
        else if ( outer != kSfntTrueType && outer != kSfntTrue && outer != kSfntTyp1
            && outer != kSfntOtto )
        {
            return false;
        }

        const std::uint32_t tag = read32(pData, offset);
        if ( tag != kSfntTrueType && tag != kSfntTrue && tag != kSfntTyp1
            && tag != kSfntOtto )
        {
            return false;
        }

        const std::uint16_t numTables = read16(pData, offset + 4);
        if ( numTables == 0 || offset + 12u + (std::size_t)numTables * 16u > size )
        {
            return false;
        }

        TableRecord cmap;
        TableRecord head;
        TableRecord hhea;
        TableRecord hmtx;
        TableRecord maxp;
        TableRecord loca;
        TableRecord glyf;
        TableRecord kern;
        for ( std::uint16_t index = 0; index < numTables; ++index )
        {
            const std::size_t entry = offset + 12 + (std::size_t)index * 16;
            const std::uint32_t tableTag = read32(pData, entry);
            const std::uint32_t tableOffset = read32(pData, entry + 8);
            const std::uint32_t tableLength = read32(pData, entry + 12);
            if ( tableOffset > size || tableLength > size - tableOffset )
            {
                return false;
            }

            TableRecord* pRecord = nullptr;
            switch ( tableTag )
            {
                case kTagCmap: pRecord = &cmap; break;
                case kTagHead: pRecord = &head; break;
                case kTagHhea: pRecord = &hhea; break;
                case kTagHmtx: pRecord = &hmtx; break;
                case kTagMaxp: pRecord = &maxp; break;
                case kTagLoca: pRecord = &loca; break;
                case kTagGlyf: pRecord = &glyf; break;
                case kTagKern: pRecord = &kern; break;
                default: break;
            }

            if ( pRecord )
            {
                pRecord->tag    = tableTag;
                pRecord->offset = tableOffset;
                pRecord->length = tableLength;
            }
        }

        if ( !cmap.length || !head.length || !hhea.length || !hmtx.length || !maxp.length
            || !loca.length || !glyf.length )
        {
            return false;
        }
        if ( head.length < 54 || hhea.length < 36 || maxp.length < 6
            || cmap.length < 4 || hmtx.length < 4 )
        {
            return false;
        }

        // stb_truetype divides by the ascent-descent span when scaling, and
        // by the units per em when mapping, so both must be usable.
        const std::int16_t fontAscent  = (std::int16_t)read16(pData, hhea.offset + 4);
        const std::int16_t fontDescent = (std::int16_t)read16(pData, hhea.offset + 6);
        if ( fontAscent <= fontDescent || read16(pData, head.offset + 18) == 0 )
        {
            return false;
        }

        const std::uint16_t numGlyphs = read16(pData, maxp.offset + 4);
        if ( numGlyphs == 0 )
        {
            return false;
        }

        const std::uint16_t indexToLocFormat = read16(pData, head.offset + 50);
        if ( indexToLocFormat > 1 )
        {
            return false;
        }
        const std::uint32_t locaEntrySize = indexToLocFormat == 0 ? 2u : 4u;
        if ( loca.length < ((std::uint32_t)numGlyphs + 1u) * locaEntrySize )
        {
            return false;
        }

        const std::uint16_t numHMetrics = read16(pData, hhea.offset + 34);
        if ( numHMetrics == 0 || numHMetrics > numGlyphs )
        {
            return false;
        }
        const std::uint64_t hmtxNeeded = (std::uint64_t)numHMetrics * 4u
            + (std::uint64_t)(numGlyphs - numHMetrics) * 2u;
        if ( hmtx.length < hmtxNeeded )
        {
            return false;
        }

        // Every glyph must lie inside glyf, and the offsets must not go
        // backwards, so stb_truetype never reads outside the validated table.
        std::uint32_t previous = 0;
        for ( std::uint32_t index = 0; index <= numGlyphs; ++index )
        {
            const std::uint32_t value = indexToLocFormat == 0
                ? (std::uint32_t)read16(pData, loca.offset + index * 2u) * 2u
                : read32(pData, loca.offset + index * 4u);
            if ( value > glyf.length || value < previous )
            {
                return false;
            }
            previous = value;
        }

        if ( !validateCmap(pData, size, cmap) )
        {
            return false;
        }
        if ( kern.length && !validateKern(pData, size, kern) )
        {
            return false;
        }

        fontOffset = (int)offset;
        return true;
    }

    int glyphIndexFor(const stbtt_fontinfo& info, char32_t codepoint)
    {
        if ( codepoint > 0x10FFFF )
        {
            return 0;
        }

        return stbtt_FindGlyphIndex(&info, (int)codepoint);
    }

    struct Placement
    {
        std::uint32_t x = 0;
        std::uint32_t y = 0;
    };

    // Shelves of glyphs separated by one pixel, starting one pixel from the
    // top left. Returns false when a glyph is wider than the atlas or the
    // shelves would grow past 4096 pixels.
    bool packGlyphs(std::uint32_t width, const std::vector<Glyph>& glyphs,
        std::vector<Placement>& placements, std::uint32_t& height)
    {
        placements.assign(glyphs.size(), Placement{});

        std::uint32_t cursorX   = kAtlasPadding;
        std::uint32_t cursorY   = kAtlasPadding;
        std::uint32_t rowHeight = 0;
        for ( std::size_t index = 0; index < glyphs.size(); ++index )
        {
            const Glyph& glyph = glyphs[index];
            if ( glyph.width == 0 || glyph.height == 0 )
            {
                continue;
            }

            if ( glyph.width > width - 2u * kAtlasPadding )
            {
                return false;
            }

            if ( cursorX + glyph.width + kAtlasPadding > width )
            {
                cursorX = kAtlasPadding;
                cursorY += rowHeight + kAtlasPadding;
                rowHeight = 0;
            }

            if ( cursorY + glyph.height + kAtlasPadding > kMaxAtlasSize )
            {
                return false;
            }

            placements[index].x = cursorX;
            placements[index].y = cursorY;
            cursorX += glyph.width + kAtlasPadding;
            rowHeight = std::max(rowHeight, glyph.height);
        }

        height = std::max(1u, cursorY + rowHeight + kAtlasPadding);
        return height <= kMaxAtlasSize;
    }

}

std::optional<Font> Font::load(std::vector<std::byte> bytes)
{
    if ( bytes.empty() )
    {
        return std::nullopt;
    }

    const std::uint8_t* pData = reinterpret_cast<const std::uint8_t*>(bytes.data());
    int fontOffset            = -1;
    if ( !validateFont(pData, bytes.size(), fontOffset) )
    {
        return std::nullopt;
    }

    stbtt_fontinfo info;
    if ( !stbtt_InitFont(&info, pData, fontOffset) )
    {
        return std::nullopt;
    }

    Font font;
    font.m_bytes      = std::move(bytes);
    font.m_fontOffset = fontOffset;
    return font;
}

std::optional<FontAtlas> Font::bake(float pixelHeight,
    std::span<const char32_t> codepoints) const
{
    if ( !(pixelHeight > 0.0f) || !std::isfinite(pixelHeight) || m_fontOffset < 0 )
    {
        return std::nullopt;
    }

    const std::uint8_t* pData = reinterpret_cast<const std::uint8_t*>(m_bytes.data());
    stbtt_fontinfo info;
    if ( !stbtt_InitFont(&info, const_cast<std::uint8_t*>(pData), m_fontOffset) )
    {
        return std::nullopt;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    if ( !(scale > 0.0f) || !std::isfinite(scale) )
    {
        return std::nullopt;
    }

    FontAtlas atlas;
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    atlas.ascent  = (float)ascent * scale;
    atlas.descent = (float)descent * scale;
    atlas.lineGap = (float)lineGap * scale;
    atlas.glyphs.reserve(codepoints.size());

    for ( char32_t codepoint : codepoints )
    {
        const int glyphIndex = glyphIndexFor(info, codepoint);

        int advanceWidth = 0;
        int leftSideBearing = 0;
        stbtt_GetGlyphHMetrics(&info, glyphIndex, &advanceWidth, &leftSideBearing);
        (void)leftSideBearing;

        int x0 = 0;
        int y0 = 0;
        int x1 = 0;
        int y1 = 0;
        stbtt_GetGlyphBitmapBox(&info, glyphIndex, scale, scale, &x0, &y0, &x1, &y1);

        Glyph glyph;
        glyph.x       = 0;
        glyph.y       = 0;
        glyph.width   = x1 > x0 ? (std::uint32_t)(x1 - x0) : 0;
        glyph.height  = y1 > y0 ? (std::uint32_t)(y1 - y0) : 0;
        glyph.offsetX = (float)x0;
        glyph.offsetY = (float)y0;
        glyph.advance = (float)advanceWidth * scale;
        atlas.glyphs.push_back(glyph);
    }

    // Try every power-of-two width and keep the one whose longer side is shortest: a wide,
    // short atlas wastes less space than the narrowest one that happens to fit.
    std::vector<Placement> placements;
    std::vector<Placement> candidatePlacements;
    std::uint32_t width       = 0;
    std::uint32_t height      = 0;
    std::uint32_t bestExtent  = kMaxAtlasSize + 1u;
    for ( std::uint32_t candidate = 16; candidate <= kMaxAtlasSize; candidate *= 2u )
    {
        std::uint32_t candidateHeight = 0;
        if ( !packGlyphs(candidate, atlas.glyphs, candidatePlacements, candidateHeight) )
        {
            continue;
        }

        const std::uint32_t extent = std::max(candidate, candidateHeight);
        if ( extent < bestExtent )
        {
            bestExtent  = extent;
            width       = candidate;
            height      = candidateHeight;
            placements  = candidatePlacements;
        }
    }

    if ( width == 0 )
    {
        log::warning("font: glyphs do not fit in a {} x {} atlas",
            kMaxAtlasSize, kMaxAtlasSize);
        return std::nullopt;
    }

    atlas.width  = width;
    atlas.height = height;
    const std::size_t texels = (std::size_t)width * height;
    atlas.coverage.assign(texels, 0);

    for ( std::size_t index = 0; index < atlas.glyphs.size(); ++index )
    {
        Glyph& glyph = atlas.glyphs[index];
        glyph.x      = placements[index].x;
        glyph.y      = placements[index].y;
        if ( glyph.width == 0 || glyph.height == 0 )
        {
            continue;
        }

        const int glyphIndex = glyphIndexFor(info, codepoints[index]);
        std::uint8_t* pDestination = atlas.coverage.data()
            + (std::size_t)glyph.y * width + glyph.x;
        stbtt_MakeGlyphBitmap(&info, pDestination, (int)glyph.width, (int)glyph.height,
            (int)width, scale, scale, glyphIndex);
    }

    return atlas;
}

float Font::kerning(char32_t a, char32_t b, float pixelHeight) const
{
    if ( !(pixelHeight > 0.0f) || !std::isfinite(pixelHeight) || m_fontOffset < 0 )
    {
        return 0.0f;
    }

    const std::uint8_t* pData = reinterpret_cast<const std::uint8_t*>(m_bytes.data());
    stbtt_fontinfo info;
    if ( !stbtt_InitFont(&info, const_cast<std::uint8_t*>(pData), m_fontOffset) )
    {
        return 0.0f;
    }

    const float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    if ( !(scale > 0.0f) || !std::isfinite(scale) )
    {
        return 0.0f;
    }

    const int first  = glyphIndexFor(info, a);
    const int second = glyphIndexFor(info, b);
    return (float)stbtt_GetGlyphKernAdvance(&info, first, second) * scale;
}

}
