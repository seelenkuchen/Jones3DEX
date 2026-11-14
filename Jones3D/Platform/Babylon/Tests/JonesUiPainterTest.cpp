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
#include <cmath>
#include <cstdarg>
#include <deque>
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

#include <Jones3D/Display/JonesHudConstants.h>
#include <Jones3D/Display/jonesConfig.h>
#include <Jones3D/Main/JonesLevel.h>
#include <Jones3D/Platform/Babylon/JonesUiControls.h>
#include <Jones3D/Platform/Babylon/JonesUiDisplaySettings.h>
#include <Jones3D/Platform/Babylon/JonesUiMainMenu.h>
#include <Jones3D/Platform/Babylon/JonesUiContext.h>
#include <Jones3D/Platform/Babylon/JonesUiModal.h>
#include <Jones3D/Platform/Babylon/JonesUiPainter.h>
#include <Jones3D/Platform/Babylon/JonesUiEndScreens.h>
#include <Jones3D/Platform/Babylon/JonesUiSaveLoad.h>
#include <Jones3D/Platform/Babylon/JonesUiSkin.h>
#include <Jones3D/Platform/Babylon/JonesUiStore.h>

#include <rdroid/Main/rdroid.h>
#include <rdroid/Primitives/rdFont.h>
#include <rdroid/Raster/rdCache.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonEvents.h>
#include <std/Babylon/stdBabylonTour.h>
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

        // Overrides one glyph's advance so a case can build a font with mixed widths.
        void setAdvance(unsigned char chr, int advance)
        {
            aGlyphs[chr].baselineOriginX = advance;
        }

        void shutdown()
        {
            std3D_ClearSystemTexture(&texture);
        }
    };

    // A solid-color 8 x 8 material used as the main menu's load-screen background.
    struct SyntheticMaterial
    {
        rdMaterial material;
        tSystemTexture texture;

        bool startup(std::uint32_t argb)
        {
            memset(&material, 0, sizeof(material));
            memset(&texture, 0, sizeof(texture));

            tRasterInfo raster;
            memset(&raster, 0, sizeof(raster));
            raster.width     = 8;
            raster.height    = 8;
            raster.colorInfo = stdColor_cfARGB8888;

            tVBuffer* pBuffer = stdDisplay_VBufferNew(&raster, 0, 0);
            if ( !pBuffer )
            {
                return false;
            }

            std::uint32_t* pPixels = reinterpret_cast<std::uint32_t*>(pBuffer->pPixels);
            const std::size_t count = pBuffer->rasterInfo.size / sizeof(std::uint32_t);
            for ( std::size_t cur = 0; cur < count; ++cur )
            {
                pPixels[cur] = argb;
            }

            tVBuffer* aMips[1] = { pBuffer };
            std3D_AllocSystemTexture(&texture, aMips, 1u, STDCOLOR_FORMAT_RGBA);
            std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGBA);
            stdDisplay_VBufferFree(pBuffer);
            if ( !texture.pCachedTexture )
            {
                return false;
            }

            material.formatType = STDCOLOR_FORMAT_RGBA;
            material.width      = 8;
            material.height     = 8;
            material.numCels    = 1;
            material.aTextures  = &texture;
            return true;
        }

        void shutdown()
        {
            std3D_ClearSystemTexture(&texture);
        }
    };

    // Draws a full-screen colored quad at the wallpaper's far depth (RD_FIXEDPOINT_RHW_SCALE_X5),
    // the way rdWallpaper_Draw does. The painter's own quads sit at the near 2D depth.
    void DrawFarBackground(const babylon::ui::Color& color)
    {
        rdCacheProcEntry* pPoly = rdCache_GetAlphaProcEntry();
        if ( !pPoly )
        {
            return;
        }

        pPoly->flags        = RD_FF_TEX_TRANSLUCENT;
        pPoly->lightingMode = RD_LIGHTING_DIFFUSE;
        pPoly->numVertices  = 4;
        pPoly->pMaterial    = nullptr;
        pPoly->matCelNum    = 0;
        pPoly->extraLight.red   = color.r;
        pPoly->extraLight.green = color.g;
        pPoly->extraLight.blue  = color.b;
        pPoly->extraLight.alpha = color.a;
        pPoly->aVertIntensities[0] = pPoly->extraLight;

        const float z = RD_FIXEDPOINT_RHW_SCALE_X5;
        StdVertexTL* pVertex = pPoly->aVertices;
        pVertex->sx = 0.0f;
        pVertex->sy = 0.0f;
        pVertex->sz = z;
        pVertex->rhw = z;
        ++pVertex;
        pVertex->sx = 640.0f;
        pVertex->sy = 0.0f;
        pVertex->sz = z;
        pVertex->rhw = z;
        ++pVertex;
        pVertex->sx = 640.0f;
        pVertex->sy = 480.0f;
        pVertex->sz = z;
        pVertex->rhw = z;
        ++pVertex;
        pVertex->sx = 0.0f;
        pVertex->sy = 480.0f;
        pVertex->sz = z;
        pVertex->rhw = z;

        rdCache_AddAlphaProcFace(4);
        rdCache_FlushAlpha();
        rdCache_Flush();
    }

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

    // Counts pixels in a rectangle close to color. The rectangle is clamped to the image.
    size_t CountColor(const std::vector<std::uint8_t>& pixels, std::uint32_t width,
        std::uint32_t height, float x0, float y0, float x1, float y1,
        const Rgb& color, int tolerance = 8)
    {
        const int left   = std::max(0, (int)std::floor(x0));
        const int top    = std::max(0, (int)std::floor(y0));
        const int right  = std::min((int)width, (int)std::ceil(x1));
        const int bottom = std::min((int)height, (int)std::ceil(y1));

        size_t count = 0;
        for ( int y = top; y < bottom; ++y )
        {
            for ( int x = left; x < right; ++x )
            {
                const Rgb pixel = PixelAt(pixels, width, (std::uint32_t)x, (std::uint32_t)y);
                if ( Near(pixel.r, color.r, tolerance) && Near(pixel.g, color.g, tolerance)
                    && Near(pixel.b, color.b, tolerance) )
                {
                    ++count;
                }
            }
        }

        return count;
    }

    // The shared widget-text check: the panel and border colors are dark or golden, and the title
    // is yellow, so only the labels pass this.
    bool IsBrightText(const Rgb& pixel)
    {
        return pixel.r >= 200 && pixel.g >= 200 && pixel.b >= 180;
    }

    size_t CountBrightText(const std::vector<std::uint8_t>& pixels, std::uint32_t width,
        float x0, float y0, float x1, float y1)
    {
        const int left   = std::max(0, (int)std::floor(x0));
        const int top    = std::max(0, (int)std::floor(y0));
        const int right  = (int)std::ceil(x1);
        const int bottom = (int)std::ceil(y1);

        size_t count = 0;
        for ( int y = top; y < bottom; ++y )
        {
            for ( int x = left; x < right; ++x )
            {
                if ( IsBrightText(PixelAt(pixels, width, (std::uint32_t)x, (std::uint32_t)y)) )
                {
                    ++count;
                }
            }
        }

        return count;
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

        std::optional<std::size_t> askChoice(std::string_view, std::string_view,
            std::span<const std::string_view>) override
        {
            return std::nullopt;   // these tests never ask for a dialog
        }

        platform::FolderChoice chooseFolder(std::string_view, std::string_view) override
        {
            return {};   // these tests never ask for a dialog
        }

        bool rumbleGamepad(platform::GamepadId, float, float, std::uint32_t) override
        {
            return false;
        }

    private:
        int m_emptyPolls  = 1;
        bool m_sendClose  = true;
    };

    // A platform for the store sample: one empty frame first, then queued events one frame each
    // (each event followed by an empty poll to end its frame). The store sample needs the store
    // drawn, closed by back (which prompts), answered No, drawn again, and then closed by a quit.
    class StoreSamplePlatform final : public platform::Platform
    {
    public:
        std::unique_ptr<platform::Window> createWindow(const platform::WindowDesc&) override
        {
            return nullptr;
        }

        std::optional<platform::Event> pollEvent() override
        {
            if ( m_delivering )
            {
                m_delivering = false;
                return std::nullopt;
            }

            if ( m_emptyPolls > 0 )
            {
                --m_emptyPolls;
                return std::nullopt;
            }

            if ( m_events.empty() )
            {
                return std::nullopt;
            }

            m_delivering = true;
            platform::Event event = m_events.front();
            m_events.pop_front();
            return event;
        }

        bool showMessageBox(std::string_view, std::string_view) override
        {
            return false;
        }

        std::optional<std::size_t> askChoice(std::string_view, std::string_view,
            std::span<const std::string_view>) override
        {
            return std::nullopt;   // these tests never ask for a dialog
        }

        platform::FolderChoice chooseFolder(std::string_view, std::string_view) override
        {
            return {};   // these tests never ask for a dialog
        }

        bool rumbleGamepad(platform::GamepadId, float, float, std::uint32_t) override
        {
            return false;
        }

        void PushKey(platform::Key key)
        {
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, true, false } });
            m_events.push_back(platform::Event{
                platform::KeyChanged{ 1, key, false, false } });
        }

        void PushQuit()
        {
            m_events.push_back(platform::Event{ platform::QuitRequested{} });
        }

    private:
        std::deque<platform::Event> m_events;
        int m_emptyPolls = 1;
        bool m_delivering = false;
    };

    // Installs a hand-built context as the active ui session for the modal loop and clears it
    // before the context is destroyed, so every screen in the case shares one background.
    class TestUiSession
    {
    public:
        explicit TestUiSession(JonesUiContext& context)
        {
            JonesUiContext_SetActiveForTest(&context);
        }

        ~TestUiSession()
        {
            JonesUiContext_SetActiveForTest(nullptr);
        }

        TestUiSession(const TestUiSession&) = delete;
        TestUiSession& operator=(const TestUiSession&) = delete;
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
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

TEST_CASE("a nested modal reuses the outermost frozen background")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the nested modal image");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    StoreSamplePlatform platform;
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    // One held session behind both screens, as the main menu and pause menu hold their context.
    JonesUiContext uiContext;
    TestUiSession session(uiContext);

    // A white frame for the session to capture before it draws its first screen.
    const babylon::ui::Color white{ 1.0f, 1.0f, 1.0f, 1.0f };
    REQUIRE(std3D_StartScene() == 0);
    painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 640.0f, 480.0f }, white);

    Screen outer;
    outer.title   = "Outer";
    outer.widgets = { Button{ "Inner", 1, true } };
    outer.backResult = 2;

    Screen inner;
    inner.title   = "Inner";
    inner.widgets = { Button{ "OK", 1, true } };
    inner.backResult = 2;

    const std::uint8_t dimmed = (std::uint8_t)(255.0f * (1.0f - skin.dim.a));
    bool bInnerChecked = false;
    Rgb innerCorner{};
    std::uint32_t innerWidth  = 0;
    std::uint32_t innerHeight = 0;

    JonesUiModalEventHandler outerHandler = [&](Ui& uiRef, ScreenId screenId,
        const UiEvent& event) -> bool
    {
        const ActionPressed* pAction = std::get_if<ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId || pAction->action != 1 )
        {
            return false;
        }

        const int innerResult = JonesUiModal_Run(uiRef, &painter, inner,
            [](babylon::ui::ResultCode code) { return (int)code; });
        CHECK(innerResult == 2);

        // Read the child's last frame before the outer loop draws again. Its corner must show the
        // outer white frame dimmed once, not that frame captured and dimmed a second time.
        std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
        REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));
        innerCorner = PixelAt(pixels, width, 2u, 2u);
        innerWidth  = width;
        innerHeight = height;
        bInnerChecked = true;
        return true;
    };

    platform.PushKey(platform::Key::Enter);
    platform.PushKey(platform::Key::Escape);
    platform.PushKey(platform::Key::Escape);

    const int result = JonesUiModal_RunHandled(ui, &painter, outer,
        [](babylon::ui::ResultCode code) { return (int)code; }, outerHandler);
    CHECK(result == 2);

    REQUIRE(bInnerChecked);
    CHECK(innerWidth == 640u);
    CHECK(innerHeight == 480u);
    CHECK(Near(innerCorner.r, dimmed, 6));
    CHECK(Near(innerCorner.g, dimmed, 6));
    CHECK(Near(innerCorner.b, dimmed, 6));

    // After the child closed, the outer screen draws over the same original background.
    std3D_EndScene();
    REQUIRE(stdDisplay_Update() == 0);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const Rgb outerCorner = PixelAt(pixels, width, 2u, 2u);
    CHECK(Near(outerCorner.r, dimmed, 6));
    CHECK(Near(outerCorner.g, dimmed, 6));
    CHECK(Near(outerCorner.b, dimmed, 6));

    // Release the session frame while the device is still open.
    painter.unregisterImage(uiContext.modalFrozenImage);
    uiContext.modalFrozenImage = 0;

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("a nested modal reuses the outermost background callback")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the nested background callback");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    StoreSamplePlatform platform;
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    JonesUiContext uiContext;
    TestUiSession session(uiContext);

    const babylon::ui::Color green{ 0.0f, 1.0f, 0.0f, 1.0f };
    int backgroundCalls = 0;
    JonesUiModalBackground background;
    background.draw = [&painter, &backgroundCalls, &green]()
    {
        ++backgroundCalls;
        painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 640.0f, 480.0f }, green);
    };

    Screen outer;
    outer.title   = "Outer";
    outer.widgets = { Button{ "Inner", 1, true } };
    outer.backResult = 2;

    Screen inner;
    inner.title   = "Inner";
    inner.widgets = { Button{ "OK", 1, true } };
    inner.backResult = 2;

    bool bInnerChecked = false;
    int callsDuringInner = 0;
    Rgb innerCorner{};

    JonesUiModalEventHandler outerHandler = [&](Ui& uiRef, ScreenId screenId,
        const UiEvent& event) -> bool
    {
        const ActionPressed* pAction = std::get_if<ActionPressed>(&event);
        if ( !pAction || pAction->screen != screenId || pAction->action != 1 )
        {
            return false;
        }

        const int before = backgroundCalls;
        const int innerResult = JonesUiModal_Run(uiRef, &painter, inner,
            [](babylon::ui::ResultCode code) { return (int)code; });
        CHECK(innerResult == 2);
        callsDuringInner = backgroundCalls - before;

        std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
        REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));
        innerCorner = PixelAt(pixels, width, 2u, 2u);
        bInnerChecked = true;
        return true;
    };

    platform.PushKey(platform::Key::Enter);
    platform.PushKey(platform::Key::Escape);
    platform.PushKey(platform::Key::Escape);

    const int result = JonesUiModal_RunHandled(ui, &painter, outer,
        [](babylon::ui::ResultCode code) { return (int)code; }, outerHandler, background);
    CHECK(result == 2);

    REQUIRE(bInnerChecked);
    // The child drew through the parent's callback: full green, not a captured and dimmed copy.
    CHECK(callsDuringInner >= 1);
    CHECK(Near(innerCorner.r, 0, 6));
    CHECK(Near(innerCorner.g, 255, 6));
    CHECK(Near(innerCorner.b, 0, 6));

    std3D_EndScene();
    REQUIRE(stdDisplay_Update() == 0);

    // Release the session frame while the device is still open.
    painter.unregisterImage(uiContext.modalFrozenImage);
    uiContext.modalFrozenImage = 0;

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("a screen after another closes draws over the session background")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the sequential modal image");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    StoreSamplePlatform platform;
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    // The main menu or pause menu holds one context while it opens screen after screen.
    JonesUiContext uiContext;
    TestUiSession session(uiContext);

    const babylon::ui::Color white{ 1.0f, 1.0f, 1.0f, 1.0f };
    REQUIRE(std3D_StartScene() == 0);
    painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 640.0f, 480.0f }, white);

    Screen first;
    first.title   = "First";
    first.widgets = { Button{ "Close", 1, true } };
    first.backResult = 2;

    Screen second;
    second.title   = "Second";
    second.widgets = { Button{ "Close", 1, true } };
    second.backResult = 2;

    platform.PushKey(platform::Key::Escape);
    platform.PushKey(platform::Key::Escape);

    CHECK(JonesUiModal_Run(ui, &painter, first,
        [](babylon::ui::ResultCode code) { return (int)code; }) == 2);
    // The first screen has closed, but the second must still draw over the session's original
    // white frame, not capture and dim the frame the first screen left behind.
    CHECK(JonesUiModal_Run(ui, &painter, second,
        [](babylon::ui::ResultCode code) { return (int)code; }) == 2);

    std3D_EndScene();
    REQUIRE(stdDisplay_Update() == 0);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::uint8_t dimmed = (std::uint8_t)(255.0f * (1.0f - skin.dim.a));
    const Rgb corner = PixelAt(pixels, width, 2u, 2u);
    CHECK(Near(corner.r, dimmed, 6));
    CHECK(Near(corner.g, dimmed, 6));
    CHECK(Near(corner.b, dimmed, 6));

    // Release the session frame while the device is still open.
    painter.unregisterImage(uiContext.modalFrozenImage);
    uiContext.modalFrozenImage = 0;

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("a modal frame clears the previous screen's panel from the background")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the stale panel image");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    StoreSamplePlatform platform;
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    // Content-sized and centered: this test is about the modal frame, not the menu layout.
    skin.placement = PanelPlacement::Center;
    skin.fixedPanelWidth = 0.0f;
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    JonesUiContext uiContext;
    TestUiSession session(uiContext);

    const babylon::ui::Color green{ 0.0f, 1.0f, 0.0f, 1.0f };
    JonesUiModalBackground background;
    background.draw = [&green]()
    {
        DrawFarBackground(green);
    };

    // A wide first panel. The second screen has no title or widgets, so it draws only a small
    // centered panel and leaves this point for the background.
    Screen first;
    first.title   = "First";
    first.widgets = { Label{ std::string(100, 'M'), "" } };
    first.backResult = 2;

    Screen empty;
    empty.backResult = 2;

    platform.PushKey(platform::Key::Escape);
    platform.PushKey(platform::Key::Escape);

    CHECK(JonesUiModal_Run(ui, &painter, first,
        [](babylon::ui::ResultCode code) { return (int)code; }, background) == 2);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    // This point is inside the wide panel and outside the next screen's small one.
    const Rgb panelCorner = PixelAt(pixels, width, 20u, 240u);
    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    CHECK(Near(panelCorner.r, panelColor.r, 6));
    CHECK(Near(panelCorner.g, panelColor.g, 6));
    CHECK(Near(panelCorner.b, panelColor.b, 6));

    CHECK(JonesUiModal_Run(ui, &painter, empty,
        [](babylon::ui::ResultCode code) { return (int)code; }, background) == 2);

    width  = stdRenderBabylon_GetBackBufferWidth();
    height = stdRenderBabylon_GetBackBufferHeight();
    pixels.assign((size_t)width * height * 4u, 0);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const Rgb backgroundCorner = PixelAt(pixels, width, 20u, 240u);
    CHECK(Near(backgroundCorner.r, 0, 6));
    CHECK(Near(backgroundCorner.g, 255, 6));
    CHECK(Near(backgroundCorner.b, 0, 6));

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the main menu draws the level map behind its panel")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the main menu image");
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
    SyntheticMaterial backgroundMaterial;
    REQUIRE(backgroundMaterial.startup(STD_RGBA(20, 110, 90, 255)));

    JonesUiPainter painter;
    const FontId menuFont = painter.registerFont(&syntheticFont.font, 14.0f);
    REQUIRE(menuFont != 0);

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    const ImageId backgroundImage =
        painter.registerMaterialImage(&backgroundMaterial.material, 0);
    REQUIRE(backgroundImage != 0);

    // The 8 x 8 source is cover-scaled to the 640 x 480 canvas; the corner shows it undimmed.
    const JonesUiModalBackground background{ backgroundImage, 8.0f, 8.0f };
    JonesUiMainMenuHooks hooks;
    char aOutLoadPath[JONESCONFIG_GAMESAVE_FILEPATHSIZE] = { 0 };
    CHECK(JonesUiMainMenu_Run(ui, &painter, background, true, hooks, aOutLoadPath)
        == JonesUiMainMenuAction::Quit);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-mainmenu-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    const Rgb corner = PixelAt(pixels, width, 2u, 2u);
    CHECK(Near(corner.r, 20, 8));
    CHECK(Near(corner.g, 110, 8));
    CHECK(Near(corner.b, 90, 8));

    // The menu panel is opaque over the map.
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

    painter.unregisterImage(backgroundImage);

    backgroundMaterial.shutdown();
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
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

