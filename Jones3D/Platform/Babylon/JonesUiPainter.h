// Added: the game's babylon::ui painter (ADR-0026 amendment). It draws the ui through the same
// rdFont text and rdCache 2D path the HUD uses, so the menus look like the HUD without a second
// font system.
//
// Call it inside the game's current frame: after std3D_StartScene() (or after the frame was
// begun another way) and before std3D_EndScene() and stdDisplay_Update(). Every draw call submits
// its quads at once, so later game draws keep their order.
//
// Canvas coordinates are the ui's: 480 units high, 480 * aspect wide, origin at the top left. The
// painter maps them to the current back buffer size, which it reads on every call.
//
// The painter must be destroyed, or its owned images unregistered, before std3D_Shutdown: freeing
// an owned texture needs the open device.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIPAINTER_H
#define JONES3D_PLATFORM_BABYLON_JONESUIPAINTER_H
#ifndef __cplusplus
#error "JonesUiPainter.h is C++ only: it implements a babylon::ui interface."
#endif

#include <babylon/font/Font.h>
#include <babylon/ui/Painter.h>
#include <babylon/ui/Types.h>

#include <rdroid/types.h>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace babylon::files
{
    class FileSystem;
}

namespace babylon::image
{
    struct Image;
}

class JonesUiPainter final : public babylon::ui::Painter
{
public:
    JonesUiPainter() = default;
    ~JonesUiPainter() override;

    JonesUiPainter(const JonesUiPainter&) = delete;
    JonesUiPainter& operator=(const JonesUiPainter&) = delete;

    // Registers a TrueType font the painter bakes and draws with. pixelHeight is the size in
    // canvas units; the painter bakes printable ASCII and Latin-1 (U+0020..U+007E and
    // U+00A0..U+00FF) once and owns that atlas. The font is copied, so the caller may release it
    // afterwards. Returns an id of at least 1, or 0 with one warning when the bake fails.
    babylon::ui::FontId registerTtfFont(const babylon::font::Font& font, float pixelHeight);

    // Registers a decoded PNG as a drawable image. The painter copies the RGBA pixels into an
    // RGBA texture once and owns it until unregisterImage(). Returns an id of at least 1, or 0
    // with one warning for an empty image.
    babylon::ui::ImageId registerPngImage(const babylon::image::Image& image);

    // Loads <basePath>Data/ui/<relativePath> through the file system and registers it. relativePath
    // uses '/' and must be relative. A missing or invalid file logs one warning and returns 0.
    babylon::ui::FontId registerTtfFontFile(babylon::files::FileSystem& files,
        std::string_view relativePath, float pixelHeight);
    babylon::ui::ImageId registerPngImageFile(babylon::files::FileSystem& files,
        std::string_view relativePath);

    // Registers a font the painter draws with. pointSize is the size in canvas units it is drawn
    // at; the font is scaled from its own rdFont::fontSize. The painter borrows the font and does
    // not free it. Returns an id of at least 1, or 0 with one warning for a null font or a
    // non-positive pointSize. Ids stay valid until the painter is destroyed.
    babylon::ui::FontId registerFont(rdFont* pFont, float pointSize);

    // Registers an rdMaterial (one cel) the game owns as a drawable image. The painter borrows it;
    // the material and its texture must stay alive while a screen using the image can be drawn.
    // Returns an id of at least 1, or 0 with one warning for a null material or a cel out of range.
    babylon::ui::ImageId registerMaterialImage(const rdMaterial* pMaterial, int celNum);

    // Registers an RGB888 pixel block as an image: width x height pixels, rows top first, three
    // bytes B, G, R per pixel, the layout sithGamesave_ReadThumbnail() returns. The painter copies
    // the pixels into a texture once and owns it until unregisterImage(). Returns an id of at least
    // 1, or 0 with one warning for a null or empty image.
    babylon::ui::ImageId registerRgbImage(const std::uint8_t* pBgrPixels, std::uint32_t width,
        std::uint32_t height);

    // Releases an image. An image the painter created is freed; a registered material is only
    // forgotten. Safe for 0 and for an id that was already released.
    void unregisterImage(babylon::ui::ImageId image);

    void fillRect(const babylon::ui::Rect& rect, const babylon::ui::Color& color) override;
    void drawImage(babylon::ui::ImageId image, const babylon::ui::Rect& rect) override;
    void drawText(babylon::ui::FontId font, std::string_view text,
        const babylon::ui::Rect& rect, babylon::ui::TextAlign align,
        const babylon::ui::Color& color) override;
    babylon::ui::Size measureText(babylon::ui::FontId font, std::string_view text) const override;
    void pushClip(const babylon::ui::Rect& rect) override;
    void popClip() override;

private:
    struct ImageEntry
    {
        const rdMaterial* pMaterial = nullptr;
        int celNum                  = 0;
        rdMaterial* pOwnedMaterial  = nullptr;
    };

    struct FontEntry
    {
        enum class Kind
        {
            RdFont,
            TrueType
        };

        Kind kind             = Kind::RdFont;
        rdFont* pFont         = nullptr;
        float pointSize       = 0.0f;
        std::optional<babylon::font::Font> trueType;
        std::vector<char32_t> codepoints;
        std::vector<babylon::font::Glyph> glyphs;
        float ascent          = 0.0f;
        float descent         = 0.0f;
        float lineGap         = 0.0f;
        ImageEntry atlas;
    };

    static std::optional<std::vector<std::byte>> loadUiFile(
        babylon::files::FileSystem& files, std::string_view relativePath);
    bool createRgbaMaterial(const std::uint8_t* pRgbaPixels, std::uint32_t width,
        std::uint32_t height, ImageEntry& entry);
    void drawTrueTypeText(const FontEntry& entry, std::string_view text,
        const babylon::ui::Rect& rect, babylon::ui::TextAlign align,
        const babylon::ui::Color& color);
    babylon::ui::Size measureTrueTypeText(const FontEntry& entry,
        std::string_view text) const;

    // The canvas width and the number of back-buffer pixels per canvas unit, or false when there
    // is no back buffer.
    static bool getCanvasMetrics(float& canvasWidth, float& scale);

    // The back buffer size in canvas units, assuming RD_REF_WIDTH for the width while no back
    // buffer exists.
    static babylon::ui::Rect fullCanvas();

    babylon::ui::Rect currentClip() const;
    static babylon::ui::Rect intersect(const babylon::ui::Rect& a, const babylon::ui::Rect& b);
    static bool isEmpty(const babylon::ui::Rect& rect);

    // Draws one textured (pMaterial) or solid (null pMaterial) quad. The rectangle is in canvas
    // units and already clipped; u/v are the texture coordinates of its corners.
    void drawQuad(const babylon::ui::Rect& rect, const rdMaterial* pMaterial, int celNum,
        float u0, float v0, float u1, float v1, const babylon::ui::Color& color);

    void releaseImageEntry(ImageEntry& entry);

    std::vector<FontEntry> m_fonts;
    std::vector<ImageEntry> m_images;
    std::vector<babylon::ui::Rect> m_clips;
};

#endif // JONES3D_PLATFORM_BABYLON_JONESUIPAINTER_H
