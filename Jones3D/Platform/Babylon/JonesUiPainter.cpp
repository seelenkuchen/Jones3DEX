// Added: the game's babylon::ui painter (ADR-0026 amendment); see JonesUiPainter.h.
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>

#include <babylon/files/FileSystem.h>
#include <babylon/files/Path.h>
#include <babylon/image/Image.h>

#include <rdroid/Main/rdroid.h>
#include <rdroid/Primitives/rdFont.h>
#include <rdroid/Raster/rdCache.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdMemory.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

namespace
{
    constexpr float kCanvasHeight = 480.0f;

    // A title measured to fit its text area must keep its edge glyphs even though the measured
    // width and the drawn advance round differently in float and double.
    constexpr double kEdgeEpsilon = 0.01;

    // The horizontal advance of one glyph in canvas units, matching what
    // rdFont_DrawTextLineClippedEx advances: the glyph's reference-pixel origin times the font's
    // point-size scale, stretched by the canvas width like the rest of the painter.
    double glyphAdvance(const rdFont* pFont, unsigned char chr, double fontScale, double xScale)
    {
        size_t curChar = chr;
        if ( pFont->aGlyphs[curChar].baselineOriginY == -pFont->fontSize )
        {
            curChar = '?';
        }

        return (double)pFont->aGlyphs[curChar].baselineOriginX * fontScale * xScale;
    }

    // The drawn width of text in canvas units. drawText and measureText both use this, so the
    // measured width is exactly the advance the font applies.
    double textWidth(const rdFont* pFont, const std::string& text, double fontScale, double xScale)
    {
        double width = 0.0;
        for ( char chr : text )
        {
            width += glyphAdvance(pFont, (unsigned char)chr, fontScale, xScale);
        }

        return width;
    }

    void setColor(rdVector4& color, const babylon::ui::Color& value)
    {
        color.red   = value.r;
        color.green = value.g;
        color.blue  = value.b;
        color.alpha = value.a;
    }

    // Decodes UTF-8 into codepoints, replacing each malformed byte with U+FFFD. The painter uses
    // the same result for measuring and drawing, so the two always agree.
    std::vector<char32_t> decodeUtf8(std::string_view text)
    {
        std::vector<char32_t> codepoints;
        codepoints.reserve(text.size());

        std::size_t index = 0;
        while ( index < text.size() )
        {
            const unsigned char first = (unsigned char)text[index];
            if ( first < 0x80 )
            {
                codepoints.push_back((char32_t)first);
                ++index;
                continue;
            }

            std::size_t length = 0;
            char32_t codepoint = 0;
            char32_t minimum   = 0;
            if ( (first & 0xE0u) == 0xC0u )
            {
                length    = 2;
                codepoint = first & 0x1Fu;
                minimum   = 0x80;
            }
            else if ( (first & 0xF0u) == 0xE0u )
            {
                length    = 3;
                codepoint = first & 0x0Fu;
                minimum   = 0x800;
            }
            else if ( (first & 0xF8u) == 0xF0u )
            {
                length    = 4;
                codepoint = first & 0x07u;
                minimum   = 0x10000;
            }
            else
            {
                codepoints.push_back(0xFFFD);
                ++index;
                continue;
            }

            bool valid = index + length <= text.size();
            for ( std::size_t cur = 1; valid && cur < length; ++cur )
            {
                const unsigned char next = (unsigned char)text[index + cur];
                if ( (next & 0xC0u) != 0x80u )
                {
                    valid = false;
                    break;
                }
                codepoint = (codepoint << 6) | (next & 0x3Fu);
            }

            if ( !valid || codepoint < minimum || codepoint > 0x10FFFF
                || (codepoint >= 0xD800 && codepoint <= 0xDFFF) )
            {
                codepoints.push_back(0xFFFD);
                ++index;
                continue;
            }

            codepoints.push_back(codepoint);
            index += length;
        }

        return codepoints;
    }

    const babylon::font::Glyph* findGlyph(const std::vector<char32_t>& codepoints,
        const std::vector<babylon::font::Glyph>& glyphs, char32_t codepoint)
    {
        const auto it = std::find(codepoints.begin(), codepoints.end(), codepoint);
        if ( it == codepoints.end() )
        {
            return nullptr;
        }

        return &glyphs[(std::size_t)(it - codepoints.begin())];
    }