TEST_CASE("the Jones level completed screen draws its statistics")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the level completed image");
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    SithGameStatistics statistics;
    memset(&statistics, 0, sizeof(statistics));
    statistics.curLevelNum = JONESLEVEL_LASTLEVELNUM;

    int balance   = 500;
    int aItems[14] = { 0 };
    const int result = JonesUiLevelCompleted_RunLevelCompletedScreen(ui, &painter, statistics,
        0x0205u, 41, 4, 100, 200, &balance, aItems, 11);
    CHECK(result == 2);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-levelcompleted-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // The panel is drawn, and the title and row text are the bright skin colors over it.
    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    const Rgb titleColor = { ToByte(skin.title.r), ToByte(skin.title.g), ToByte(skin.title.b) };
    size_t panelPixels = 0;
    size_t titlePixels = 0;
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

            if ( Near(pixel.r, titleColor.r, 6) && Near(pixel.g, titleColor.g, 6)
                && Near(pixel.b, titleColor.b, 6) )
            {
                ++titlePixels;
            }
        }
    }
    CHECK(panelPixels > 1000u);
    CHECK(titlePixels > 20u);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the Jones store screen draws its item list")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the store image");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    StoreSamplePlatform platform;
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    int aState[kJonesStoreItemCount] = {};
    for ( std::size_t item = 0; item < kJonesStoreItemCount; ++item )
    {
        aState[item] = (int)(((std::uint32_t)JonesHud_aStoreItems[item].menuID << 16)
            | ((std::uint32_t)item << 4) | 1u);
    }

    // Draw the store, close it with back (the original's WM_CLOSE prompts), answer No so it
    // reopens, let the reopened store draw, then quit to close it without another prompt.
    platform.PushKey(platform::Key::Escape);
    platform.PushKey(platform::Key::Escape);
    platform.PushQuit();

    int balance = 1200;
    const int result = JonesUiStore_RunStoreScreen(ui, &painter, &balance, aState, 11);
    CHECK(result == 2);
    if ( stdBabylonEvents_PeekQuitRequest() )
    {
        static_cast<void>(stdBabylonEvents_TakeQuitRequest());
    }

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-store-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // The panel is drawn, and the title is the bright skin title color over it.
    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    const Rgb titleColor = { ToByte(skin.title.r), ToByte(skin.title.g), ToByte(skin.title.b) };
    size_t panelPixels = 0;
    size_t titlePixels = 0;
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

            if ( Near(pixel.r, titleColor.r, 6) && Near(pixel.g, titleColor.g, 6)
                && Near(pixel.b, titleColor.b, 6) )
            {
                ++titlePixels;
            }
        }
    }
    CHECK(panelPixels > 1000u);
    CHECK(titlePixels > 20u);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the Jones display screen draws the settings list")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the display image");
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    const std::uint32_t aSizes[][2] = {
        { 512u, 384u }, { 640u, 480u }, { 800u, 600u }, { 1024u, 768u },
    };

    std::vector<StdVideoMode> aModes;
    for ( const auto& aSize : aSizes )
    {
        StdVideoMode mode;
        memset(&mode, 0, sizeof(mode));
        mode.rasterInfo.width               = aSize[0];
        mode.rasterInfo.height              = aSize[1];
        mode.rasterInfo.colorInfo.bpp       = 32;
        mode.rasterInfo.colorInfo.colorMode = STDCOLOR_RGB;
        mode.refreshRate                    = 60;
        aModes.push_back(mode);
    }

    StdDisplayInfo info;
    memset(&info, 0, sizeof(info));
    snprintf(info.displayDevice.aDriverName, sizeof(info.displayDevice.aDriverName), "%s",
        "Sample display");
    info.displayDevice.bHAL = 1;
    info.aModes             = aModes.data();
    info.numModes           = aModes.size();

    StdDisplayEnvironment env;
    memset(&env, 0, sizeof(env));
    env.numInfos      = 1;
    env.aDisplayInfos = &info;

    JonesDisplaySettings settings;
    memset(&settings, 0, sizeof(settings));
    settings.displayDeviceNum = 0;
    settings.videoModeNum     = 1;
    settings.filter           = STD3D_MIPMAPFILTER_TRILINEAR;
    settings.width            = 640;
    settings.height           = 480;
    settings.bFog             = 1;
    settings.geoMode          = RD_GEOMETRY_FULL;
    settings.lightMode        = RD_LIGHTING_GOURAUD;
    settings.fogDensity       = 1.0f;

    const int result = JonesUiDisplay_RunDisplayScreen(ui, &painter, &env, &settings);
    CHECK(result == 2);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-display-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // The centered panel is drawn over the frozen frame.
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

