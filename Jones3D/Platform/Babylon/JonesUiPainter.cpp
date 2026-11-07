// Added: the game's babylon::ui painter (ADR-0026 amendment); see JonesUiPainter.h.
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>

#include <rdroid/Main/rdroid.h>
#include <rdroid/Primitives/rdFont.h>
#include <rdroid/Raster/rdCache.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdMemory.h>

#include <algorithm>
#include <cstring>
#include <string>

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
}

JonesUiPainter::~JonesUiPainter()
{
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

    pPoly->flags        = (rdFaceFlags)(RD_FF_TEX_CLAMP_Y | RD_FF_TEX_CLAMP_X | RD_FF_TEX_TRANSLUCENT);
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
