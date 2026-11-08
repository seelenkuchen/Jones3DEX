// Added: Unity Fixture suites of the Babylon column's stdDisplay backend (stdDisplayBabylon.cpp), run
// through the public stdDisplay_ functions and the capture forwarders in front of them. The Null group
// runs on a babylon::render::NullDevice, which applies clears and uploads to its textures, so the back
// buffer locks, fills and the present are checked through the recorded command stream and the color
// target's bytes. Expected values are worked out here from stdDisplayDX9.c's rules (mode list order,
// fill rects of left, top, width, height, the 1/0 answers) and from stdColor's channel scaling (bits
// repeated when widened, cut when narrowed). The GPU group draws a 3D frame with std3D on the SDL GPU
// device, writes 2D pixels into it through the back buffer lock and reads both layers back; without
// a GPU it is ignored, or fails with BABYLON_REQUIRE_GPU=1.
//
// The suites run in std3DBabylonTests (std3DBabylonTest.cpp has the main and installs the adapter
// context); each test puts its own render device, and maybe a window, into that context.
#include <unity_fixture.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>

#include <SDL3/SDL_stdinc.h>

#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdRenderCapture.h>

#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonTour.h>
#include <std/Babylon/stdRenderBabylon.h>

#include "stdGeneralTest.h"

namespace render = babylon::render;

namespace
{
    std::unique_ptr<render::Device> stdDisplayBabylonTest_pDevice;
    render::NullDevice* stdDisplayBabylonTest_pNullDevice = nullptr;
    babylon::platform::NullPlatform stdDisplayBabylonTest_platform;
    std::unique_ptr<babylon::platform::Window> stdDisplayBabylonTest_pWindow;

    const char* const kScreenshotPath = "stdDisplayBabylonTest_Screen.bmp";

    StdBabylonContext& Context()
    {
        StdBabylonContext* pContext = stdBabylon_GetContext();
        TEST_ASSERT_NOT_NULL(pContext);
        return *pContext;
    }

    void UseNullDevice()
    {
        std::unique_ptr<render::NullDevice> pDevice = render::createNullDevice();
        stdDisplayBabylonTest_pNullDevice = pDevice.get();
        stdDisplayBabylonTest_pDevice     = std::move(pDevice);
        Context().pRenderDevice           = stdDisplayBabylonTest_pDevice.get();
    }

    void DropDevice()
    {
        StdBabylonContext* pContext = stdBabylon_GetContext();
        if ( pContext )
        {
            pContext->pRenderDevice = nullptr;
            pContext->pWindow       = nullptr;
        }

        stdDisplayBabylonTest_pNullDevice = nullptr;
        stdDisplayBabylonTest_pDevice.reset();
        stdDisplayBabylonTest_pWindow.reset();
    }

    render::NullDevice& Null()
    {
        TEST_ASSERT_NOT_NULL(stdDisplayBabylonTest_pNullDevice);
        return *stdDisplayBabylonTest_pNullDevice;
    }

    bool SameColorInfo(const ColorInfo& a, const ColorInfo& b)
    {
        return std::memcmp(&a, &b, sizeof(ColorInfo)) == 0;
    }

    // The index of the width x height mode of bpp bits, or the mode count.
    size_t FindMode(uint32_t width, uint32_t height, uint32_t bpp)
    {
        for ( size_t i = 0; i < stdDisplay_GetNumVideoModes(); ++i )
        {
            StdVideoMode mode;
            TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(i, &mode));
            if ( mode.rasterInfo.width == width && mode.rasterInfo.height == height && mode.rasterInfo.colorInfo.bpp == bpp )
            {
                return i;
            }
        }

        return stdDisplay_GetNumVideoModes();
    }

    // The display started, opened and set to a windowed width x height mode (X8R8G8B8).
    void OpenWindowed(uint32_t width, uint32_t height)
    {
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
        stdDisplay_SetDefaultResolution(width, height);
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    }

    void CloseDisplay()
    {
        std3D_Shutdown();
        stdDisplay_Shutdown();
        std3D_ResetGlobals();
        stdDisplay_ResetGlobals();
    }

    template <class T>
    const T& RecordedAt(size_t index)
    {
        const std::vector<render::RecordedCommand>& recorded = Null().recorded();
        TEST_ASSERT_TRUE(index < recorded.size());
        TEST_ASSERT_TRUE(std::holds_alternative<T>(recorded[index]));
        return std::get<T>(recorded[index]);
    }

    // A recorded pass that clears the back buffer's color to color and has no depth attachment.
    void AssertClearPass(size_t index, const render::ClearColor& color)
    {
        const render::BeginPass& begin = RecordedAt<render::BeginPass>(index);
        TEST_ASSERT_EQUAL_size_t(1u, begin.desc.colorTargets.size());
        TEST_ASSERT_TRUE(begin.desc.colorTargets[0].target == stdRenderBabylon_GetColorTarget());
        TEST_ASSERT_TRUE(begin.desc.colorTargets[0].load == render::LoadOp::Clear);
        TEST_ASSERT_TRUE(begin.desc.colorTargets[0].clear == color);
        TEST_ASSERT_FALSE(begin.desc.depthTarget.has_value());
        RecordedAt<render::EndPass>(index + 1u);
    }

    void AssertSubmitted(size_t index)
    {
        const render::FrameSubmitted& submitted = RecordedAt<render::FrameSubmitted>(index);
        TEST_ASSERT_TRUE(submitted.frame > 0);
        TEST_ASSERT_FALSE(submitted.presented);
    }

    // The color target's bytes (Bgra8, rows top first) as the device holds them.
    std::vector<std::byte> ColorTargetBytes()
    {
        return stdDisplayBabylonTest_pDevice->readTexture(stdRenderBabylon_GetColorTarget(), 0);
    }

    void AssertBgra(const std::vector<std::byte>& bytes, size_t pixel, uint8_t b, uint8_t g, uint8_t r, uint8_t a)
    {
        TEST_ASSERT_TRUE(pixel * 4u + 4u <= bytes.size());
        const uint8_t expected[4] = { b, g, r, a };
        char aMessage[96];
        std::snprintf(aMessage, sizeof(aMessage), "pixel %zu is %02x %02x %02x %02x", pixel, (unsigned)bytes[pixel * 4u],
            (unsigned)bytes[pixel * 4u + 1u], (unsigned)bytes[pixel * 4u + 2u], (unsigned)bytes[pixel * 4u + 3u]);
        for ( size_t i = 0; i < 4u; ++i )
        {
            TEST_ASSERT_EQUAL_HEX8_MESSAGE(expected[i], (uint8_t)bytes[pixel * 4u + i], aMessage);
        }
    }

    tVBuffer* NewVBuffer(const ColorInfo& ci, uint32_t width, uint32_t height)
    {
        tRasterInfo raster;
        std::memset(&raster, 0, sizeof(raster));
        raster.width     = width;
        raster.height    = height;
        raster.colorInfo = ci;
        tVBuffer* pVBuffer = stdDisplay_VBufferNew(&raster, 0, 0);
        TEST_ASSERT_NOT_NULL(pVBuffer);
        return pVBuffer;
    }

    uint16_t Pixel16(const tVBuffer* pVBuffer, size_t i)
    {
        uint16_t value;
        std::memcpy(&value, pVBuffer->pPixels + i * 2u, sizeof(value));
        return value;
    }

    uint32_t Pixel32(const tVBuffer* pVBuffer, size_t i)
    {
        uint32_t value;
        std::memcpy(&value, pVBuffer->pPixels + i * 4u, sizeof(value));
        return value;
    }

    void SetPixel16(tVBuffer* pVBuffer, size_t i, uint16_t value)
    {
        std::memcpy(pVBuffer->pPixels + i * 2u, &value, sizeof(value));
    }

    void SetPixel32(tVBuffer* pVBuffer, size_t i, uint32_t value)
    {
        std::memcpy(pVBuffer->pPixels + i * 4u, &value, sizeof(value));
    }

    bool GpuRequired()
    {
        // SDL_getenv: MSVC rejects getenv under /sdl.
        const char* pValue = SDL_getenv("BABYLON_REQUIRE_GPU");
        return pValue && std::strcmp(pValue, "1") == 0;
    }
}