TEST_CASE("the Jones controls screen draws the action list")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the controls image");
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    JonesControlsScheme scheme;
    REQUIRE(JonesControlSchemes_SetDefaultScheme(&scheme, 0) == 1);
    std::snprintf(scheme.aName, sizeof(scheme.aName), "%s", "Sample scheme");

    // The default ScriptedPlatform draws one frame and then closes with Escape.
    const int result = JonesUiControls_RunEditScreen(ui, &painter, scheme, 3);
    CHECK(result == 2);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    const std::string bmpPath = "ui-controls-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the scripted tour drives a modal screen by present count")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the scripted modal tour");
        return;
    }

    std::unique_ptr<files::FileSystem> pFileSystem = files::createSdlFileSystem();
    std::unique_ptr<time::Clock> pClock = time::createSdlClock();
    time::NullClock virtualClock;
    platform::NullPlatform platform;
    cvars::Registry cvars;
    if ( stdBabylonEvents_PeekQuitRequest() )
    {
        static_cast<void>(stdBabylonEvents_TakeQuitRequest());
    }

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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    // The script's frame number is the present count: the screen opens on present 1, Down is
    // delivered at 2 and Enter at 3, so the second button is the answer.
    const std::filesystem::path scriptPath("ui-tour-script.txt");
    const std::filesystem::path logPath("ui-tour-present.log");
    const std::filesystem::path shotDir("ui-tour-shots");
    std::error_code removeError;
    std::filesystem::remove(scriptPath, removeError);
    std::filesystem::remove(logPath, removeError);
    std::filesystem::remove_all(shotDir, removeError);
    {
        std::ofstream script(scriptPath, std::ios::binary | std::ios::trunc);
        REQUIRE(script);
        script << "frame 2: key Down down\n"
                  "frame 3: key Enter down\n";
    }

    StdBabylonTourOptions options;
    options.inputPath        = scriptPath.string();
    options.presentLogPath   = logPath.string();
    options.screenshotDir    = shotDir.string();
    options.screenshotFrames = { 1u };
    std::string error;
    REQUIRE(stdBabylonTour_Configure(options, 0, error));
    struct TourGuard
    {
        ~TourGuard() { stdBabylonTour_Shutdown(); }
    } tourGuard;

    Screen screen;
    screen.title   = "Scripted";
    screen.widgets = {
        Button{ "One", 1, true },
        Button{ "Two", 2, true },
    };
    screen.backResult = 2;

    const int result = JonesUiModal_Run(ui, &painter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; });
    CHECK(result == 2);
    CHECK(stdBabylonTour_GetPresentCount() == 3u);
    stdBabylonTour_Shutdown();

    // Every present names the open screen in the log.
    std::ifstream log(logPath, std::ios::binary);
    std::string line;
    const char* const aExpectedLog[] = {
        "frame 1: Scripted",
        "frame 2: Scripted",
        "frame 3: Scripted",
    };
    for ( const char* pExpected : aExpectedLog )
    {
        REQUIRE(std::getline(log, line));
        CHECK(line == pExpected);
    }

    // The requested present was written as a 640 x 480, 24-bit BMP.
    std::ifstream bmp(shotDir / "frame-1.bmp", std::ios::binary);
    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(bmp)),
        std::istreambuf_iterator<char>());
    const std::uint64_t stride = ((std::uint64_t)640u * 3u + 3u) & ~std::uint64_t(3u);
    REQUIRE(bytes.size() == 54u + stride * 480u);
    CHECK(bytes[0] == 'B');
    CHECK(bytes[1] == 'M');
    CHECK(bytes[18] == (std::uint8_t)(640u & 0xFFu));
    CHECK(bytes[19] == (std::uint8_t)(640u >> 8));
    CHECK(bytes[22] == (std::uint8_t)(480u & 0xFFu));
    CHECK(bytes[23] == (std::uint8_t)(480u >> 8));
    CHECK(bytes[28] == 24u);

    std::filesystem::remove(scriptPath, removeError);
    std::filesystem::remove(logPath, removeError);
    std::filesystem::remove_all(shotDir, removeError);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the painter draws labels taller than the old skin row")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the tall-font image");
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
    // The font's line height at this point size is taller than the old 24-unit skin row.
    const FontId tallFont = painter.registerFont(&syntheticFont.font, 28.0f);
    REQUIRE(tallFont != 0);
    const float lineHeight = painter.measureText(tallFont, "").height;
    REQUIRE(lineHeight > 24.0f);

    // The old skin metrics, kept to prove the painter no longer drops a line that does not fit.
    Skin oldSkin = JonesUiSkin_Create(tallFont, tallFont, &painter);
    // Content-sized and centered: the row bands below use that layout.
    oldSkin.placement = PanelPlacement::Center;
    oldSkin.fixedPanelWidth = 0.0f;
    oldSkin.rowHeight   = 24.0f;
    oldSkin.titleHeight = 32.0f;

    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(oldSkin));
    REQUIRE(ui.setViewport(640, 480));

    Screen screen;
    screen.widgets = {
        Button{ "First", 1, true },
        Button{ "Second", 2, true },
        Button{ "Third", 3, true },
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

    const std::string bmpPath = "ui-tall-font-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    // Every button label sits in its own row band and must have been drawn even though the font is
    // taller than the old row.
    const float contentHeight = oldSkin.titleHeight
        + 3.0f * (oldSkin.rowSpacing + oldSkin.rowHeight);
    const float panelHeight = contentHeight + 2.0f * oldSkin.panelPadding;
    const float rowsStart   = (480.0f - panelHeight) * 0.5f + oldSkin.panelPadding
        + oldSkin.titleHeight + oldSkin.rowSpacing;
    for ( int row = 0; row < 3; ++row )
    {
        const float rowY = rowsStart
            + (float)row * (oldSkin.rowSpacing + oldSkin.rowHeight);
        CAPTURE(row);
        CHECK(CountBrightText(pixels, width, 0.0f, rowY, (float)width,
            rowY + oldSkin.rowHeight) > 20u);
    }

    // The new skin makes the rows tall enough for the font.
    const Skin newSkin = JonesUiSkin_Create(tallFont, tallFont, &painter);
    CHECK(newSkin.rowHeight >= lineHeight);
    CHECK(newSkin.titleHeight >= lineHeight);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the painter measures the width it draws")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the measured-width image");
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

    // Mixed glyph widths and a point size different from the font's own size, so the old
    // truncated measurement and the drawn advance disagree.
    syntheticFont.setAdvance((unsigned char)'M', 16);
    syntheticFont.setAdvance((unsigned char)'i', 3);

    JonesUiPainter painter;
    const FontId menuFont = painter.registerFont(&syntheticFont.font, 20.0f);
    REQUIRE(menuFont != 0);

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    // Content-sized and centered: this test checks that the measured width is the drawn width, not
    // the fixed menu layout.
    skin.placement = PanelPlacement::Center;
    skin.fixedPanelWidth = 0.0f;
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    // No widgets and this title as the widest content: the panel's text area is exactly the
    // measured title width, so a width mismatch drops the first and last glyphs.
    const std::string title = std::string("M") + std::string(100, 'i');
    Screen screen;
    screen.title = title;
    screen.backResult = 2;
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

    const std::string bmpPath = "ui-measured-width-sample.bmp";
    REQUIRE(WriteBmp(bmpPath, width, height, pixels.data()));
    MESSAGE("wrote " << std::filesystem::absolute(bmpPath).string());

    const float measured   = painter.measureText(menuFont, title).width;
    const float panelWidth = std::min(std::max(skin.minPanelWidth,
        measured + 2.0f * skin.panelPadding), 640.0f - 2.0f * skin.canvasMargin);
    const float innerX     = (640.0f - panelWidth) * 0.5f + skin.panelPadding;
    const float innerWidth = panelWidth - 2.0f * skin.panelPadding;
    const float panelHeight = skin.titleHeight + 2.0f * skin.panelPadding;
    const float titleY      = (480.0f - panelHeight) * 0.5f + skin.panelPadding;

    const Rgb titleColor = { ToByte(skin.title.r), ToByte(skin.title.g), ToByte(skin.title.b) };

    // Both ends of the title are drawn: the first glyph starts at the text area's left edge and
    // the last one ends at its right edge.
    CHECK(CountColor(pixels, width, height, innerX, titleY,
        innerX + 12.0f, titleY + skin.titleHeight, titleColor) > 0u);
    CHECK(CountColor(pixels, width, height, innerX + innerWidth - 10.0f, titleY,
        innerX + innerWidth + 10.0f, titleY + skin.titleHeight, titleColor) > 0u);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the modal loop calls the background callback before the screen")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the background-callback image");
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

    Skin skin = JonesUiSkin_Create(menuFont, menuFont, &painter);
    skin.panel = Color{ 0.05f, 0.06f, 0.09f, 1.0f }; // opaque and distinct from the clear black
    Ui ui(painter, cvars);
    REQUIRE(ui.setSkin(skin));
    REQUIRE(ui.setViewport(640, 480));

    const babylon::ui::Color green{ 0.0f, 1.0f, 0.0f, 1.0f };
    int backgroundCalls = 0;
    JonesUiModalBackground background;
    background.draw = [&painter, &backgroundCalls, &green]()
    {
        ++backgroundCalls;
        painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 640.0f, 480.0f }, green);
    };

    Screen screen;
    screen.title   = "Message";
    screen.widgets = {
        Label{ "The level file is missing.", "" },
        Button{ "OK", 1, true },
    };
    screen.backResult = 2;

    const int result = JonesUiModal_Run(ui, &painter, screen,
        [](babylon::ui::ResultCode code) { return (int)code; }, background);
    CHECK(result == 2);
    // The scripted platform draws exactly one frame before it closes the screen.
    CHECK(backgroundCalls == 1);

    std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
    std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
    REQUIRE(width == 640u);
    REQUIRE(height == 480u);

    std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
    REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

    // The callback filled the whole frame green first; the opaque panel was drawn over it after.
    const Rgb corner = PixelAt(pixels, width, 2u, 2u);
    CHECK(Near(corner.r, 0, 6));
    CHECK(Near(corner.g, 255, 6));
    CHECK(Near(corner.b, 0, 6));

    const Rgb panelColor = { ToByte(skin.panel.r), ToByte(skin.panel.g), ToByte(skin.panel.b) };
    CHECK(CountColor(pixels, width, height, 0.0f, 0.0f, (float)width, (float)height,
        panelColor) > 1000u);

    std3D_ClearSystemTexture(&syntheticFont.texture);
    std3D_Shutdown();
    stdDisplay_Shutdown();
    rdClose();
    rdShutdown();
    stdShutdown();
    stdBabylon_SetContext(nullptr);
}