    // The baked set covers printable ASCII and Latin-1. Text outside it falls back to '?', the
    // same substitution rdFont makes, so measuring and drawing still agree.
    void normalizeCodepoints(const std::vector<char32_t>& baked,
        const std::vector<babylon::font::Glyph>& glyphs, std::vector<char32_t>& codepoints)
    {
        for ( char32_t& codepoint : codepoints )
        {
            if ( !findGlyph(baked, glyphs, codepoint) )
            {
                codepoint = U'?';
            }
        }
    }
}

JonesUiPainter::~JonesUiPainter()
{
    for ( FontEntry& entry : m_fonts )
    {
        releaseImageEntry(entry.atlas);
    }
    for ( ImageEntry& entry : m_images )
    {
        releaseImageEntry(entry);
    }
}

babylon::ui::FontId JonesUiPainter::registerFont(rdFont* pFont, float pointSize)
{
    if ( !pFont || !(pointSize > 0.0f) )
    {
        STDLOG_WARNING("JonesUiPainter::registerFont: bad font or point size.\n");
        return 0;
    }

    FontEntry entry;
    entry.pFont     = pFont;
    entry.pointSize = pointSize;
    m_fonts.push_back(entry);
    return (babylon::ui::FontId)m_fonts.size();
}

babylon::ui::FontId JonesUiPainter::registerTtfFont(const babylon::font::Font& font,
    float pixelHeight)
{
    if ( !(pixelHeight > 0.0f) || !std::isfinite(pixelHeight) )
    {
        STDLOG_WARNING("JonesUiPainter::registerTtfFont: bad pixel height.\n");
        return 0;
    }

    std::vector<char32_t> codepoints;
    codepoints.reserve(0x7F - 0x20 + 0x100 - 0xA0);
    for ( char32_t codepoint = 0x20; codepoint <= 0x7E; ++codepoint )
    {
        codepoints.push_back(codepoint);
    }
    for ( char32_t codepoint = 0xA0; codepoint <= 0xFF; ++codepoint )
    {
        codepoints.push_back(codepoint);
    }

    const std::optional<babylon::font::FontAtlas> atlas = font.bake(pixelHeight, codepoints);
    if ( !atlas )
    {
        STDLOG_WARNING("JonesUiPainter::registerTtfFont: the font did not bake.\n");
        return 0;
    }

    std::vector<std::uint8_t> rgba((std::size_t)atlas->width * atlas->height * 4u);
    for ( std::size_t index = 0; index < atlas->coverage.size(); ++index )
    {
        rgba[index * 4u + 0u] = 255;
        rgba[index * 4u + 1u] = 255;
        rgba[index * 4u + 2u] = 255;
        rgba[index * 4u + 3u] = atlas->coverage[index];
    }

    ImageEntry atlasEntry;
    if ( !createRgbaMaterial(rgba.data(), atlas->width, atlas->height, atlasEntry) )
    {
        STDLOG_WARNING("JonesUiPainter::registerTtfFont: the atlas texture failed.\n");
        return 0;
    }

    FontEntry entry;
    entry.kind       = FontEntry::Kind::TrueType;
    entry.trueType   = font;
    entry.pointSize  = pixelHeight;
    entry.codepoints = std::move(codepoints);
    entry.glyphs     = atlas->glyphs;
    entry.ascent     = atlas->ascent;
    entry.descent    = atlas->descent;
    entry.lineGap    = atlas->lineGap;
    entry.atlas      = atlasEntry;
    m_fonts.push_back(std::move(entry));
    return (babylon::ui::FontId)m_fonts.size();
}

babylon::ui::ImageId JonesUiPainter::registerPngImage(const babylon::image::Image& image)
{
    if ( !image.width || !image.height || image.pixels.empty() )
    {
        STDLOG_WARNING("JonesUiPainter::registerPngImage: empty image.\n");
        return 0;
    }

    ImageEntry entry;
    if ( !createRgbaMaterial(image.pixels.data(), image.width, image.height, entry) )
    {
        STDLOG_WARNING("JonesUiPainter::registerPngImage: the image texture failed.\n");
        return 0;
    }

    m_images.push_back(entry);
    return (babylon::ui::ImageId)m_images.size();
}