TEST_GROUP(stdDisplayBabylon);

TEST_SETUP(stdDisplayBabylon)
{
    StdGeneralTest_Startup();
    stdRenderCapture_ResetGlobals();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    UseNullDevice();
    std::remove(kScreenshotPath);
}

TEST_TEAR_DOWN(stdDisplayBabylon)
{
    CloseDisplay();
    stdRenderBabylon_Close();
    DropDevice();
    std::remove(kScreenshotPath);
    StdGeneralTest_Shutdown();
}

TEST(stdDisplayBabylon, OneDeviceWithTheRenderDeviceMemory)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_size_t(1u, stdDisplay_GetNumDevices());

    StdDisplayDevice device;
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetDevice(0u, &device));
    TEST_ASSERT_EQUAL_STRING("Jones3D Babylon Display", device.aDeviceName);
    TEST_ASSERT_EQUAL_STRING("stdDisplayBabylon", device.aDriverName);
    TEST_ASSERT_EQUAL_INT(1, device.bHAL);
    TEST_ASSERT_EQUAL_INT(0, device.bWindowRenderNotSupported);
    TEST_ASSERT_EQUAL_size_t((size_t)8192u * 8192u * 4u, device.totalVideoMemory); // the Null device's limit, DX9's caps rule
    TEST_ASSERT_EQUAL_size_t(device.totalVideoMemory / 2u, device.freeVideoMemory);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_GetDevice(1u, &device));

    // Before a mode the back buffer already has the default size, as with stdDisplayNull.c.
    uint32_t width  = 0;
    uint32_t height = 0;
    stdDisplay_GetBackBufferSize(&width, &height);
    TEST_ASSERT_EQUAL_UINT32(640u, width);
    TEST_ASSERT_EQUAL_UINT32(480u, height);

    size_t total = 0;
    size_t free  = 0;
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetTextureMemory(&total, &free));
    TEST_ASSERT_EQUAL_size_t(device.totalVideoMemory, total);
    TEST_ASSERT_EQUAL_size_t(device.freeVideoMemory, free);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_CanRenderWindowed()); // headless: no window to render into

    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Open(1u));
    TEST_ASSERT_FALSE(stdDisplay_IsOpen());
    TEST_ASSERT_FALSE(stdRenderBabylon_IsOpen());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_TRUE(stdRenderBabylon_IsOpen());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetDevice() == stdDisplayBabylonTest_pDevice.get());
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetTotalMemory(&total, &free));
    TEST_ASSERT_EQUAL_size_t(1024u * 1024u, total); // 1 MB per listed X8R8G8B8 mode
    TEST_ASSERT_EQUAL_size_t(total / 2u, free);
    stdDisplay_Close();
    TEST_ASSERT_FALSE(stdRenderBabylon_IsOpen());
}

TEST(stdDisplayBabylon, HeadlessModesAreTheDefaultSizeInBothFormats)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_size_t(0u, stdDisplay_GetNumVideoModes()); // listed at Open
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));

    // 640 x 480 in RGB565, then in X8R8G8B8 (stdDisplayDX9.c's order: by bpp, width, height).
    TEST_ASSERT_EQUAL_size_t(2u, stdDisplay_GetNumVideoModes());
    for ( size_t i = 0; i < 2u; ++i )
    {
        StdVideoMode mode;
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(i, &mode));
        const tRasterInfo& raster = mode.rasterInfo;
        TEST_ASSERT_EQUAL_UINT32(640u, raster.width);
        TEST_ASSERT_EQUAL_UINT32(480u, raster.height);
        TEST_ASSERT_TRUE(SameColorInfo(raster.colorInfo, i == 0u ? stdColor_cfRGB565 : stdColor_cfRGB8888));
        TEST_ASSERT_EQUAL_UINT32(60u, mode.refreshRate);
        TEST_ASSERT_EQUAL_size_t(640u, raster.rowWidth);
        TEST_ASSERT_EQUAL_size_t(640u * (raster.colorInfo.bpp / 8u), raster.rowSize);
        TEST_ASSERT_EQUAL_size_t(raster.rowSize * 480u, raster.size);
        // Headless keeps stdDisplayNull.c's width / height, which the recorded traces were made with.
        TEST_ASSERT_EQUAL_FLOAT(640.0f / 480.0f, mode.aspectRatio);
    }

    StdVideoMode mode;
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_GetVideoMode(2u, &mode));

    stdDisplay_Close();
    TEST_ASSERT_EQUAL_size_t(0u, stdDisplay_GetNumVideoModes());
}

