// Added: the GPU test of the game's babylon::ui painter (ADR-0026 amendment), next to the
// std/render-on-Babylon tests. It draws a sample settings screen with a synthetic rdFont and
// material, reads the back buffer of the SDL GPU device and writes it as ui-painter-sample.bmp.
// The synthetic font and material keep game assets out of the test. Without a GPU device it prints
// a skip and passes, unless BABYLON_REQUIRE_GPU=1 makes that a failure.
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <SDL3/SDL_stdinc.h>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/Window.h>
#include <babylon/render/Device.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>
#include <babylon/ui/Binding.h>
#include <babylon/ui/Screen.h>
#include <babylon/ui/Skin.h>
#include <babylon/ui/Types.h>
#include <babylon/ui/Ui.h>
#include <babylon/ui/Widget.h>

#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>

#include <rdroid/Main/rdroid.h>
#include <rdroid/Primitives/rdFont.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdRenderBabylon.h>

#include <std/General/std.h>
#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdMemory.h>

#include <sith/Dss/sithGamesave.h>
#include <sith/Dss/sithGamesaveSys.h>
#include <sith/types.h>

namespace
{
    using namespace babylon;
    using namespace babylon::ui;

    // Babylon's own print signature; tHostServices keeps the simple vararg one.
    int J3DAPI TestPrintf(const char* pFormat, ...)
    {
        va_list args;
        va_start(args, pFormat);
        int result = vfprintf(stderr, pFormat, args);
        va_end(args);
        return result;
    }

    tHostServices g_hostServices;

    void SetupHostServices()
    {
        memset(&g_hostServices, 0, sizeof(g_hostServices));
        g_hostServices.pStatusPrint  = TestPrintf;
        g_hostServices.pMessagePrint = TestPrintf;
        g_hostServices.pWarningPrint = TestPrintf;
        g_hostServices.pErrorPrint   = TestPrintf;
        g_hostServices.pDebugPrint   = TestPrintf;
        g_hostServices.pMalloc       = malloc;
        g_hostServices.pFree         = free;
        g_hostServices.pRealloc      = realloc;
        g_hostServices.pFileOpen     = stdFileOpen;
        g_hostServices.pFileClose    = stdFileClose;
        g_hostServices.pFileRead     = stdFileRead;
        g_hostServices.pFileGets     = stdFileGets;
        g_hostServices.pFileWrite    = stdFileWrite;
        g_hostServices.pFileEOF      = stdFileEof;
        g_hostServices.pFileTell     = stdFileTell;
        g_hostServices.pFileSeek     = stdFileSeek;
        g_hostServices.pFileSize     = stdFileSize;
        g_hostServices.pFilePrintf   = stdFilePrintf;
        g_hostServices.pFileGetws    = stdFileGetws;

        stdStartup(&g_hostServices);
        rdroid_g_pHS = &g_hostServices;
    }

    bool GpuRequired()
    {
        const char* pValue = SDL_getenv("BABYLON_REQUIRE_GPU");
        return pValue && strcmp(pValue, "1") == 0;
    }

    // Builds a 4 x 4 opaque white glyph atlas: every glyph draws an 8 x 12 unit white quad and
    // advances 8 units, the space advances without drawing.
    struct SyntheticFont
    {
        rdFont font;
        rdMaterial material;
        tSystemTexture texture;
        tVBuffer* pGlyphBuffer = nullptr;
        rdGlyphMetrics aGlyphs[256];

        bool startup()
        {
            memset(&font, 0, sizeof(font));
            memset(&material, 0, sizeof(material));
            memset(&texture, 0, sizeof(texture));
            memset(aGlyphs, 0, sizeof(aGlyphs));

            tRasterInfo raster;
            memset(&raster, 0, sizeof(raster));
            raster.width     = 4;
            raster.height    = 4;
            raster.colorInfo = stdColor_cfARGB8888;

            pGlyphBuffer = stdDisplay_VBufferNew(&raster, 0, 0);
            if ( !pGlyphBuffer )
            {
                return false;
            }

            memset(pGlyphBuffer->pPixels, 0xFF, pGlyphBuffer->rasterInfo.size);

            tVBuffer* aMips[1] = { pGlyphBuffer };
            std3D_AllocSystemTexture(&texture, aMips, 1u, STDCOLOR_FORMAT_RGBA);
            std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGBA);
            stdDisplay_VBufferFree(pGlyphBuffer);
            pGlyphBuffer = nullptr;
            if ( !texture.pCachedTexture )
            {
                return false;
            }