babylon::ui::FontId JonesUiPainter::registerTtfFontFile(babylon::files::FileSystem& files,
    std::string_view relativePath, float pixelHeight)
{
    const std::optional<std::vector<std::byte>> bytes = loadUiFile(files, relativePath);
    if ( !bytes )
    {
        return 0;
    }

    const std::optional<babylon::font::Font> font =
        babylon::font::Font::load(std::move(*bytes));
    if ( !font )
    {
        STDLOG_WARNING("JonesUiPainter: '%s' is not a usable TrueType font.\n",
            std::string(relativePath).c_str());
        return 0;
    }

    return registerTtfFont(*font, pixelHeight);
}

babylon::ui::ImageId JonesUiPainter::registerPngImageFile(babylon::files::FileSystem& files,
    std::string_view relativePath)
{
    const std::optional<std::vector<std::byte>> bytes = loadUiFile(files, relativePath);
    if ( !bytes )
    {
        return 0;
    }

    const std::optional<babylon::image::Image> image = babylon::image::decodePng(*bytes);
    if ( !image )
    {
        STDLOG_WARNING("JonesUiPainter: '%s' is not a usable PNG.\n",
            std::string(relativePath).c_str());
        return 0;
    }

    return registerPngImage(*image);
}

std::optional<std::vector<std::byte>> JonesUiPainter::loadUiFile(
    babylon::files::FileSystem& files, std::string_view relativePath)
{
    if ( relativePath.empty() || relativePath.front() == '/'
        || !babylon::files::isValidPath(relativePath) )
    {
        STDLOG_WARNING("JonesUiPainter: invalid UI asset path '%.*s'.\n",
            (int)relativePath.size(), relativePath.data());
        return std::nullopt;
    }

    const std::string path = babylon::files::joinPath(files.basePath(),
        "Data/ui/" + std::string(relativePath));
    if ( path.empty() )
    {
        return std::nullopt;
    }

    const std::unique_ptr<babylon::files::File> file =
        files.open(path, babylon::files::OpenMode::Read);
    if ( !file )
    {
        return std::nullopt;
    }

    const std::optional<std::int64_t> size = file->size();
    if ( !size || *size < 0 )
    {
        return std::nullopt;
    }

    std::vector<std::byte> bytes((std::size_t)*size);
    if ( !bytes.empty() )
    {
        const std::optional<std::size_t> read = file->read(bytes);
        if ( !read || *read != bytes.size() )
        {
            return std::nullopt;
        }
    }

    return bytes;
}

