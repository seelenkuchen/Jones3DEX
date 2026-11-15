// Added: loads the menu assets for babylon::uirender::RenderPainter; see JonesUiAssets.h.
#include <Jones3D/Platform/Babylon/JonesUiAssets.h>

#include <babylon/files/FileSystem.h>
#include <babylon/files/Path.h>
#include <babylon/font/Font.h>
#include <babylon/image/Image.h>
#include <babylon/uirender/RenderPainter.h>

#include <std/General/std.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace
{
    // The Data/ui file behind a relative path, or nullopt for an invalid path or a read failure.
    std::optional<std::vector<std::byte>> LoadUiFile(babylon::files::FileSystem& files,
        std::string_view relativePath)
    {
        if ( relativePath.empty() || relativePath.front() == '/'
            || !babylon::files::isValidPath(relativePath) )
        {
            STDLOG_WARNING("JonesUiAssets: invalid UI asset path '%.*s'.\n",
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

        std::vector<std::byte> bytes(static_cast<std::size_t>(*size));
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
}

babylon::ui::FontId JonesUiAssets_LoadFont(babylon::uirender::RenderPainter& painter,
    babylon::files::FileSystem& files, std::string_view relativePath, float canvasPixelHeight)
{
    const std::optional<std::vector<std::byte>> bytes = LoadUiFile(files, relativePath);
    if ( !bytes )
    {
        return 0;
    }

    const std::optional<babylon::font::Font> font =
        babylon::font::Font::load(std::move(*bytes));
    if ( !font )
    {
        STDLOG_WARNING("JonesUiAssets: '%s' is not a usable TrueType font.\n",
            std::string(relativePath).c_str());
        return 0;
    }

    return painter.addFont(*font, canvasPixelHeight);
}

babylon::ui::ImageId JonesUiAssets_LoadPng(babylon::uirender::RenderPainter& painter,
    babylon::files::FileSystem& files, std::string_view relativePath)
{
    const std::optional<std::vector<std::byte>> bytes = LoadUiFile(files, relativePath);
    if ( !bytes )
    {
        return 0;
    }

    const std::optional<babylon::image::Image> image = babylon::image::decodePng(*bytes);
    if ( !image )
    {
        STDLOG_WARNING("JonesUiAssets: '%s' is not a usable PNG.\n",
            std::string(relativePath).c_str());
        return 0;
    }

    return painter.addImage(*image);
}

babylon::ui::ImageId JonesUiAssets_AddRgbImage(babylon::uirender::RenderPainter& painter,
    const std::uint8_t* pBgrPixels, std::uint32_t width, std::uint32_t height)
{
    if ( !pBgrPixels || !width || !height )
    {
        STDLOG_WARNING("JonesUiAssets::AddRgbImage: bad image.\n");
        return 0;
    }

    const std::size_t pixelCount = (std::size_t)width * height;
    std::vector<std::uint8_t> rgba(pixelCount * 4u);
    for ( std::size_t index = 0; index < pixelCount; ++index )
    {
        rgba[index * 4u + 0u] = pBgrPixels[index * 3u + 2u];
        rgba[index * 4u + 1u] = pBgrPixels[index * 3u + 1u];
        rgba[index * 4u + 2u] = pBgrPixels[index * 3u + 0u];
        rgba[index * 4u + 3u] = 255;
    }

    babylon::image::Image image;
    image.width  = width;
    image.height = height;
    image.pixels = std::move(rgba);
    return painter.addImage(image);
}