TEST(stdDisplayBabylon, AWindowedRunAddsTheWindowSizeAndDX9Aspects)
{
    stdDisplayBabylonTest_pWindow = stdDisplayBabylonTest_platform.createWindow(babylon::platform::WindowDesc{ "test", { 700, 500 } });
    TEST_ASSERT_NOT_NULL(stdDisplayBabylonTest_pWindow.get());
    Context().pWindow = stdDisplayBabylonTest_pWindow.get();

    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_CanRenderWindowed());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));

    // 16 sizes and the window's in RGB565 and X8R8G8B8, by bpp, then width, then height.
    TEST_ASSERT_EQUAL_size_t(34u, stdDisplay_GetNumVideoModes());
    StdVideoMode prev;
    for ( size_t i = 0; i < 34u; ++i )
    {
        StdVideoMode listed;
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(i, &listed));
        const tRasterInfo& raster = listed.rasterInfo;
        TEST_ASSERT_TRUE(SameColorInfo(raster.colorInfo, i < 17u ? stdColor_cfRGB565 : stdColor_cfRGB8888));
        TEST_ASSERT_EQUAL_UINT32(60u, listed.refreshRate);
        TEST_ASSERT_EQUAL_size_t(raster.width * (raster.colorInfo.bpp / 8u), raster.rowSize);
        if ( i > 0u )
        {
            const bool bOrdered = prev.rasterInfo.colorInfo.bpp < raster.colorInfo.bpp
                || (prev.rasterInfo.colorInfo.bpp == raster.colorInfo.bpp
                    && (prev.rasterInfo.width < raster.width
                        || (prev.rasterInfo.width == raster.width && prev.rasterInfo.height < raster.height)));
            TEST_ASSERT_TRUE(bOrdered);
        }
        prev = listed;
    }

    StdVideoMode mode;
    const size_t window16 = FindMode(700u, 500u, 16u);
    const size_t window32 = FindMode(700u, 500u, 32u);
    TEST_ASSERT_TRUE(window16 < 17u);
    TEST_ASSERT_TRUE(window32 >= 17u && window32 < 34u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(window16 - 1u, &mode));
    TEST_ASSERT_EQUAL_UINT32(640u, mode.rasterInfo.width); // sorted in: 640x480, then 700x500, then 800x600
    TEST_ASSERT_EQUAL_UINT32(480u, mode.rasterInfo.height);

    // stdDisplayDX9.c's SetAspectRatio: 0.75 for the non-square modes, 1 otherwise.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(FindMode(640u, 400u, 32u), &mode));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, mode.aspectRatio);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(FindMode(320u, 200u, 16u), &mode));
    TEST_ASSERT_EQUAL_FLOAT(0.75f, mode.aspectRatio);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(window32, &mode));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, mode.aspectRatio);

    stdDisplay_SetDefaultResolution(640u, 480u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetCurrentVideoMode(&mode));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, mode.aspectRatio);
}

TEST(stdDisplayBabylon, SetModeMakesTheBackBufferClearsAndPresents)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    StdVideoMode mode;
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_GetCurrentVideoMode(&mode));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Update()); // no mode: nothing to present
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size());

    // Fullscreen takes the listed mode: here 640 x 480 in RGB565.
    const size_t modeNum = FindMode(640u, 480u, 16u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(modeNum, 1, 2u));
    StdVideoMode listed;
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetVideoMode(modeNum, &listed));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetCurrentVideoMode(&mode));
    TEST_ASSERT_EQUAL_MEMORY(&listed, &mode, sizeof(mode));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_IsFullscreen());

    uint32_t width  = 0;
    uint32_t height = 0;
    stdDisplay_GetBackBufferSize(&width, &height);
    TEST_ASSERT_EQUAL_UINT32(640u, width);
    TEST_ASSERT_EQUAL_UINT32(480u, height);
    TEST_ASSERT_EQUAL_INT(VBUFFER_HARDWARE, stdDisplay_g_backBuffer.type);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_g_backBuffer.bVideoMemory);
    TEST_ASSERT_NULL(stdDisplay_g_backBuffer.pPixels);
    TEST_ASSERT_EQUAL_size_t(1280u, stdDisplay_g_backBuffer.rasterInfo.rowSize);
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfRGB565, stdDisplay_g_backBuffer.rasterInfo.colorInfo));
    TEST_ASSERT_EQUAL_UINT32(640u, stdRenderBabylon_GetBackBufferWidth());
    TEST_ASSERT_EQUAL_UINT32(480u, stdRenderBabylon_GetBackBufferHeight());

    // stdDisplayDX9.c's SetMode: fill black, present, and in fullscreen fill once more.
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertClearPass(0u, render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f });
    AssertSubmitted(2u);
    TEST_ASSERT_TRUE(stdRenderBabylon_HasFrame());
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(6u, Null().recorded().size());
    AssertClearPass(3u, render::ClearColor{ 0.0f, 0.0f, 0.0f, 1.0f });
    AssertSubmitted(5u);
    TEST_ASSERT_FALSE(stdRenderBabylon_HasFrame());

    // A frame with nothing recorded is still submitted (and on a swapchain, presented).
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(7u, Null().recorded().size());
    AssertSubmitted(6u);

    // EncodeFromRGB565 encodes for the mode: unchanged in RGB565.
    TEST_ASSERT_EQUAL_HEX32(0xF81Fu, stdDisplay_EncodeFromRGB565(0xF81Fu));

    // A mode that isn't listed is refused before the current one is cleared.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_SetMode(stdDisplay_GetNumVideoModes(), 1, 1u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetCurrentVideoMode(&mode));

    // Windowed takes the default resolution in X8R8G8B8; the old back buffer goes.
    const render::TextureHandle oldTarget = stdRenderBabylon_GetColorTarget();
    stdDisplay_SetDefaultResolution(320u, 200u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(modeNum, 0, 1u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_IsFullscreen());
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_GetCurrentVideoMode(&mode));
    TEST_ASSERT_EQUAL_UINT32(320u, mode.rasterInfo.width);
    TEST_ASSERT_EQUAL_UINT32(200u, mode.rasterInfo.height);
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfRGB8888, mode.rasterInfo.colorInfo));
    TEST_ASSERT_EQUAL_size_t(1280u, mode.rasterInfo.rowSize);
    TEST_ASSERT_EQUAL_FLOAT(1.6f, mode.aspectRatio);
    TEST_ASSERT_FALSE(oldTarget == stdRenderBabylon_GetColorTarget());
    TEST_ASSERT_TRUE(stdDisplayBabylonTest_pDevice->readTexture(oldTarget, 0).empty());
    TEST_ASSERT_EQUAL_HEX32(0x00FF00FFu, stdDisplay_EncodeFromRGB565(0xF81Fu));
    TEST_ASSERT_EQUAL_HEX32(0x00848284u, stdDisplay_EncodeFromRGB565(0x8410u)); // 16, 32, 16 widened by repeating bits

    // ClearMode: no mode, no back buffer.
    stdDisplay_ClearMode();
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_GetCurrentVideoMode(&mode));
    stdDisplay_GetBackBufferSize(&width, &height);
    TEST_ASSERT_EQUAL_UINT32(0u, width);
    TEST_ASSERT_TRUE(stdRenderBabylon_GetColorTarget().isNull());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Update());
    TEST_ASSERT_EQUAL_HEX32(0xFFu, stdDisplay_EncodeFromRGB565(0xFFFFu)); // no mode: no shifts, the channels overlap
}