            material.formatType = STDCOLOR_FORMAT_RGBA;
            material.width      = 4;
            material.height     = 4;
            material.numCels    = 1;
            material.aTextures  = &texture;

            for ( size_t i = 0; i < 256; ++i )
            {
                aGlyphs[i].baselineOriginX = 8;
                aGlyphs[i].baselineOriginY = -1;
            }

            // Every printable ASCII character draws the same 8 x 12 white box out of the atlas.
            for ( size_t i = 32; i < 127; ++i )
            {
                rdGlyphMetrics& glyph = aGlyphs[i];
                glyph.left            = 0.0f;
                glyph.top             = 0.0f;
                glyph.right           = 8.0f / 256.0f;
                glyph.bottom          = 12.0f / 256.0f;
                glyph.baselineOriginY = 10;
                glyph.baselineOriginX = 8;
            }

            // The space advances but draws nothing.
            aGlyphs[(unsigned char)' '].left            = 0.0f;
            aGlyphs[(unsigned char)' '].top             = 0.0f;
            aGlyphs[(unsigned char)' '].right           = 0.0f;
            aGlyphs[(unsigned char)' '].bottom          = 0.0f;
            aGlyphs[(unsigned char)' '].baselineOriginY = -1;
            aGlyphs[(unsigned char)' '].baselineOriginX = 8;

            font.aGlyphs    = aGlyphs;
            font.lineSpacing = 16;
            font.pName      = const_cast<char*>("synthetic");
            font.fontSize   = 14;
            font.pMaterial  = &material;
            return true;
        }

        void shutdown()
        {
            std3D_ClearSystemTexture(&texture);
        }
    };

    struct PutU16
    {
        static void put(std::uint8_t* pData, std::uint16_t value)
        {
            pData[0] = (std::uint8_t)value;
            pData[1] = (std::uint8_t)(value >> 8);
        }
    };

    void PutU32(std::uint8_t* pData, std::uint32_t value)
    {
        pData[0] = (std::uint8_t)value;
        pData[1] = (std::uint8_t)(value >> 8);
        pData[2] = (std::uint8_t)(value >> 16);
        pData[3] = (std::uint8_t)(value >> 24);
    }

    // Writes width x height Bgra8 pixels, rows top first, as a 24-bit BMP.
    bool WriteBmp(const std::string& path, std::uint32_t width, std::uint32_t height,
        const std::uint8_t* pPixels)
    {
        const std::uint64_t rowBytes   = ((std::uint64_t)width * 3u + 3u) & ~std::uint64_t(3u);
        const std::uint64_t imageBytes = rowBytes * height;
        const std::uint64_t fileBytes  = 54u + imageBytes;
        if ( imageBytes > UINT32_MAX || fileBytes > UINT32_MAX )
        {
            return false;
        }

        std::uint8_t aHeader[54] = {};
        aHeader[0] = 'B';
        aHeader[1] = 'M';
        PutU32(&aHeader[2], (std::uint32_t)fileBytes);
        PutU32(&aHeader[10], 54u);
        PutU32(&aHeader[14], 40u);
        PutU32(&aHeader[18], width);
        PutU32(&aHeader[22], height);
        PutU16::put(&aHeader[26], 1u);
        PutU16::put(&aHeader[28], 24u);
        PutU32(&aHeader[34], (std::uint32_t)imageBytes);

        std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if ( !file )
        {
            return false;
        }

        file.write(reinterpret_cast<const char*>(aHeader), sizeof(aHeader));
        std::vector<std::uint8_t> row((size_t)rowBytes);
        for ( std::uint32_t y = 0; y < height && file; ++y )
        {
            const std::uint8_t* pSrc = pPixels + (size_t)(height - 1u - y) * width * 4u;
            memset(row.data(), 0, row.size());
            for ( std::uint32_t x = 0; x < width; ++x )
            {
                row[x * 3u + 0u] = pSrc[x * 4u + 0u];
                row[x * 3u + 1u] = pSrc[x * 4u + 1u];
                row[x * 3u + 2u] = pSrc[x * 4u + 2u];
            }

            file.write(reinterpret_cast<const char*>(row.data()), (std::streamsize)row.size());
        }

        file.close();
        return (bool)file;
    }

    std::uint8_t ToByte(float value)
    {
        return (std::uint8_t)(value * 255.0f);
    }

    bool Near(std::uint8_t value, std::uint8_t expected, int tolerance = 2)
    {
        return std::abs((int)value - (int)expected) <= tolerance;
    }

    struct Rgb
    {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
    };

    Rgb PixelAt(const std::vector<std::uint8_t>& pixels, std::uint32_t width,
        std::uint32_t x, std::uint32_t y)
    {
        const std::uint8_t* p = &pixels[((size_t)y * width + x) * 4u];
        return Rgb{ p[2], p[1], p[0] };
    }

    // A platform whose queue is empty for the loop's first pump and closes the screen with
    // Escape on the next one, so the modal loop draws exactly one frame before it returns.
    class ScriptedPlatform final : public platform::Platform
    {
    public:
        std::unique_ptr<platform::Window> createWindow(const platform::WindowDesc&) override
        {
            return nullptr;
        }

        std::optional<platform::Event> pollEvent() override
        {
            if ( m_emptyPolls > 0 )
            {
                --m_emptyPolls;
                return std::nullopt;
            }

            if ( m_sendClose )
            {
                m_sendClose = false;
                return platform::Event{
                    platform::KeyChanged{ 1, platform::Key::Escape, true, false } };
            }

            return std::nullopt;
        }

        // The test never asks for a box; a scripted platform has none to show.
        bool showMessageBox(std::string_view, std::string_view) override
        {
            return false;
        }

    private:
        int m_emptyPolls  = 1;
        bool m_sendClose  = true;
    };
}

