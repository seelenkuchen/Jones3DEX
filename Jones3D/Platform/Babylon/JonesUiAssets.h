// Added: loads the menu's font and image assets for babylon::uirender::RenderPainter. The
// render painter owns no file system, so the game adapter reads Data/ui and uploads the decoded
// pixels. All paths are relative to Data/ui and use '/'.
#ifndef JONES3D_PLATFORM_BABYLON_JONESUIASSETS_H
#define JONES3D_PLATFORM_BABYLON_JONESUIASSETS_H
#ifndef __cplusplus
#error "JonesUiAssets.h is C++ only: it names babylon::uirender and babylon::files types."
#endif

#include <babylon/ui/Painter.h>

#include <cstdint>
#include <string_view>

namespace babylon::files
{
    class FileSystem;
}

namespace babylon::uirender
{
    class RenderPainter;
}

// Loads <basePath>Data/ui/<relativePath> as a TrueType font and registers it at
// canvasPixelHeight canvas units. Returns the font id, or 0 with one warning for a missing or
// invalid file, a font that does not parse, or a failed bake.
babylon::ui::FontId JonesUiAssets_LoadFont(babylon::uirender::RenderPainter& painter,
    babylon::files::FileSystem& files, std::string_view relativePath, float canvasPixelHeight);

// Loads <basePath>Data/ui/<relativePath> as a PNG and registers its pixels. Returns the image id,
// or 0 with one warning for a missing or invalid file, a broken PNG, or an empty image.
babylon::ui::ImageId JonesUiAssets_LoadPng(babylon::uirender::RenderPainter& painter,
    babylon::files::FileSystem& files, std::string_view relativePath);

// Widens width x height RGB888 pixels (three bytes B, G, R per pixel, rows top first, the layout
// sithGamesave_ReadThumbnail returns) to RGBA8 and registers them. Returns the image id, or 0 with
// one warning for a null or empty image.
babylon::ui::ImageId JonesUiAssets_AddRgbImage(babylon::uirender::RenderPainter& painter,
    const std::uint8_t* pBgrPixels, std::uint32_t width, std::uint32_t height);

#endif // JONES3D_PLATFORM_BABYLON_JONESUIASSETS_H