TEST(stdDisplayBabylon, LocksReadBackOnlyWhatTheGpuChanged)
{
    void* pSurface  = nullptr;
    uint32_t width  = 0;
    uint32_t height = 0;
    int32_t pitch   = 0;
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch)); // not open
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch)); // no mode
    stdDisplay_SetDefaultResolution(4u, 2u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));

    // A fill of the whole buffer is a clear pass in the frame.
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00112233u, nullptr));
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size());

    // The first lock reads the cleared buffer back: the frame so far is submitted first, unpresented.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_UINT32(4u, width);
    TEST_ASSERT_EQUAL_UINT32(2u, height);
    TEST_ASSERT_EQUAL_INT32(16, pitch);
    TEST_ASSERT_NOT_NULL(pSurface);
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertClearPass(0u, render::ClearColor{ 0x11 / 255.0f, 0x22 / 255.0f, 0x33 / 255.0f, 1.0f });
    AssertSubmitted(2u);
    const std::vector<std::byte> cleared(static_cast<std::byte*>(pSurface), static_cast<std::byte*>(pSurface) + 32u);
    for ( size_t i = 0; i < 8u; ++i )
    {
        AssertBgra(cleared, i, 0x33u, 0x22u, 0x11u, 0xFFu);
    }

    // The 2D pixels written through the lock reach the color target at the unlock, alpha opaque.
    uint8_t* pPixels = static_cast<uint8_t*>(pSurface);
    for ( size_t i = 0; i < 32u; ++i )
    {
        pPixels[i] = (uint8_t)(0x40u + i);
    }
    stdDisplay_UnlockBackBuffer();
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size()); // an upload is no command
    std::vector<std::byte> target = ColorTargetBytes();
    for ( size_t i = 0; i < 8u; ++i )
    {
        AssertBgra(target, i, (uint8_t)(0x40u + i * 4u), (uint8_t)(0x41u + i * 4u), (uint8_t)(0x42u + i * 4u), 0xFFu);
    }

    // Present, then lock again: only the CPU changed the buffer, so there is no readback. A change
    // made behind the display's back (not through a pass or the display) proves it.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(4u, Null().recorded().size());
    AssertSubmitted(3u);
    const std::vector<std::byte> zeros(32u, std::byte{ 0 });
    TEST_ASSERT_TRUE(stdDisplayBabylonTest_pDevice->uploadTexture(stdRenderBabylon_GetColorTarget(), 0, zeros));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBufferReadOnly(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_HEX8(0x40u, static_cast<uint8_t*>(pSurface)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5Fu, static_cast<uint8_t*>(pSurface)[31]);
    stdDisplay_UnlockBackBuffer();
    TEST_ASSERT_EQUAL_size_t(4u, Null().recorded().size());

    // A pass on the back buffer (here a clear) makes the next lock read back again.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00000000u, nullptr));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_HEX8(0x00u, static_cast<uint8_t*>(pSurface)[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFu, static_cast<uint8_t*>(pSurface)[3]);
    TEST_ASSERT_EQUAL_size_t(7u, Null().recorded().size());
    AssertSubmitted(6u);

    // A fill of a locked buffer fails, as a DX9 ColorFill on a locked surface did.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_BackBufferFill(0x00FFFFFFu, nullptr));
    stdDisplay_UnlockBackBuffer();
}

TEST(stdDisplayBabylon, LockNestingFollowsDX9)
{
    void* pSurface  = nullptr;
    void* pNested   = nullptr;
    uint32_t width  = 0;
    uint32_t height = 0;
    int32_t pitch   = 0;
    OpenWindowed(4u, 2u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00102030u, nullptr));

    // A read-only lock can't be upgraded; what is written through it never reaches the GPU.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBufferReadOnly(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_LockBackBuffer(&pNested, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBufferReadOnly(&pNested, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_PTR(pSurface, pNested);
    std::memset(pSurface, 0xEE, 32u);
    stdDisplay_UnlockBackBuffer();
    stdDisplay_UnlockBackBuffer();
    AssertBgra(ColorTargetBytes(), 0u, 0x30u, 0x20u, 0x10u, 0xFFu);

    // A write lock can be nested with a read-only one; the last unlock uploads.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBufferReadOnly(&pNested, &width, &height, &pitch));
    std::memset(pSurface, 0x77, 32u);
    stdDisplay_UnlockBackBuffer();
    AssertBgra(ColorTargetBytes(), 0u, 0x30u, 0x20u, 0x10u, 0xFFu); // still locked
    stdDisplay_UnlockBackBuffer();
    AssertBgra(ColorTargetBytes(), 0u, 0x77u, 0x77u, 0x77u, 0xFFu);
    stdDisplay_UnlockBackBuffer(); // one too many: a no-op
    AssertBgra(ColorTargetBytes(), 0u, 0x77u, 0x77u, 0x77u, 0xFFu);
}