bool JonesUiPainter::createRgbaMaterial(const std::uint8_t* pRgbaPixels,
    std::uint32_t width, std::uint32_t height, ImageEntry& entry)
{
    if ( !pRgbaPixels || !width || !height )
    {
        return false;
    }

    const std::uint64_t rowSize = (std::uint64_t)width * 4u;
    if ( rowSize > (std::uint64_t)SIZE_MAX
        || (std::uint64_t)height > (std::uint64_t)SIZE_MAX / rowSize )
    {
        return false;
    }

    tRasterInfo raster;
    STD_ZEROMEM(&raster, sizeof(raster));
    raster.width     = width;
    raster.height    = height;
    raster.rowWidth  = width;
    raster.rowSize   = (std::size_t)rowSize;
    raster.size      = (std::size_t)(rowSize * height);
    raster.colorInfo = stdColor_cfARGB8888;

    tVBuffer* pVBuffer = stdDisplay_VBufferNew(&raster, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
    if ( !pVBuffer )
    {
        return false;
    }

    // cfARGB8888 stores B, G, R, A in memory; the painter's input is R, G, B, A.
    for ( std::uint32_t y = 0; y < height; ++y )
    {
        const std::uint8_t* pSource = pRgbaPixels + y * (std::size_t)rowSize;
        std::uint8_t* pDestination = pVBuffer->pPixels + y * pVBuffer->rasterInfo.rowSize;
        for ( std::uint32_t x = 0; x < width; ++x )
        {
            pDestination[x * 4u + 0u] = pSource[x * 4u + 2u];
            pDestination[x * 4u + 1u] = pSource[x * 4u + 1u];
            pDestination[x * 4u + 2u] = pSource[x * 4u + 0u];
            pDestination[x * 4u + 3u] = pSource[x * 4u + 3u];
        }
    }

    rdMaterial* pMaterial = (rdMaterial*)STDMALLOC(sizeof(rdMaterial));
    if ( !pMaterial )
    {
        stdDisplay_VBufferFree(pVBuffer);
        return false;
    }

    STD_ZEROMEM(pMaterial, sizeof(*pMaterial));
    pMaterial->formatType = STDCOLOR_FORMAT_RGBA;
    pMaterial->width      = width;
    pMaterial->height     = height;
    pMaterial->numCels    = 1;
    pMaterial->aTextures  = (tSystemTexture*)STDMALLOC(sizeof(tSystemTexture));
    if ( !pMaterial->aTextures )
    {
        stdMemory_Free(pMaterial);
        stdDisplay_VBufferFree(pVBuffer);
        return false;
    }

    STD_ZEROMEM(&pMaterial->aTextures[0], sizeof(tSystemTexture));
    tVBuffer* aMips[1] = { pVBuffer };
    std3D_AllocSystemTexture(&pMaterial->aTextures[0], aMips, 1u, STDCOLOR_FORMAT_RGBA);
    std3D_AddToTextureCache(&pMaterial->aTextures[0], STDCOLOR_FORMAT_RGBA);
    stdDisplay_VBufferFree(pVBuffer);

    if ( !pMaterial->aTextures[0].pCachedTexture )
    {
        std3D_ClearSystemTexture(&pMaterial->aTextures[0]);
        stdMemory_Free(pMaterial->aTextures);
        stdMemory_Free(pMaterial);
        return false;
    }

    entry.pMaterial      = pMaterial;
    entry.celNum         = 0;
    entry.pOwnedMaterial = pMaterial;
    return true;
}

babylon::ui::ImageId JonesUiPainter::registerMaterialImage(const rdMaterial* pMaterial, int celNum)
{
    if ( !pMaterial || celNum < 0 || (size_t)celNum >= pMaterial->numCels )
    {
        STDLOG_WARNING("JonesUiPainter::registerMaterialImage: bad material or cel.\n");
        return 0;
    }

    ImageEntry entry;
    entry.pMaterial = pMaterial;
    entry.celNum    = celNum;
    m_images.push_back(entry);
    return (babylon::ui::ImageId)m_images.size();
}

babylon::ui::ImageId JonesUiPainter::registerRgbImage(const std::uint8_t* pBgrPixels,
    std::uint32_t width, std::uint32_t height)
{
    if ( !pBgrPixels || !width || !height )
    {
        STDLOG_WARNING("JonesUiPainter::registerRgbImage: bad image.\n");
        return 0;
    }

    const uint64_t rowSize = (uint64_t)width * 3u;
    if ( rowSize > (uint64_t)SIZE_MAX || (uint64_t)height > (uint64_t)SIZE_MAX / rowSize )
    {
        STDLOG_WARNING("JonesUiPainter::registerRgbImage: image too large.\n");
        return 0;
    }

    tRasterInfo raster;
    STD_ZEROMEM(&raster, sizeof(raster));
    raster.width     = width;
    raster.height    = height;
    raster.rowWidth  = width;
    raster.rowSize   = (size_t)rowSize;
    raster.size      = (size_t)(rowSize * height);
    raster.colorInfo = stdColor_cfRGB888;

    tVBuffer* pVBuffer = stdDisplay_VBufferNew(&raster, /*bUseVSurface=*/0, /*bUseVideoMemory=*/0);
    if ( !pVBuffer )
    {
        return 0;
    }

    for ( uint32_t y = 0; y < height; ++y )
    {
        memcpy(&pVBuffer->pPixels[y * pVBuffer->rasterInfo.rowSize],
            &pBgrPixels[y * (size_t)rowSize], (size_t)rowSize);
    }

    rdMaterial* pMaterial = (rdMaterial*)STDMALLOC(sizeof(rdMaterial));
    if ( !pMaterial )
    {
        stdDisplay_VBufferFree(pVBuffer);
        return 0;
    }

    STD_ZEROMEM(pMaterial, sizeof(*pMaterial));
    pMaterial->formatType = STDCOLOR_FORMAT_RGB;
    pMaterial->width      = width;
    pMaterial->height     = height;
    pMaterial->numCels    = 1;
    pMaterial->aTextures  = (tSystemTexture*)STDMALLOC(sizeof(tSystemTexture));
    if ( !pMaterial->aTextures )
    {
        stdMemory_Free(pMaterial);
        stdDisplay_VBufferFree(pVBuffer);
        return 0;
    }

    STD_ZEROMEM(&pMaterial->aTextures[0], sizeof(tSystemTexture));
    tVBuffer* aMips[1] = { pVBuffer };
    std3D_AllocSystemTexture(&pMaterial->aTextures[0], aMips, 1u, STDCOLOR_FORMAT_RGB);
    std3D_AddToTextureCache(&pMaterial->aTextures[0], STDCOLOR_FORMAT_RGB);
    stdDisplay_VBufferFree(pVBuffer);

    if ( !pMaterial->aTextures[0].pCachedTexture )
    {
        std3D_ClearSystemTexture(&pMaterial->aTextures[0]);
        stdMemory_Free(pMaterial->aTextures);
        stdMemory_Free(pMaterial);
        return 0;
    }

    ImageEntry entry;
    entry.pMaterial     = pMaterial;
    entry.celNum        = 0;
    entry.pOwnedMaterial = pMaterial;
    m_images.push_back(entry);
    return (babylon::ui::ImageId)m_images.size();
}

void JonesUiPainter::unregisterImage(babylon::ui::ImageId image)
{
    if ( image == 0 || image > m_images.size() )
    {
        return;
    }

    releaseImageEntry(m_images[image - 1]);
}

void JonesUiPainter::releaseImageEntry(ImageEntry& entry)
{
    if ( !entry.pOwnedMaterial )
    {
        entry.pMaterial = nullptr;
        entry.celNum    = 0;
        return;
    }

    rdMaterial* pMaterial = entry.pOwnedMaterial;
    if ( pMaterial->aTextures )
    {
        std3D_ClearSystemTexture(&pMaterial->aTextures[0]);
        stdMemory_Free(pMaterial->aTextures);
    }

    stdMemory_Free(pMaterial);
    entry.pMaterial      = nullptr;
    entry.pOwnedMaterial = nullptr;
    entry.celNum         = 0;
}

bool JonesUiPainter::getCanvasMetrics(float& canvasWidth, float& scale)
{
    uint32_t width = 0;
    uint32_t height = 0;
    stdDisplay_GetBackBufferSize(&width, &height);
    if ( !width || !height )
    {
        return false;
    }

    scale       = (float)height / kCanvasHeight;
    canvasWidth = (float)width / scale;
    return true;
}

babylon::ui::Rect JonesUiPainter::fullCanvas()
{
    float canvasWidth = RD_REF_WIDTH;
    float scale       = 1.0f;
    getCanvasMetrics(canvasWidth, scale);

    babylon::ui::Rect rect;
    rect.width  = canvasWidth;
    rect.height = kCanvasHeight;
    return rect;
}

babylon::ui::Rect JonesUiPainter::currentClip() const
{
    if ( !m_clips.empty() )
    {
        return m_clips.back();
    }

    return fullCanvas();
}

babylon::ui::Rect JonesUiPainter::intersect(const babylon::ui::Rect& a, const babylon::ui::Rect& b)
{
    const float x0 = std::max(a.x, b.x);
    const float y0 = std::max(a.y, b.y);
    const float x1 = std::min(a.x + a.width, b.x + b.width);
    const float y1 = std::min(a.y + a.height, b.y + b.height);

    babylon::ui::Rect result;
    if ( x1 <= x0 || y1 <= y0 )
    {
        return result;
    }

    result.x      = x0;
    result.y      = y0;
    result.width  = x1 - x0;
    result.height = y1 - y0;
    return result;
}

bool JonesUiPainter::isEmpty(const babylon::ui::Rect& rect)
{
    return rect.width <= 0.0f || rect.height <= 0.0f;
}

void JonesUiPainter::fillRect(const babylon::ui::Rect& rect, const babylon::ui::Color& color)
{
    const babylon::ui::Rect visible = intersect(rect, currentClip());
    if ( isEmpty(visible) )
    {
        return;
    }

    drawQuad(visible, nullptr, 0, 0.0f, 0.0f, 1.0f, 1.0f, color);
}

void JonesUiPainter::drawImage(babylon::ui::ImageId image, const babylon::ui::Rect& rect)
{
    if ( image == 0 || image > m_images.size() )
    {
        STDLOG_WARNING("JonesUiPainter::drawImage: unknown image %u.\n", image);
        return;
    }

    const ImageEntry& entry = m_images[image - 1];
    if ( !entry.pMaterial || isEmpty(rect) )
    {
        return;
    }

    const babylon::ui::Rect visible = intersect(rect, currentClip());
    if ( isEmpty(visible) )
    {
        return;
    }

    // The visible part keeps its place in the stretched image.
    const float du = visible.width / rect.width;
    const float dv = visible.height / rect.height;
    const float u0 = (visible.x - rect.x) / rect.width;
    const float v0 = (visible.y - rect.y) / rect.height;
    const babylon::ui::Color white = { 1.0f, 1.0f, 1.0f, 1.0f };
    drawQuad(visible, entry.pMaterial, entry.celNum, u0, v0, u0 + du, v0 + dv, white);
}

void JonesUiPainter::drawText(babylon::ui::FontId font, std::string_view text,
    const babylon::ui::Rect& rect, babylon::ui::TextAlign align,
    const babylon::ui::Color& color)
{
    if ( font == 0 || font > m_fonts.size() )
    {
        STDLOG_WARNING("JonesUiPainter::drawText: unknown font %u.\n", font);
        return;
    }

    const FontEntry& entry = m_fonts[font - 1];
    if ( entry.kind == FontEntry::Kind::TrueType )
    {
        drawTrueTypeText(entry, text, rect, align, color);
        return;
    }

    if ( !entry.pFont || text.empty() || isEmpty(rect) )
    {
        return;
    }

    float canvasWidth = 0.0f;
    float scale       = 0.0f;
    if ( !getCanvasMetrics(canvasWidth, scale) )
    {
        return;
    }

    const double fontScale = entry.pointSize / (double)entry.pFont->fontSize;
    if ( !(fontScale > 0.0) )
    {
        return;
    }

    // The reference font is 640 x 480. The canvas is 480 high, so the vertical scale is 1 and the
    // horizontal reference pixels stretch by canvasWidth / 640.
    const double xScale     = canvasWidth / RD_REF_WIDTH;
    const float lineHeight  = (float)((double)entry.pFont->lineSpacing * fontScale);
    const float lineTop     = rect.y + (rect.height - lineHeight) * 0.5f;

    const babylon::ui::Rect visible = intersect(rect, currentClip());
    // A line is never dropped for being taller than its box: it is centered vertically and drawn.
    // Only a line that lies completely outside the visible clip is skipped.
    if ( isEmpty(visible) || lineTop >= visible.y + visible.height || lineTop + lineHeight <= visible.y )
    {
        return;
    }

    const std::string str(text);
    const double drawnWidth = textWidth(entry.pFont, str, fontScale, xScale);

    double startX = rect.x;
    if ( align == babylon::ui::TextAlign::Center )
    {
        startX = rect.x + (rect.width - drawnWidth) * 0.5;
    }
    else if ( align == babylon::ui::TextAlign::Right )
    {
        startX = rect.x + rect.width - drawnWidth;
    }

    // Drop every glyph that is not completely inside the visible rectangle. A glyph that crosses
    // an edge would otherwise bleed past the clip; the font can only clip at the screen edge.
    const double right = visible.x + visible.width;
    double pos         = startX;
    size_t first       = 0;
    while ( first < str.size() && pos < visible.x - kEdgeEpsilon )
    {
        pos += glyphAdvance(entry.pFont, (unsigned char)str[first], fontScale, xScale);
        ++first;
    }

    const float drawX = (float)pos;
    size_t last = first;
    while ( last < str.size() )
    {
        const double advance = glyphAdvance(entry.pFont, (unsigned char)str[last], fontScale, xScale);
        if ( pos + advance > right + kEdgeEpsilon )
        {
            break;
        }

        pos += advance;
        ++last;
    }

    if ( first == last )
    {
        return;
    }

    rdFontColor fontColor;
    for ( size_t i = 0; i < STD_ARRAYLEN(fontColor); ++i )
    {
        setColor(fontColor[i], color);
    }

    rdFont_SetFontColor(fontColor);

    // drawText centers the text vertically in its line box; the font's y argument is the top of
    // the line, so the box top is passed as is.
    const std::string clipped(str.substr(first, last - first));
    rdFont_DrawTextLineClippedEx(clipped.c_str(), drawX / canvasWidth, lineTop / kCanvasHeight,
        RD_FIXEDPOINT_RHW_SCALE_X1, entry.pFont, RDFONT_ALIGNLEFT, entry.pointSize);

    rdCache_FlushAlpha();
    rdCache_Flush();
}

babylon::ui::Size JonesUiPainter::measureText(babylon::ui::FontId font,
    std::string_view text) const
{
    babylon::ui::Size size;
    if ( font == 0 || font > m_fonts.size() )
    {
        return size;
    }

    const FontEntry& entry = m_fonts[font - 1];
    if ( entry.kind == FontEntry::Kind::TrueType )
    {
        return measureTrueTypeText(entry, text);
    }

    if ( !entry.pFont )
    {
        return size;
    }

    float canvasWidth = 0.0f;
    float scale       = 0.0f;
    if ( !getCanvasMetrics(canvasWidth, scale) )
    {
        return size;
    }

    const double fontScale = entry.pointSize / (double)entry.pFont->fontSize;
    const double xScale    = canvasWidth / RD_REF_WIDTH;
    const std::string str(text);
    size.width  = (float)textWidth(entry.pFont, str, fontScale, xScale);
    size.height = (float)((double)entry.pFont->lineSpacing * fontScale);
    return size;
}

void JonesUiPainter::drawTrueTypeText(const FontEntry& entry, std::string_view text,
    const babylon::ui::Rect& rect, babylon::ui::TextAlign align,
    const babylon::ui::Color& color)
{
    if ( text.empty() || isEmpty(rect) || !entry.trueType || !entry.atlas.pMaterial )
    {
        return;
    }

    std::vector<char32_t> codepoints = decodeUtf8(text);
    normalizeCodepoints(entry.codepoints, entry.glyphs, codepoints);
    if ( codepoints.empty() )
    {
        return;
    }

    float lineHeight = entry.ascent - entry.descent + entry.lineGap;
    if ( !(lineHeight > 0.0f) )
    {
        lineHeight = entry.pointSize;
    }
    const float lineTop = rect.y + (rect.height - lineHeight) * 0.5f;

    const babylon::ui::Rect visible = intersect(rect, currentClip());
    if ( isEmpty(visible) || lineTop >= visible.y + visible.height
        || lineTop + lineHeight <= visible.y )
    {
        return;
    }

    float drawnWidth = 0.0f;
    for ( std::size_t index = 0; index < codepoints.size(); ++index )
    {
        if ( index > 0 )
        {
            drawnWidth += entry.trueType->kerning(codepoints[index - 1],
                codepoints[index], entry.pointSize);
        }
        if ( const babylon::font::Glyph* pGlyph =
            findGlyph(entry.codepoints, entry.glyphs, codepoints[index]) )
        {
            drawnWidth += pGlyph->advance;
        }
    }

    drawnWidth = std::max(0.0f, drawnWidth);
    float penX = rect.x;
    if ( align == babylon::ui::TextAlign::Center )
    {
        penX = rect.x + (rect.width - drawnWidth) * 0.5f;
    }
    else if ( align == babylon::ui::TextAlign::Right )
    {
        penX = rect.x + rect.width - drawnWidth;
    }

    const float atlasWidth  = (float)entry.atlas.pMaterial->width;
    const float atlasHeight = (float)entry.atlas.pMaterial->height;
    const float baseline    = lineTop + entry.ascent;
    for ( std::size_t index = 0; index < codepoints.size(); ++index )
    {
        if ( index > 0 )
        {
            penX += entry.trueType->kerning(codepoints[index - 1],
                codepoints[index], entry.pointSize);
        }

        const babylon::font::Glyph* pGlyph =
            findGlyph(entry.codepoints, entry.glyphs, codepoints[index]);
        if ( !pGlyph )
        {
            continue;
        }

        if ( pGlyph->width > 0 && pGlyph->height > 0 )
        {
            const babylon::ui::Rect destination = { penX + pGlyph->offsetX,
                baseline + pGlyph->offsetY, (float)pGlyph->width, (float)pGlyph->height };
            const babylon::ui::Rect clipped = intersect(destination, visible);
            if ( !isEmpty(clipped) )
            {
                const float u0 = ((float)pGlyph->x + (clipped.x - destination.x)) / atlasWidth;
                const float v0 = ((float)pGlyph->y + (clipped.y - destination.y)) / atlasHeight;
                const float u1 = u0 + clipped.width / atlasWidth;
                const float v1 = v0 + clipped.height / atlasHeight;
                drawQuad(clipped, entry.atlas.pMaterial, entry.atlas.celNum,
                    u0, v0, u1, v1, color);
            }
        }

        penX += pGlyph->advance;
    }
}

babylon::ui::Size JonesUiPainter::measureTrueTypeText(const FontEntry& entry,
    std::string_view text) const
{
    babylon::ui::Size size;
    float lineHeight = entry.ascent - entry.descent + entry.lineGap;
    if ( !(lineHeight > 0.0f) )
    {
        lineHeight = entry.pointSize;
    }
    size.height = lineHeight;

    if ( !entry.trueType )
    {
        return size;
    }

    std::vector<char32_t> codepoints = decodeUtf8(text);
    normalizeCodepoints(entry.codepoints, entry.glyphs, codepoints);
    for ( std::size_t index = 0; index < codepoints.size(); ++index )
    {
        if ( index > 0 )
        {
            size.width += entry.trueType->kerning(codepoints[index - 1],
                codepoints[index], entry.pointSize);
        }
        if ( const babylon::font::Glyph* pGlyph =
            findGlyph(entry.codepoints, entry.glyphs, codepoints[index]) )
        {
            size.width += pGlyph->advance;
        }
    }

    size.width = std::max(0.0f, size.width);
    return size;
}

void JonesUiPainter::pushClip(const babylon::ui::Rect& rect)
{
    m_clips.push_back(intersect(rect, currentClip()));
}

void JonesUiPainter::popClip()
{
    if ( m_clips.empty() )
    {
        STDLOG_WARNING("JonesUiPainter::popClip: no matching pushClip.\n");
        return;
    }

    m_clips.pop_back();
}

void JonesUiPainter::drawQuad(const babylon::ui::Rect& rect, const rdMaterial* pMaterial,
    int celNum, float u0, float v0, float u1, float v1,
    const babylon::ui::Color& color)
{
    float canvasWidth = 0.0f;
    float scale       = 0.0f;
    if ( !getCanvasMetrics(canvasWidth, scale) )
    {
        return;
    }

    rdCacheProcEntry* pPoly = rdCache_GetAlphaProcEntry();
    if ( !pPoly )
    {
        return;
    }

    // The HUD's 2D sprites do not write depth, so neither do the menu quads; the panel must not
    // leave nearer depth that a later background cannot overwrite.
    pPoly->flags        = (rdFaceFlags)(RD_FF_TEX_CLAMP_Y | RD_FF_TEX_CLAMP_X | RD_FF_TEX_TRANSLUCENT
        | RD_FF_ZWRITE_DISABLED);
    pPoly->lightingMode = RD_LIGHTING_DIFFUSE;
    pPoly->numVertices  = 4;
    pPoly->pMaterial    = const_cast<rdMaterial*>(pMaterial);
    pPoly->matCelNum    = celNum;
    setColor(pPoly->extraLight, color);
    setColor(pPoly->aVertIntensities[0], color);

    const float x0 = rect.x * scale;
    const float y0 = rect.y * scale;
    const float x1 = (rect.x + rect.width) * scale;
    const float y1 = (rect.y + rect.height) * scale;

    StdVertexTL* pVertex = pPoly->aVertices;
    pVertex->sx  = x0;
    pVertex->sy  = y0;
    pVertex->sz  = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->rhw = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->tu  = u0;
    pVertex->tv  = v0;
    ++pVertex;

    pVertex->sx  = x1;
    pVertex->sy  = y0;
    pVertex->sz  = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->rhw = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->tu  = u1;
    pVertex->tv  = v0;
    ++pVertex;

    pVertex->sx  = x1;
    pVertex->sy  = y1;
    pVertex->sz  = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->rhw = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->tu  = u1;
    pVertex->tv  = v1;
    ++pVertex;

    pVertex->sx  = x0;
    pVertex->sy  = y1;
    pVertex->sz  = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->rhw = RD_FIXEDPOINT_RHW_SCALE_X1;
    pVertex->tu  = u0;
    pVertex->tv  = v1;

    rdCache_AddAlphaProcFace(4);
    rdCache_FlushAlpha();
    rdCache_Flush();
}