TEST_CASE("the painter draws a TrueType font and a PNG from Data/ui")
{
    std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(nullptr);
    if ( !pDevice )
    {
        if ( GpuRequired() )
        {
            FAIL("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }

        MESSAGE("no SDL GPU device, skipping the TTF and PNG images");
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

    {
        JonesUiPainter painter;
        const FontId font = painter.registerTtfFontFile(*pFileSystem,
            "fonts/Jost[wght].ttf", 32.0f);
        REQUIRE(font != 0);
        const ImageId prompt = painter.registerPngImageFile(*pFileSystem,
            "prompts/xbox/XboxSeriesX_A.png");
        REQUIRE(prompt != 0);

        const std::string text = "Indiana";
        const babylon::ui::Size textSize = painter.measureText(font, text);
        REQUIRE(textSize.width > 0.0f);
        REQUIRE(textSize.height > 0.0f);

        REQUIRE(std3D_StartScene() == 0);
        const babylon::ui::Color black{ 0.0f, 0.0f, 0.0f, 1.0f };
        const babylon::ui::Color white{ 1.0f, 1.0f, 1.0f, 1.0f };
        painter.fillRect(babylon::ui::Rect{ 0.0f, 0.0f, 640.0f, 480.0f }, black);

        const babylon::ui::Rect imageRect{ 400.0f, 20.0f, 100.0f, 100.0f };
        painter.drawImage(prompt, imageRect);

        // The same text and rectangle at all three alignments: if the measured width were not the
        // advance the painter draws, the center and right copies would start at another x.
        const babylon::ui::Rect leftRect{ 40.0f, 100.0f, textSize.width, textSize.height };
        const babylon::ui::Rect centerRect{ 40.0f, 200.0f, textSize.width, textSize.height };
        const babylon::ui::Rect rightRect{ 40.0f, 300.0f, textSize.width, textSize.height };
        painter.drawText(font, text, leftRect, babylon::ui::TextAlign::Left, white);
        painter.drawText(font, text, centerRect, babylon::ui::TextAlign::Center, white);
        painter.drawText(font, text, rightRect, babylon::ui::TextAlign::Right, white);
        std3D_EndScene();
        REQUIRE(stdDisplay_Update() == 0);

        std::uint32_t width  = stdRenderBabylon_GetBackBufferWidth();
        std::uint32_t height = stdRenderBabylon_GetBackBufferHeight();
        REQUIRE(width == 640u);
        REQUIRE(height == 480u);

        std::vector<std::uint8_t> pixels((size_t)width * height * 4u);
        REQUIRE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));

        const auto inkBounds = [&pixels, width](float y0, float y1,
            float& minX, float& maxX, size_t& count)
        {
            minX = 1.0e9f;
            maxX = -1.0f;
            count = 0;
            for ( std::uint32_t y = (std::uint32_t)y0; y < (std::uint32_t)y1; ++y )
            {
                for ( std::uint32_t x = 0; x < 300u; ++x )
                {
                    if ( IsBrightText(PixelAt(pixels, width, x, y)) )
                    {
                        minX = std::min(minX, (float)x);
                        maxX = std::max(maxX, (float)x);
                        ++count;
                    }
                }
            }
        };

        float leftMin = 0.0f, leftMax = 0.0f, centerMin = 0.0f, centerMax = 0.0f;
        float rightMin = 0.0f, rightMax = 0.0f;
        size_t leftCount = 0, centerCount = 0, rightCount = 0;
        inkBounds(100.0f, 100.0f + textSize.height, leftMin, leftMax, leftCount);
        inkBounds(200.0f, 200.0f + textSize.height, centerMin, centerMax, centerCount);
        inkBounds(300.0f, 300.0f + textSize.height, rightMin, rightMax, rightCount);

        REQUIRE(leftCount > 0);
        REQUIRE(centerCount > 0);
        REQUIRE(rightCount > 0);
        CHECK(std::abs(centerMin - leftMin) <= 1.0f);
        CHECK(std::abs(rightMin - leftMin) <= 1.0f);
        CHECK(std::abs(centerMax - leftMax) <= 1.0f);
        CHECK(std::abs(rightMax - leftMax) <= 1.0f);

        // The prompt's green A is drawn inside its rectangle and nowhere in the empty strip
        // beside it.
        const Rgb promptGreen{ 135, 214, 56 };
        CHECK(CountColor(pixels, width, height, 400.0f, 20.0f, 500.0f, 120.0f,
            promptGreen, 16) > 0u);
        CHECK(CountColor(pixels, width, height, 510.0f, 20.0f, 610.0f, 120.0f,
            promptGreen, 16) == 0u);
    }

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