TEST(stdDisplayBabylon, VBufferLocksOfTheBackBufferLockIt)
{
    OpenWindowed(4u, 2u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00AABBCCu, nullptr));

    // stdDisplayDX9.c's hardware VBuffer lock: the pixels and the pitch of the back buffer lock, 1.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLockReadOnly(&stdDisplay_g_backBuffer));
    TEST_ASSERT_NOT_NULL(stdDisplay_g_backBuffer.pPixels);
    TEST_ASSERT_EQUAL_size_t(1u, stdDisplay_g_backBuffer.lockRefCount);
    TEST_ASSERT_EQUAL_size_t(16u, stdDisplay_g_backBuffer.rasterInfo.rowSize);
    TEST_ASSERT_EQUAL_size_t(4u, stdDisplay_g_backBuffer.rasterInfo.rowWidth);
    TEST_ASSERT_EQUAL_size_t(32u, stdDisplay_g_backBuffer.rasterInfo.size);
    TEST_ASSERT_EQUAL_HEX32(0xFFAABBCCu, Pixel32(&stdDisplay_g_backBuffer, 7u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferLock(&stdDisplay_g_backBuffer)); // no upgrade
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLockReadOnly(&stdDisplay_g_backBuffer));
    TEST_ASSERT_EQUAL_size_t(2u, stdDisplay_g_backBuffer.lockRefCount);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(&stdDisplay_g_backBuffer)); // still locked
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferUnlock(&stdDisplay_g_backBuffer)); // unlocked
    TEST_ASSERT_NULL(stdDisplay_g_backBuffer.pPixels);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferUnlock(&stdDisplay_g_backBuffer));

    // A write lock's pixels are uploaded when the VBuffer is unlocked.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(&stdDisplay_g_backBuffer));
    SetPixel32(&stdDisplay_g_backBuffer, 1u, 0x00123456u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_VBufferUnlock(&stdDisplay_g_backBuffer));
    const std::vector<std::byte> target = ColorTargetBytes();
    AssertBgra(target, 0u, 0xCCu, 0xBBu, 0xAAu, 0xFFu);
    AssertBgra(target, 1u, 0x56u, 0x34u, 0x12u, 0xFFu);
}

TEST(stdDisplayBabylon, AnRgb565BackBufferConvertsBothWays)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(FindMode(640u, 480u, 16u), 1, 1u));

    // The fill color is a pixel of the mode: RGB565 red is a clear to (1, 0, 0).
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0xF800u, nullptr));
    void* pSurface  = nullptr;
    uint32_t width  = 0;
    uint32_t height = 0;
    int32_t pitch   = 0;
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_INT32(1280, pitch);
    uint16_t* pPixels = static_cast<uint16_t*>(pSurface);
    TEST_ASSERT_EQUAL_HEX16(0xF800u, pPixels[0]);
    TEST_ASSERT_EQUAL_HEX16(0xF800u, pPixels[640u * 480u - 1u]);

    // Widened by repeating bits on the way up: 0x8410 is 16, 32, 16 = 0x84, 0x82, 0x84.
    pPixels[0] = 0x07E0u;
    pPixels[1] = 0x8410u;
    pPixels[2] = 0x001Fu;
    stdDisplay_UnlockBackBuffer();
    const std::vector<std::byte> target = ColorTargetBytes();
    AssertBgra(target, 0u, 0x00u, 0xFFu, 0x00u, 0xFFu);
    AssertBgra(target, 1u, 0x84u, 0x82u, 0x84u, 0xFFu);
    AssertBgra(target, 2u, 0xFFu, 0x00u, 0x00u, 0xFFu);
    AssertBgra(target, 3u, 0x00u, 0x00u, 0xFFu, 0xFFu);

    // Cut on the way down: Bgra 0x40, 0x80, 0xFF is RGB565 31, 32, 8 = 0xFC08.
    std::vector<std::byte> bgra(640u * 480u * 4u, std::byte{ 0 });
    bgra[0] = std::byte{ 0x40 };
    bgra[1] = std::byte{ 0x80 };
    bgra[2] = std::byte{ 0xFF };
    TEST_ASSERT_TRUE(stdRenderBabylon_WriteBackBuffer(bgra.data()));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x0000u, nullptr)); // a pass: the next lock reads back
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBufferReadOnly(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, static_cast<uint16_t*>(pSurface)[0]);
    stdDisplay_UnlockBackBuffer();
    TEST_ASSERT_TRUE(stdRenderBabylon_WriteBackBuffer(bgra.data()));
    const StdRect lastRow = { 0, 479, 1, 1 };
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0xFFFFu, &lastRow)); // a part: read back, then uploaded
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBufferReadOnly(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_HEX16(0xFC08u, static_cast<uint16_t*>(pSurface)[0]);
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, static_cast<uint16_t*>(pSurface)[640u * 479u]);
    stdDisplay_UnlockBackBuffer();
}

TEST(stdDisplayBabylon, PartialFillsUseDX9Rects)
{
    OpenWindowed(4u, 2u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x000000FFu, nullptr));

    // StdRect is left, top, width, height in stdDisplayDX9.c's fills; a width or height of 0 fails.
    const StdRect rect = { 1, 0, 2, 1 };
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00FF0000u, &rect));
    const StdRect empty = { 1, 0, 0, 1 };
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_BackBufferFill(0x00FFFFFFu, &empty));
    const StdRect outside = { 3, 1, 5, 5 }; // clipped to the buffer
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferFill(&stdDisplay_g_backBuffer, 0x0000FF00u, &outside));

    const std::vector<std::byte> target = ColorTargetBytes();
    AssertBgra(target, 0u, 0xFFu, 0x00u, 0x00u, 0xFFu);
    AssertBgra(target, 1u, 0x00u, 0x00u, 0xFFu, 0xFFu);
    AssertBgra(target, 2u, 0x00u, 0x00u, 0xFFu, 0xFFu);
    AssertBgra(target, 3u, 0xFFu, 0x00u, 0x00u, 0xFFu);
    AssertBgra(target, 6u, 0xFFu, 0x00u, 0x00u, 0xFFu);
    AssertBgra(target, 7u, 0x00u, 0xFFu, 0x00u, 0xFFu);

    // A VBuffer fill of the whole back buffer answers 1, like stdDisplayDX9.c's.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferFill(&stdDisplay_g_backBuffer, 0u, nullptr));
}