TEST_CASE("the Jones painter draws a settings screen")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the painter image");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    platform::NullPlatform platform;
    cvars::Registry cvars;

    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform,
        &platform, nullptr, &cvars, pDevice.get() };
    stdBabylon_SetContext(&context);
    SetupHostServices();

    rdStartup();
    REQUIRE(rdOpen() == 1);

    REQUIRE(stdDisplay_Startup() == 1);
    REQUIRE(stdDisplay_Open(0u) == 1);
    stdDisplay_SetDefaultResolution(640u, 480u);
    REQUIRE(stdDisplay_SetMode(0u, 0, 1u) == 0);
    REQUIRE(std3D_Startup() == 1);
    REQUIRE(std3D_Open(0u) == 1);

    SyntheticFont syntheticFont;
    REQUIRE(syntheticFont.startup());

    JonesUiPainter painter;
    const FontId menuFont = painter.registerFont(&syntheticFont.font, 14.0f);
    REQUIRE(menuFont != 0);

    const Skin skin = JonesUiSkin_Create(menuFont);
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));
    CHECK(ui.canvasSize() == Size{ 640.0f, 480.0f });

    Screen screen;
    screen.title   = "Settings";
    screen.widgets = {
        Toggle{ "Fullbright", "", true },
        Slider{ "Gamma", "", 0.5, 2.0, 0.1, true },
        Choice{ "Resolution", "", { "640x480", "800x600", "1280x720" }, true },
        List{ "Save games", { ListItem{ "First", 0, 1 }, ListItem{ "Second", 0, 2 } }, 0, 1, true },
        Button{ "Delete", 2, false },
        Button{ "Back", 3, true },
    };
    screen.backResult = 3;
    REQUIRE(ui.push(screen) != kInvalidScreen);

    REQUIRE(std3D_StartScene() == 0);
    ui.draw();
    std3D_EndScene();
    REQUIRE(stdDisplay_Update() == 0);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-painter-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // The panel is opaque, so its pixels are exactly the skin color; find it and check the clear
    // color outside it.
    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    int minX = (int)width;
    int minY = (int)height;
    int maxX = -1;
    int maxY = -1;
    size_t panelPixels = 0;
    for ( std::uint32_t y = 0; y < height; ++y )
    {
        for ( std::uint32_t x = 0; x < width; ++x )
        {
            const Rgb pixel = PixelAt(pixels, width, x, y);
            if ( Near(pixel.r, panelColor.r) && Near(pixel.g, panelColor.g) && Near(pixel.b, panelColor.b) )
            {
                minX = std::min(minX, (int)x);
                minY = std::min(minY, (int)y);
                maxX = std::max(maxX, (int)x);
                maxY = std::max(maxY, (int)y);
                ++panelPixels;
            }
        }
    }

    REQUIRE(panelPixels > 1000u);
    REQUIRE(minX < maxX);
    REQUIRE(minY < maxY);

    const Rgb clear = PixelAt(pixels, width, 2u, 2u);
    CHECK(clear.r == 0);
    CHECK(clear.g == 0);
    CHECK(clear.b == 0);

    const Rgb inside = PixelAt(pixels, width, (std::uint32_t)(minX + 4), (std::uint32_t)(minY + 4));
    CHECK(Near(inside.r, panelColor.r));
    CHECK(Near(inside.g, panelColor.g));
    CHECK(Near(inside.b, panelColor.b));

    // The title is the only text in the top rows of the panel; a bright title-colored pixel there
    // proves the font drew.
    const Rgb titleColor = { ToByte(skin.title.r), ToByte(skin.title.g), ToByte(skin.title.b) };
    size_t titlePixels = 0;
    const int titleBottom = std::min((int)height, minY + (int)skin.titleHeight + 8);
    for ( int y = minY + 2; y < titleBottom; ++y )
    {
        for ( int x = minX + 3; x < maxX - 2; ++x )
        {
            const Rgb pixel = PixelAt(pixels, width, (std::uint32_t)x, (std::uint32_t)y);
            if ( Near(pixel.r, titleColor.r, 6) && Near(pixel.g, titleColor.g, 6)
                && Near(pixel.b, titleColor.b, 6) )
            {
                ++titlePixels;
            }
        }
    }

    CHECK(titlePixels > 20u);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the modal loop draws the message over the frozen frame")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the message image");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    ScriptedPlatform platform;
    cvars::Registry cvars;
    if ( stdBabylonEvents_PeekQuitRequest() )
    {
        static_cast<void>(stdBabylonEvents_TakeQuitRequest());
    }

    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform,
        nullptr, nullptr, &cvars, pDevice.get() };
    stdBabylon_SetContext(&context);
    SetupHostServices();

    rdStartup();
    REQUIRE(rdOpen() == 1);

    REQUIRE(stdDisplay_Startup() == 1);
    REQUIRE(stdDisplay_Open(0u) == 1);
    stdDisplay_SetDefaultResolution(640u, 480u);
    REQUIRE(stdDisplay_SetMode(0u, 0, 1u) == 0);
    REQUIRE(std3D_Startup() == 1);
    REQUIRE(std3D_Open(0u) == 1);

    SyntheticFont syntheticFont;
    REQUIRE(syntheticFont.startup());

    JonesUiPainter painter;
    const FontId menuFont = painter.registerFont(&syntheticFont.font, 14.0f);
    REQUIRE(menuFont != 0);

    const Skin skin = JonesUiSkin_Create(menuFont);
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    // A white frame behind the screen: after the loop draws the skin's translucent black dim over
    // it, a pixel outside the panel shows the dimmed white, which proves the captured frame was
    // drawn. The scene is left open like JonesHud_Process leaves it when it shows a message, so
    // the loop's own scene handling (end the caller's, capture, then reopen for the caller) runs.
    const babylon::ui::Color white{ 1.0f, 1.0f, 1.0f, 1.0f };
    REQUIRE(std3D_StartScene() == 0);
    painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 640.0f, 480.0f }, white);

    Screen screen;
    screen.title = "Message";
    screen.widgets = {
        Label{ "The level file is missing.", "" },
        Label{ "Insert CD 2.", "" },
        Button{ "OK", 1, true },
    };
    screen.backResult = 2;

    const int result = JonesUiModal_Run(ui, &painter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; });
    CHECK(result == 2);

    // The loop reopened the caller's scene before it returned; close it like the game frame does
    // so the frame is balanced. The pass loads the back buffer and draws nothing, so it keeps the
    // modal frame the readback checks.
    std3D_EndScene();
    REQUIRE(stdDisplay_Update() == 0);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-message-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // The corner is outside the centered panel: the frozen white frame under the dim layer.
    const std::uint8_t dimmed = (std::uint8_t)(255.0f * (1.0f - skin.dim.a));
    const Rgb corner = PixelAt(pixels, width, 2u, 2u);
    CHECK(Near(corner.r, dimmed, 6));
    CHECK(Near(corner.g, dimmed, 6));
    CHECK(Near(corner.b, dimmed, 6));

    // The panel itself is opaque and covers the dimmed frame.
    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    size_t panelPixels = 0;
    for ( std::uint32_t y = 0; y < height; ++y )
    {
        for ( std::uint32_t x = 0; x < width; ++x )
        {
            const Rgb pixel = PixelAt(pixels, width, x, y);
            if ( Near(pixel.r, panelColor.r) && Near(pixel.g, panelColor.g)
                && Near(pixel.b, panelColor.b) )
            {
                ++panelPixels;
            }
        }
    }
    CHECK(panelPixels > 1000u);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the Jones save screen draws thumbnails and the save list")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the save image");
        return;
    }

    const std::filesystem::path saveDir("ui-save-sample");
    std::error_code error;
    std::filesystem::remove_all(saveDir, error);
    std::filesystem::create_directories(saveDir, error);

    const auto writeSave = [&saveDir](const char* pName, const char* pLevel,
        std::uint8_t red, std::uint8_t green, std::uint8_t blue)
    {
        NdsHeader header;
        memset(&header, 0, sizeof(header));
        header.version    = SITHSAVEGAME_FILEVERSION;
        header.bThumbnail = 1;
        snprintf(header.aLevelFilename, sizeof(header.aLevelFilename), "%s", pLevel);

        std::vector<std::uint8_t> thumbnail(SITHSAVEGAME_THUMBSIZE);
        for ( std::size_t cur = 0; cur < thumbnail.size(); cur += 3 )
        {
            thumbnail[cur + 0] = blue;
            thumbnail[cur + 1] = green;
            thumbnail[cur + 2] = red;
        }

        std::ofstream file(saveDir / pName, std::ios::binary | std::ios::trunc);
        REQUIRE(file);
        file.write(reinterpret_cast<const char*>(&header), sizeof(header));
        file.write(reinterpret_cast<const char*>(thumbnail.data()),
            (std::streamsize)thumbnail.size());
        file.close();
        REQUIRE(file);
    };

    writeSave("first.nds", "FIRST LEVEL", 180, 40, 20);
    writeSave("second.nds", "SECOND LEVEL", 20, 120, 200);
    std::filesystem::last_write_time(saveDir / "first.nds",
        std::filesystem::file_time_type::clock::now() - std::chrono::seconds(50));
    std::filesystem::last_write_time(saveDir / "second.nds",
        std::filesystem::file_time_type::clock::now() - std::chrono::seconds(5));

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    ScriptedPlatform platform;
    cvars::Registry cvars;
    if ( stdBabylonEvents_PeekQuitRequest() )
    {
        static_cast<void>(stdBabylonEvents_TakeQuitRequest());
    }

    StdBabylonContext context{ pClock.get(), &virtualClock, pFileSystem.get(), &platform,
        nullptr, nullptr, &cvars, pDevice.get() };
    stdBabylon_SetContext(&context);
    SetupHostServices();

    rdStartup();
    REQUIRE(rdOpen() == 1);

    REQUIRE(stdDisplay_Startup() == 1);
    REQUIRE(stdDisplay_Open(0u) == 1);
    stdDisplay_SetDefaultResolution(640u, 480u);
    REQUIRE(stdDisplay_SetMode(0u, 0, 1u) == 0);
    REQUIRE(std3D_Startup() == 1);
    REQUIRE(std3D_Open(0u) == 1);

    SyntheticFont syntheticFont;
    REQUIRE(syntheticFont.startup());

    JonesUiPainter painter;
    const FontId menuFont = painter.registerFont(&syntheticFont.font, 14.0f);
    REQUIRE(menuFont != 0);

    const Skin skin = JonesUiSkin_Create(menuFont);
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    std::tm timeValue{};
    timeValue.tm_year = 126;
    timeValue.tm_mon  = 10;
    timeValue.tm_mday = 7;
    timeValue.tm_hour = 13;
    timeValue.tm_min  = 5;
    timeValue.tm_sec  = 9;

    char aPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    const int result = JonesUiSave_RunSaveScreen(ui, &painter, &painter, *pFileSystem,
        "ui-save-sample\\", "Sample Level", timeValue, aPath);
    CHECK(result == 2);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-save-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // The panel is drawn, and the first save's thumbnail is a red block inside it.
    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    size_t panelPixels = 0;
    size_t thumbnailPixels = 0;
    for ( std::uint32_t y = 0; y < height; ++y )
    {
        for ( std::uint32_t x = 0; x < width; ++x )
        {
            const Rgb pixel = PixelAt(pixels, width, x, y);
            if ( Near(pixel.r, panelColor.r) && Near(pixel.g, panelColor.g)
                && Near(pixel.b, panelColor.b) )
            {
                ++panelPixels;
            }

            // The newest save is selected first; its thumbnail is the blue block.
            if ( pixel.b > 150 && pixel.g > 80 && pixel.r < 60 )
            {
                ++thumbnailPixels;
            }
        }
    }
    CHECK(panelPixels > 1000u);
    CHECK(thumbnailPixels > 100u);

    std::filesystem::remove_all(saveDir, error);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

int main(int argc, char** argv)
{
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