TEST(stdDisplayBabylon, SoftwareVBuffersFillAndConvertByteExact)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());

    // Every VBuffer is a software one with its pixels for life, locked once already.
    tVBuffer* p565 = NewVBuffer(stdColor_cfRGB565, 3u, 2u);
    TEST_ASSERT_EQUAL_INT(VBUFFER_SOFTWARE, p565->type);
    TEST_ASSERT_EQUAL_size_t(1u, p565->lockRefCount);
    TEST_ASSERT_EQUAL_size_t(6u, p565->rasterInfo.rowSize);
    TEST_ASSERT_EQUAL_size_t(12u, p565->rasterInfo.size);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(p565));
    TEST_ASSERT_EQUAL_size_t(2u, p565->lockRefCount);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(p565));

    // Fills: 1 = done; the rect is left, top, width, height, clipped to the buffer.
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferFill(p565, 0x1234u, nullptr));
    const StdRect rect = { 1, 1, 9, 9 };
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferFill(p565, 0xABCDu, &rect));
    const uint16_t aFilled[6] = { 0x1234u, 0x1234u, 0x1234u, 0x1234u, 0xABCDu, 0xABCDu };
    for ( size_t i = 0; i < 6u; ++i )
    {
        TEST_ASSERT_EQUAL_HEX16(aFilled[i], Pixel16(p565, i));
    }

    // RGB565 to X8R8G8B8: a new buffer, the source freed; channels widened by repeating bits.
    SetPixel16(p565, 0u, 0xF81Fu);
    SetPixel16(p565, 1u, 0x07E0u);
    SetPixel16(p565, 2u, 0x8410u);
    SetPixel16(p565, 3u, 0x0000u);
    SetPixel16(p565, 4u, 0xFFFFu);
    SetPixel16(p565, 5u, 0x0821u); // 1, 1, 1: 0x08, 0x04, 0x08
    tVBuffer* p8888 = stdDisplay_VBufferConvertColorFormat(&stdColor_cfRGB8888, p565, 0, nullptr);
    TEST_ASSERT_NOT_NULL(p8888);
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfRGB8888, p8888->rasterInfo.colorInfo));
    TEST_ASSERT_EQUAL_size_t(12u, p8888->rasterInfo.rowSize);
    const uint32_t a8888[6] = { 0x00FF00FFu, 0x0000FF00u, 0x00848284u, 0x00000000u, 0x00FFFFFFu, 0x00080408u };
    for ( size_t i = 0; i < 6u; ++i )
    {
        TEST_ASSERT_EQUAL_HEX32(a8888[i], Pixel32(p8888, i));
    }

    // The same format is no conversion.
    TEST_ASSERT_EQUAL_PTR(p8888, stdDisplay_VBufferConvertColorFormat(&stdColor_cfRGB8888, p8888, 0, nullptr));

    // Back to RGB565: channels cut. 0x00FF8040 is 31, 32, 8.
    SetPixel32(p8888, 0u, 0x00FF8040u);
    SetPixel32(p8888, 1u, 0x00070307u);
    tVBuffer* pBack = stdDisplay_VBufferConvertColorFormat(&stdColor_cfRGB565, p8888, 0, nullptr);
    TEST_ASSERT_NOT_NULL(pBack);
    TEST_ASSERT_EQUAL_HEX16(0xFC08u, Pixel16(pBack, 0u));
    TEST_ASSERT_EQUAL_HEX16(0x0000u, Pixel16(pBack, 1u));
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, Pixel16(pBack, 4u));

    // ARGB4444 to ARGB8888, alpha included: 0xF84C is 15, 8, 4, 12.
    tVBuffer* p4444 = NewVBuffer(stdColor_cfARGB4444, 2u, 1u);
    SetPixel16(p4444, 0u, 0xF84Cu);
    SetPixel16(p4444, 1u, 0x1234u);
    tVBuffer* pArgb = stdDisplay_VBufferConvertColorFormat(&stdColor_cfARGB8888, p4444, 0, nullptr);
    TEST_ASSERT_NOT_NULL(pArgb);
    TEST_ASSERT_EQUAL_HEX32(0xFF8844CCu, Pixel32(pArgb, 0u));
    TEST_ASSERT_EQUAL_HEX32(0x11223344u, Pixel32(pArgb, 1u));

    // RGB565 to RGB555 keeps the buffer (same bit depth) and converts in place.
    tVBuffer* pSame = NewVBuffer(stdColor_cfRGB565, 2u, 1u);
    SetPixel16(pSame, 0u, 0xFFFFu);
    SetPixel16(pSame, 1u, 0x07E0u);
    TEST_ASSERT_EQUAL_PTR(pSame, stdDisplay_VBufferConvertColorFormat(&stdColor_cfRGB555, pSame, 0, nullptr));
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfRGB555, pSame->rasterInfo.colorInfo));
    TEST_ASSERT_EQUAL_HEX16(0x7FFFu, Pixel16(pSame, 0u));
    TEST_ASSERT_EQUAL_HEX16(0x03E0u, Pixel16(pSame, 1u));

    stdDisplay_VBufferFree(pBack);
    stdDisplay_VBufferFree(pArgb);
    stdDisplay_VBufferFree(pSame);
}

TEST(stdDisplayBabylon, SaveScreenWritesTheBackBufferAsBmp)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_SaveScreen(kScreenshotPath)); // no mode
    stdDisplay_SetDefaultResolution(3u, 2u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00102030u, nullptr));
    const StdRect topLeft = { 0, 0, 1, 1 };
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_BackBufferFill(0x00A0B0C0u, &topLeft));

    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SaveScreen(kScreenshotPath));
    std::ifstream file(kScreenshotPath, std::ios::binary);
    const std::vector<uint8_t> bmp((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // 54 header bytes, then 2 rows of 3 BGR pixels padded to 12 bytes, bottom row first.
    TEST_ASSERT_EQUAL_size_t(54u + 24u, bmp.size());
    const uint8_t aHeader[54] = {
        'B', 'M', 78, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0,
        40, 0, 0, 0, 3, 0, 0, 0, 2, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 24, 0, 0, 0,
        0x12, 0x0B, 0, 0, 0x12, 0x0B, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(aHeader, bmp.data(), 54u);
    const uint8_t aRows[24] = {
        0x30, 0x20, 0x10, 0x30, 0x20, 0x10, 0x30, 0x20, 0x10, 0, 0, 0, // bottom row
        0xC0, 0xB0, 0xA0, 0x30, 0x20, 0x10, 0x30, 0x20, 0x10, 0, 0, 0, // top row
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(aRows, bmp.data() + 54u, 24u);
}

TEST(stdDisplayBabylon, UpdateSubmitsTheScenesStd3DRecorded)
{
    OpenWindowed(16u, 16u);
    TEST_ASSERT_EQUAL_INT(1, std3D_Startup());
    TEST_ASSERT_EQUAL_INT(1, std3D_Open(0u));
    Null().clearRecorded();

    // The scenes of a frame stay in it until the present (stdDisplayDX9.c's Present).
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size());
    TEST_ASSERT_TRUE(stdRenderBabylon_HasFrame());
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(5u, Null().recorded().size());
    RecordedAt<render::BeginPass>(0u);
    RecordedAt<render::EndPass>(1u);
    RecordedAt<render::BeginPass>(2u);
    RecordedAt<render::EndPass>(3u);
    AssertSubmitted(4u);
    TEST_ASSERT_FALSE(stdRenderBabylon_HasFrame());

    // std3D can't open on a display that draws with another device.
    std3D_Close();
    stdRenderBabylon_Close();
    std::unique_ptr<render::NullDevice> pOther = render::createNullDevice();
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(pOther.get()));
    TEST_ASSERT_EQUAL_INT(0, std3D_Open(0u));
    stdRenderBabylon_Close();
}

TEST_GROUP_RUNNER(stdDisplayBabylon)
{
    RUN_TEST_CASE(stdDisplayBabylon, OneDeviceWithTheRenderDeviceMemory);
    RUN_TEST_CASE(stdDisplayBabylon, HeadlessModesAreTheDefaultSizeInBothFormats);
    RUN_TEST_CASE(stdDisplayBabylon, AWindowedRunAddsTheWindowSizeAndDX9Aspects);
    RUN_TEST_CASE(stdDisplayBabylon, SetModeMakesTheBackBufferClearsAndPresents);
    RUN_TEST_CASE(stdDisplayBabylon, LocksReadBackOnlyWhatTheGpuChanged);
    RUN_TEST_CASE(stdDisplayBabylon, LockNestingFollowsDX9);
    RUN_TEST_CASE(stdDisplayBabylon, VBufferLocksOfTheBackBufferLockIt);
    RUN_TEST_CASE(stdDisplayBabylon, AnRgb565BackBufferConvertsBothWays);
    RUN_TEST_CASE(stdDisplayBabylon, PartialFillsUseDX9Rects);
    RUN_TEST_CASE(stdDisplayBabylon, SoftwareVBuffersFillAndConvertByteExact);
    RUN_TEST_CASE(stdDisplayBabylon, SaveScreenWritesTheBackBufferAsBmp);
    RUN_TEST_CASE(stdDisplayBabylon, UpdateSubmitsTheScenesStd3DRecorded);
}

TEST_GROUP(stdDisplayBabylonGpu);

TEST_SETUP(stdDisplayBabylonGpu)
{
    StdGeneralTest_Startup();
    stdRenderCapture_ResetGlobals();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
}

TEST_TEAR_DOWN(stdDisplayBabylonGpu)
{
    CloseDisplay();
    stdRenderBabylon_Close();
    DropDevice();
    StdGeneralTest_Shutdown();
}

namespace
{
    StdVertexTL GpuVertex(float x, float y, uint32_t color)
    {
        StdVertexTL vertex;
        std::memset(&vertex, 0, sizeof(vertex));
        vertex.sx    = x;
        vertex.sy    = y;
        vertex.sz    = 0.5f;
        vertex.rhw   = 1.0f;
        vertex.color = color;
        return vertex;
    }

    void DrawRect(float x0, float y0, float x1, float y1, uint32_t color)
    {
        StdVertexTL aVerts[4] = { GpuVertex(x0, y0, color), GpuVertex(x1, y0, color), GpuVertex(x1, y1, color), GpuVertex(x0, y1, color) };
        uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 4u, aIndices, 6u);
    }

    // The pixel (x, y) of a width-wide Bgra8 readback is r, g, b, a within 1.
    void AssertGpuPixel(const std::vector<uint8_t>& pixels, uint32_t width, uint32_t x, uint32_t y, int r, int g, int b, int a)
    {
        const size_t at = ((size_t)y * width + x) * 4u;
        TEST_ASSERT_TRUE(at + 4u <= pixels.size());
        char aMessage[128];
        std::snprintf(aMessage, sizeof(aMessage), "pixel (%u, %u) is (%d, %d, %d, %d)", x, y, pixels[at + 2], pixels[at + 1],
            pixels[at], pixels[at + 3]);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, r, pixels[at + 2], aMessage);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, g, pixels[at + 1], aMessage);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, b, pixels[at], aMessage);
        TEST_ASSERT_INT_WITHIN_MESSAGE(1, a, pixels[at + 3], aMessage);
    }
}

TEST(stdDisplayBabylonGpu, CpuPixelsWrittenThroughTheLockLieBetweenTheFramesDraws)
{
    stdDisplayBabylonTest_pDevice = render::createSdlGpuDevice(nullptr);
    if ( !stdDisplayBabylonTest_pDevice )
    {
        if ( GpuRequired() )
        {
            TEST_FAIL_MESSAGE("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    Context().pRenderDevice = stdDisplayBabylonTest_pDevice.get();

    OpenWindowed(64u, 64u);
    TEST_ASSERT_EQUAL_INT(1, std3D_Startup());
    TEST_ASSERT_EQUAL_INT(1, std3D_Open(0u));

    // The 3D layer: a red rect over the left half, over std3D's transparent black clear.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    DrawRect(0.0f, 0.0f, 32.0f, 64.0f, 0xFFFF0000u);
    std3D_EndScene();

    // The lock sees the 3D frame; the 2D pixels go over it: a blue row over the red, a green
    // block on the black.
    void* pSurface  = nullptr;
    uint32_t width  = 0;
    uint32_t height = 0;
    int32_t pitch   = 0;
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_INT32(256, pitch);
    uint32_t* pPixels = static_cast<uint32_t*>(pSurface);
    TEST_ASSERT_EQUAL_HEX32(0xFFFF0000u, pPixels[8u * 64u + 8u]);
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, pPixels[8u * 64u + 48u]);
    for ( uint32_t x = 4u; x < 12u; ++x )
    {
        pPixels[4u * 64u + x] = 0x000000FFu;
    }
    for ( uint32_t y = 40u; y < 48u; ++y )
    {
        for ( uint32_t x = 40u; x < 48u; ++x )
        {
            pPixels[y * 64u + x] = 0x0000FF00u;
        }
    }
    stdDisplay_UnlockBackBuffer();

    // Drawn after the unlock, a white rect comes over the green block's right half.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    DrawRect(44.0f, 32.0f, 64.0f, 64.0f, 0xFFFFFFFFu);
    std3D_EndScene();
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());

    std::vector<uint8_t> pixels(64u * 64u * 4u);
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(pixels.data(), &width, &height));
    AssertGpuPixel(pixels, 64u, 20u, 20u, 255, 0, 0, 255);   // 3D, read back and uploaded again
    AssertGpuPixel(pixels, 64u, 8u, 4u, 0, 0, 255, 255);     // 2D over 3D
    AssertGpuPixel(pixels, 64u, 41u, 41u, 0, 255, 0, 255);   // 2D
    AssertGpuPixel(pixels, 64u, 46u, 46u, 255, 255, 255, 255); // 3D over 2D
    AssertGpuPixel(pixels, 64u, 50u, 8u, 0, 0, 0, 255);      // the clear, opaque after the upload
}

TEST(stdDisplayBabylonGpu, ScriptedTourWritesTheRequestedPresentAsBmp)
{
    stdDisplayBabylonTest_pDevice = render::createSdlGpuDevice(nullptr);
    if ( !stdDisplayBabylonTest_pDevice )
    {
        if ( GpuRequired() )
        {
            TEST_FAIL_MESSAGE("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    Context().pRenderDevice = stdDisplayBabylonTest_pDevice.get();

    OpenWindowed(4u, 2u);

    // Configure after SetMode: the mode's own clear presented a frame before the tour existed.
    const std::string screenshotDir = "stdBabylonTourTest_shots";
    std::error_code removeError;
    std::filesystem::remove_all(screenshotDir, removeError);

    StdBabylonTourOptions options;
    options.screenshotDir    = screenshotDir;
    options.screenshotFrames = { 1u };
    std::string error;
    TEST_ASSERT_TRUE_MESSAGE(stdBabylonTour_Configure(options, 0, error), error.c_str());
    TEST_ASSERT_EQUAL_UINT64(0u, stdBabylonTour_GetPresentCount());

    // Two 4-pixel rows through the back-buffer lock, so the BMP's row and channel order is exact.
    void* pSurface  = nullptr;
    uint32_t width  = 0;
    uint32_t height = 0;
    int32_t pitch   = 0;
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_LockBackBuffer(&pSurface, &width, &height, &pitch));
    TEST_ASSERT_EQUAL_UINT32(4u, width);
    TEST_ASSERT_EQUAL_UINT32(2u, height);
    uint32_t* pPixels = static_cast<uint32_t*>(pSurface);
    for ( uint32_t x = 0; x < width; ++x )
    {
        pPixels[x]              = 0x00A0B0C0u; // top row
        pPixels[width + x]      = 0x00102030u; // bottom row
    }
    stdDisplay_UnlockBackBuffer();

    // The present at count 1 writes <dir>/frame-1.bmp after it.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_UINT64(1u, stdBabylonTour_GetPresentCount());

    const std::filesystem::path bmpPath =
        std::filesystem::path(screenshotDir) / "frame-1.bmp";
    std::ifstream file(bmpPath, std::ios::binary);
    const std::vector<uint8_t> bmp((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    // 54 header bytes and two 4-pixel rows padded to 12 bytes: 24-bit, 4 x 2.
    TEST_ASSERT_EQUAL_size_t(54u + 24u, bmp.size());
    TEST_ASSERT_EQUAL_HEX8('B', bmp[0]);
    TEST_ASSERT_EQUAL_HEX8('M', bmp[1]);
    TEST_ASSERT_EQUAL_HEX8(4u, bmp[18]);
    TEST_ASSERT_EQUAL_HEX8(2u, bmp[22]);
    TEST_ASSERT_EQUAL_HEX8(24u, bmp[28]);

    // Bottom row first, B G R; then the top row.
    const uint8_t aExpectedRows[24] = {
        0x30, 0x20, 0x10, 0x30, 0x20, 0x10, 0x30, 0x20, 0x10, 0x30, 0x20, 0x10,
        0xC0, 0xB0, 0xA0, 0xC0, 0xB0, 0xA0, 0xC0, 0xB0, 0xA0, 0xC0, 0xB0, 0xA0,
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(aExpectedRows, bmp.data() + 54u, 24u);

    // A present without a request writes no file.
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_FALSE(std::filesystem::exists(
        std::filesystem::path(screenshotDir) / "frame-2.bmp"));

    stdBabylonTour_Shutdown();
    std::filesystem::remove_all(screenshotDir, removeError);
}

TEST_GROUP_RUNNER(stdDisplayBabylonGpu)
{
    RUN_TEST_CASE(stdDisplayBabylonGpu, CpuPixelsWrittenThroughTheLockLieBetweenTheFramesDraws);
    RUN_TEST_CASE(stdDisplayBabylonGpu, ScriptedTourWritesTheRequestedPresentAsBmp);
}
