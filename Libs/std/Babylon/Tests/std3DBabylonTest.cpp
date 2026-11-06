// Added: Unity Fixture suites of the Babylon column's std3D backend (std3DBabylon.cpp) and its frame
// owner (stdRenderBabylon.cpp), run through the public std3D_ functions and the capture forwarders in
// front of them, on a babylon::render::NullDevice whose recorded command stream and texture contents
// are compared with values worked out here from the DX9 backend's rules. The GPU group repeats the
// texture readback, checks the windowed present and renders synthetic scenes on the SDL GPU device,
// whose back buffer pixels are compared with values worked out from the DX9 shaders and render
// states; without a GPU it is ignored, or fails with BABYLON_REQUIRE_GPU=1. The render capture replay suite (stdRenderReplayTest.c)
// runs here once more, replaying into this backend instead of std3DNull.c.
//
// System textures in the tests are static: a failed assert leaves a test by longjmp, and the
// tear-down's std3D_Shutdown then still walks the cache they may be linked into.
//
// The executable has its own main: it installs the adapter context with the SDL file system and
// clock that the std file and time adapters need, and each test puts its own render device in it.
#include <unity_fixture.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <babylon/cvars/Registry.h>
#include <babylon/files/FileSystem.h>
#include <babylon/files/SdlFileSystem.h>
#include <babylon/log/CaptureSink.h>
#include <babylon/platform/NullPlatform.h>
#include <babylon/platform/Platform.h>
#include <babylon/platform/SdlPlatform.h>
#include <babylon/platform/Window.h>
#include <babylon/render/CommandList.h>
#include <babylon/render/Device.h>
#include <babylon/render/NullDevice.h>
#include <babylon/render/SdlGpuDevice.h>
#include <babylon/render/Texture.h>
#include <babylon/shaders/stdBabylonShaders.h>
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>

#include <SDL3/SDL_stdinc.h>

#include <std/General/std3D.h>
#include <std/General/stdColor.h>
#include <std/General/stdDisplay.h>
#include <std/General/stdDisplayCapture.h>
#include <std/General/stdRenderCapture.h>
#include <std/General/stdRenderReplay.h>

#include <std/Babylon/std3DBabylon.h>
#include <std/Babylon/stdBabylon.h>
#include <std/Babylon/stdBabylonCvars.h>
#include <std/Babylon/stdRenderBabylon.h>
#include <std/Babylon/stdRenderReplayBabylon.h>

#include "stdGeneralTest.h"

namespace render = babylon::render;

namespace
{
    StdBabylonContext std3DBabylonTest_context{};
    std::unique_ptr<render::Device> std3DBabylonTest_pDevice;
    render::NullDevice* std3DBabylonTest_pNullDevice = nullptr;

    const char* const kCapturePath = "std3DBabylonTest_Input.rcap";
    const char* const kReplayPath  = "std3DBabylonTest_Output.rcap";

    void UseNullDevice(const render::NullDeviceDesc& desc = {})
    {
        std::unique_ptr<render::NullDevice> pDevice = render::createNullDevice(desc);
        std3DBabylonTest_pNullDevice = pDevice.get();
        std3DBabylonTest_pDevice     = std::move(pDevice);
        std3DBabylonTest_context.pRenderDevice = std3DBabylonTest_pDevice.get();
    }

    void UseDevice(std::unique_ptr<render::Device> pDevice)
    {
        std3DBabylonTest_pNullDevice = nullptr;
        std3DBabylonTest_pDevice     = std::move(pDevice);
        std3DBabylonTest_context.pRenderDevice = std3DBabylonTest_pDevice.get();
    }

    void DropDevice()
    {
        std3DBabylonTest_context.pRenderDevice = nullptr;
        std3DBabylonTest_pNullDevice = nullptr;
        std3DBabylonTest_pDevice.reset();
    }

    render::NullDevice& Null()
    {
        TEST_ASSERT_NOT_NULL(std3DBabylonTest_pNullDevice);
        return *std3DBabylonTest_pNullDevice;
    }

    // The display at width x height (0 keeps the display's default 640 x 480), then std3D. What the
    // mode set recorded on a Null device (its clear and present) is dropped, so a test's stream
    // starts with its own calls.
    void OpenStd3D(uint32_t width = 0, uint32_t height = 0)
    {
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
        if ( width && height )
        {
            stdDisplay_SetDefaultResolution(width, height);
        }
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
        TEST_ASSERT_EQUAL_INT(1, std3D_Startup());
        TEST_ASSERT_EQUAL_INT(1, std3D_Open(0u));
        if ( std3DBabylonTest_pNullDevice )
        {
            std3DBabylonTest_pNullDevice->clearRecorded();
        }
    }

    // The end of a game frame: the scene, then the present, which submits the frame (M6.7).
    void EndFrame()
    {
        std3D_EndScene();
        TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    }

    void CloseStd3D()
    {
        std3D_Shutdown();
        stdDisplay_Shutdown();
        std3D_ResetGlobals();
        stdDisplay_ResetGlobals();
    }

    bool SameColorInfo(const ColorInfo& a, const ColorInfo& b)
    {
        return std::memcmp(&a, &b, sizeof(ColorInfo)) == 0;
    }

    // A VBuffer of width x height pixels of ci, filled with seed + i at byte i of each row's pixels.
    tVBuffer* NewVBuffer(const ColorInfo& ci, uint32_t width, uint32_t height, uint8_t seed)
    {
        tRasterInfo raster;
        std::memset(&raster, 0, sizeof(raster));
        raster.width     = width;
        raster.height    = height;
        raster.colorInfo = ci;

        tVBuffer* pVBuffer = stdDisplay_VBufferNew(&raster, 0, 0);
        TEST_ASSERT_NOT_NULL(pVBuffer);
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pVBuffer));
        for ( size_t i = 0; i < pVBuffer->rasterInfo.size; ++i )
        {
            pVBuffer->pPixels[i] = (uint8_t)(seed + 7u * i);
        }
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pVBuffer));
        return pVBuffer;
    }

    // The pixel bytes of a VBuffer in Babylon's upload layout: rows packed, top row first.
    std::vector<std::byte> PackedPixels(const tVBuffer* pVBuffer)
    {
        const size_t rowBytes = pVBuffer->rasterInfo.width * (pVBuffer->rasterInfo.colorInfo.bpp / 8u);
        std::vector<std::byte> bytes;
        for ( uint32_t y = 0; y < pVBuffer->rasterInfo.height; ++y )
        {
            const std::byte* pRow = reinterpret_cast<const std::byte*>(pVBuffer->pPixels + y * pVBuffer->rasterInfo.rowSize);
            bytes.insert(bytes.end(), pRow, pRow + rowBytes);
        }
        return bytes;
    }

    void AssertBytes(const std::vector<std::byte>& expected, const std::vector<std::byte>& actual)
    {
        TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
        if ( !expected.empty() )
        {
            TEST_ASSERT_EQUAL_MEMORY(expected.data(), actual.data(), expected.size());
        }
    }

    // readTexture with its warnings caught, so a failing assert never leaves a sink attached (Unity
    // leaves a test by longjmp, past destructors). *pNumWarnings gets the number of warnings.
    std::vector<std::byte> ReadTexture(render::TextureHandle texture, uint32_t mip, size_t* pNumWarnings = nullptr)
    {
        babylon::log::CaptureSink sink;
        std::vector<std::byte> bytes = std3DBabylonTest_pDevice->readTexture(texture, mip);
        if ( pNumWarnings )
        {
            *pNumWarnings = sink.records().size();
        }
        return bytes;
    }

    render::TextureHandle CachedHandle(const tSystemTexture& texture)
    {
        TEST_ASSERT_NOT_NULL(texture.pCachedTexture);
        return texture.pCachedTexture->texture;
    }

    // A 4x2 texture with one 2x1 mip of ci, allocated and cached; the VBuffers are freed again.
    void MakeCachedTexture(tSystemTexture* pTexture, const ColorInfo& ci, uint8_t seed,
        std::vector<std::byte>* pMip0 = nullptr, std::vector<std::byte>* pMip1 = nullptr)
    {
        tVBuffer* apMips[2] = { NewVBuffer(ci, 4u, 2u, seed), NewVBuffer(ci, 2u, 1u, (uint8_t)(seed + 100u)) };
        if ( pMip0 )
        {
            *pMip0 = PackedPixels(apMips[0]);
        }
        if ( pMip1 )
        {
            *pMip1 = PackedPixels(apMips[1]);
        }

        std::memset(pTexture, 0, sizeof(*pTexture));
        std3D_AllocSystemTexture(pTexture, apMips, 2u, std3D_GetColorFormat(&ci));
        TEST_ASSERT_EQUAL_size_t(2u, pTexture->numMipLevels);
        std3D_AddToTextureCache(pTexture, std3D_GetColorFormat(&ci));
        TEST_ASSERT_NOT_NULL(pTexture->pCachedTexture);

        stdDisplay_VBufferFree(apMips[0]);
        stdDisplay_VBufferFree(apMips[1]);
    }

    // X8R8G8B8 reads alpha as 1: every fourth byte of a 32-bit RGB texture is 255.
    std::vector<std::byte> Opaque(std::vector<std::byte> bytes)
    {
        for ( size_t i = 3; i < bytes.size(); i += 4 )
        {
            bytes[i] = std::byte{ 0xFF };
        }
        return bytes;
    }

    template <class T>
    const T& RecordedAt(size_t index)
    {
        const std::vector<render::RecordedCommand>& recorded = Null().recorded();
        TEST_ASSERT_TRUE(index < recorded.size());
        TEST_ASSERT_TRUE(std::holds_alternative<T>(recorded[index]));
        return std::get<T>(recorded[index]);
    }

    render::PassDesc ScenePass(render::LoadOp colorLoad, render::LoadOp depthLoad)
    {
        render::PassDesc pass;
        pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_GetColorTarget(), 0, colorLoad,
            render::StoreOp::Store, render::ClearColor{ 0.0f, 0.0f, 0.0f, 0.0f } });
        pass.depthTarget = render::DepthAttachment{ stdRenderBabylon_GetDepthTarget(), depthLoad,
            render::StoreOp::Store, 1.0f, 0 };
        return pass;
    }

    void AssertScenePass(size_t index, render::LoadOp colorLoad, render::LoadOp depthLoad)
    {
        const render::BeginPass& begin = RecordedAt<render::BeginPass>(index);
        TEST_ASSERT_TRUE(begin.desc == ScenePass(colorLoad, depthLoad));
    }

    // The load operations of a scene pass whose targets are gone (std3D was shut down since).
    void AssertScenePassLoads(size_t index, render::LoadOp colorLoad, render::LoadOp depthLoad)
    {
        const render::BeginPass& begin = RecordedAt<render::BeginPass>(index);
        TEST_ASSERT_EQUAL_size_t(1u, begin.desc.colorTargets.size());
        TEST_ASSERT_FALSE(begin.desc.colorTargets[0].target.isNull());
        TEST_ASSERT_TRUE(begin.desc.colorTargets[0].load == colorLoad);
        TEST_ASSERT_TRUE(begin.desc.depthTarget.has_value());
        TEST_ASSERT_FALSE(begin.desc.depthTarget->target.isNull());
        TEST_ASSERT_TRUE(begin.desc.depthTarget->load == depthLoad);
    }

    void AssertSubmitted(size_t index)
    {
        const render::FrameSubmitted& submitted = RecordedAt<render::FrameSubmitted>(index);
        TEST_ASSERT_TRUE(submitted.frame > 0);
        TEST_ASSERT_FALSE(submitted.presented);
    }

    std::vector<char> ReadFile(const char* pPath)
    {
        std::ifstream file(pPath, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    uint8_t Expand5(uint32_t value)
    {
        return (uint8_t)((value << 3) | (value >> 2));
    }

    bool GpuRequired()
    {
        // SDL_getenv: MSVC rejects getenv under /sdl.
        const char* pValue = SDL_getenv("BABYLON_REQUIRE_GPU");
        return pValue && std::strcmp(pValue, "1") == 0;
    }

    // Lavapipe keeps memory under pthread_once that LeakSanitizer reports each time the Vulkan loader
    // unloads it; keeping one more reference loaded avoids that (see Babylon's KeepGpuDriverLoaded.h).
    std::unique_ptr<render::Device> CreateGpuDevice(babylon::platform::Window* pWindow)
    {
        std::unique_ptr<render::Device> pDevice = render::createSdlGpuDevice(pWindow);
        if ( !pDevice && GpuRequired() )
        {
            TEST_FAIL_MESSAGE("no SDL GPU device although BABYLON_REQUIRE_GPU=1");
        }
        return pDevice;
    }
}

TEST_GROUP(std3DBabylon);

TEST_SETUP(std3DBabylon)
{
    StdGeneralTest_Startup();
    stdRenderCapture_ResetGlobals();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    UseNullDevice();
    remove(kCapturePath);
    remove(kReplayPath);
}

TEST_TEAR_DOWN(std3DBabylon)
{
    stdDisplayCapture_Close();
    stdRenderCapture_ResetGlobals();
    CloseStd3D();
    DropDevice();
    remove(kCapturePath);
    remove(kReplayPath);
    StdGeneralTest_Shutdown();
}

TEST(std3DBabylon, StartupFailsWithoutARenderDevice)
{
    // The display needs the device too (stdDisplayDX9.c's Startup failed without Direct3D 9).
    DropDevice();
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Startup());
    TEST_ASSERT_FALSE(stdDisplay_IsOpen());

    TEST_ASSERT_EQUAL_INT(0, std3D_Startup());
    TEST_ASSERT_EQUAL_size_t(0u, std3D_GetNumDevices());
    TEST_ASSERT_NULL(std3D_GetCurrentDevice());
}

TEST(std3DBabylon, OneDeviceDescribesTheRenderDevice)
{
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));
    TEST_ASSERT_EQUAL_INT(1, std3D_Startup());

    TEST_ASSERT_EQUAL_size_t(1u, std3D_GetNumDevices());
    const Device3D* pDevice = std3D_GetAllDevices();
    TEST_ASSERT_EQUAL_INT(1, pDevice->bHAL);
    TEST_ASSERT_EQUAL_INT(1, pDevice->hasZBuffer);
    TEST_ASSERT_EQUAL_INT(1, pDevice->bAlphaTextureSupported);
    TEST_ASSERT_EQUAL_INT(1, pDevice->bAlphaBlendSupported);
    TEST_ASSERT_EQUAL_INT(0, pDevice->bSqareOnlyTexture);
    TEST_ASSERT_EQUAL_size_t(1u, pDevice->minTexWidth);
    TEST_ASSERT_EQUAL_size_t(8192u, pDevice->maxTexWidth);   // the Null device's maxTextureDimension
    TEST_ASSERT_EQUAL_size_t(8192u, pDevice->maxTexHeight);
    TEST_ASSERT_EQUAL_size_t(65535u, pDevice->maxVertexCount);
    TEST_ASSERT_FALSE(pDevice->bAnisotropicFilteringSupported);
    TEST_ASSERT_FALSE(pDevice->bMipmapAutoGenSupported);
    TEST_ASSERT_FALSE(pDevice->bMSAASupported);
    TEST_ASSERT_EQUAL_STRING("Jones3D Babylon Display (Babylon render, null)", pDevice->deviceDescription);
    TEST_ASSERT_EQUAL_STRING("stdDisplayBabylon", pDevice->deviceName);

    // std3DX9.c's current device exists only while open.
    TEST_ASSERT_NULL(std3D_GetCurrentDevice());
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    TEST_ASSERT_EQUAL_INT(1, std3D_Open(0u));
    TEST_ASSERT_EQUAL_PTR(pDevice, std3D_GetCurrentDevice());
    TEST_ASSERT_EQUAL_size_t(65535u, std3D_g_maxVertices);
    std3D_Close();
    TEST_ASSERT_NULL(std3D_GetCurrentDevice());
}

TEST(std3DBabylon, TextureFormatsAreTheDX9Formats)
{
    OpenStd3D();

    // std3DX9.c's InitTextureFormats order without the 24-bit R8G8B8.
    TEST_ASSERT_EQUAL_size_t(5u, std3D_GetNumTextureFormats());
    TEST_ASSERT_EQUAL_size_t(0u, std3D_FindClosestFormat(&stdColor_cfRGB8888));
    TEST_ASSERT_EQUAL_size_t(1u, std3D_FindClosestFormat(&stdColor_cfARGB8888));
    TEST_ASSERT_EQUAL_size_t(2u, std3D_FindClosestFormat(&stdColor_cfRGB565));
    TEST_ASSERT_EQUAL_size_t(3u, std3D_FindClosestFormat(&stdColor_cfARGB5551));
    TEST_ASSERT_EQUAL_size_t(4u, std3D_FindClosestFormat(&stdColor_cfARGB4444));

    // GetTextureFormat answers what std3DX9.c's FindClosestFormat picked: X8R8G8B8 for RGB, A8R8G8B8
    // for both alpha types, no color key.
    ColorInfo ci;
    int bColorKey = -1;
    LPDDCOLORKEY pColorKey = reinterpret_cast<LPDDCOLORKEY>(&ci);
    std3D_GetTextureFormat(STDCOLOR_FORMAT_RGB, &ci, &bColorKey, &pColorKey);
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfRGB8888, ci));
    TEST_ASSERT_EQUAL_INT(0, bColorKey);
    TEST_ASSERT_NULL(pColorKey);

    std3D_GetTextureFormat(STDCOLOR_FORMAT_RGBA, &ci, &bColorKey, &pColorKey);
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfARGB8888, ci));
    TEST_ASSERT_EQUAL_INT(0, bColorKey);

    std3D_GetTextureFormat(STDCOLOR_FORMAT_RGBA_1BITALPHA, &ci, &bColorKey, &pColorKey);
    TEST_ASSERT_TRUE(SameColorInfo(stdColor_cfARGB8888, ci));
    TEST_ASSERT_EQUAL_INT(0, bColorKey);
    TEST_ASSERT_NULL(pColorKey);
}

TEST(std3DBabylon, FormatsTheDeviceCannotSampleAreLeftOut)
{
    UseNullDevice(render::NullDeviceDesc{ { render::TextureFormat::B4G4R4A4, render::TextureFormat::B5G6R5 } });
    OpenStd3D();

    TEST_ASSERT_EQUAL_size_t(3u, std3D_GetNumTextureFormats());
    TEST_ASSERT_EQUAL_size_t(2u, std3D_FindClosestFormat(&stdColor_cfARGB5551));
}

TEST(std3DBabylon, TexturesUploadEveryMipByteExactPerFormat)
{
    OpenStd3D();

    const ColorInfo* aFormats[] = { &stdColor_cfRGB8888, &stdColor_cfARGB8888, &stdColor_cfRGB565,
        &stdColor_cfARGB5551, &stdColor_cfARGB4444 };
    uint8_t seed = 1;
    for ( const ColorInfo* pCi : aFormats )
    {
        static tSystemTexture texture;
        std::vector<std::byte> mip0;
        std::vector<std::byte> mip1;
        MakeCachedTexture(&texture, *pCi, seed, &mip0, &mip1);
        seed = (uint8_t)(seed + 31u);

        const bool bOpaque = pCi == &stdColor_cfRGB8888;
        const render::TextureHandle handle = CachedHandle(texture);
        AssertBytes(bOpaque ? Opaque(mip0) : mip0, Null().readTexture(handle, 0));
        AssertBytes(bOpaque ? Opaque(mip1) : mip1, Null().readTexture(handle, 1));
        TEST_ASSERT_EQUAL_size_t(4u * 2u * (pCi->bpp / 8u), texture.textureSize);

        std3D_ClearSystemTexture(&texture);
    }
}

TEST(std3DBabylon, OtherLayoutsAreConvertedToTheFormatOfTheirType)
{
    OpenStd3D();

    // 24-bit RGB has no texture format: it becomes X8R8G8B8, bytes B, G, R, 255.
    tVBuffer* pRgb = NewVBuffer(stdColor_cfRGB888, 2u, 1u, 0x10);
    const uint8_t* p = pRgb->pPixels;
    const std::vector<std::byte> expectedRgb = {
        std::byte{ p[0] }, std::byte{ p[1] }, std::byte{ p[2] }, std::byte{ 0xFF },
        std::byte{ p[3] }, std::byte{ p[4] }, std::byte{ p[5] }, std::byte{ 0xFF } };

    static tSystemTexture texture;
    std3D_AllocSystemTexture(&texture, &pRgb, 1u, STDCOLOR_FORMAT_RGB);
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGB);
    AssertBytes(expectedRgb, Null().readTexture(CachedHandle(texture), 0));
    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(pRgb);

    // X1R5G5B5 neither: each 5-bit channel widens by repeating its top bits.
    tVBuffer* pRgb555 = NewVBuffer(stdColor_cfRGB555, 1u, 1u, 0);
    const uint16_t pixel = (uint16_t)((0x1Fu << 10) | (0x0Au << 5) | 0x03u);
    std::memcpy(pRgb555->pPixels, &pixel, sizeof(pixel));
    const std::vector<std::byte> expected555 = { std::byte{ Expand5(0x03u) }, std::byte{ Expand5(0x0Au) },
        std::byte{ Expand5(0x1Fu) }, std::byte{ 0xFF } };

    std3D_AllocSystemTexture(&texture, &pRgb555, 1u, STDCOLOR_FORMAT_RGB);
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGB);
    AssertBytes(expected555, Null().readTexture(CachedHandle(texture), 0));
    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(pRgb555);

    // ABGR8888 (red in the low byte) as RGBA becomes A8R8G8B8.
    tVBuffer* pAbgr = NewVBuffer(stdColor_cfABGR8888, 1u, 1u, 0);
    const uint8_t aAbgr[4] = { 0x11, 0x22, 0x33, 0x44 }; // R, G, B, A
    std::memcpy(pAbgr->pPixels, aAbgr, sizeof(aAbgr));
    const std::vector<std::byte> expectedAbgr = { std::byte{ 0x33 }, std::byte{ 0x22 }, std::byte{ 0x11 }, std::byte{ 0x44 } };

    std3D_AllocSystemTexture(&texture, &pAbgr, 1u, STDCOLOR_FORMAT_RGBA);
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGBA);
    AssertBytes(expectedAbgr, Null().readTexture(CachedHandle(texture), 0));
    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(pAbgr);

}

TEST(std3DBabylon, MipFilterNoneUploadsOneLevel)
{
    OpenStd3D();
    TEST_ASSERT_EQUAL_INT(0, std3D_SetMipmapFilter(STD3D_MIPMAPFILTER_NONE));

    static tSystemTexture texture;
    tVBuffer* apMips[2] = { NewVBuffer(stdColor_cfRGB565, 4u, 2u, 3), NewVBuffer(stdColor_cfRGB565, 2u, 1u, 9) };
    std3D_AllocSystemTexture(&texture, apMips, 2u, STDCOLOR_FORMAT_RGB);
    TEST_ASSERT_EQUAL_size_t(1u, std3D_GetMipMapCount(&texture));
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGB);
    AssertBytes(PackedPixels(apMips[0]), Null().readTexture(CachedHandle(texture), 0));

    size_t numWarnings = 0;
    TEST_ASSERT_TRUE(ReadTexture(CachedHandle(texture), 1, &numWarnings).empty());
    TEST_ASSERT_EQUAL_size_t(1u, numWarnings);

    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(apMips[0]);
    stdDisplay_VBufferFree(apMips[1]);
}

TEST(std3DBabylon, CacheKeepsTheDX9ListAndMemory)
{
    OpenStd3D();
    const Device3D* pDevice = std3D_GetCurrentDevice();
    const size_t total = pDevice->totalMemory;
    TEST_ASSERT_EQUAL_size_t(total, pDevice->availableMemory * 2u); // the display reports half free

    static tSystemTexture a;
    static tSystemTexture b;
    MakeCachedTexture(&a, stdColor_cfARGB8888, 1);
    MakeCachedTexture(&b, stdColor_cfARGB8888, 2);

    // Both added in the frame after std3D_Open (frame count 1), linked in order, their size counted.
    TEST_ASSERT_EQUAL_size_t(1u, a.frameNum);
    TEST_ASSERT_EQUAL_size_t(1u, b.frameNum);
    TEST_ASSERT_NULL(a.pPrevCachedTexture);
    TEST_ASSERT_EQUAL_PTR(&b, a.pNextCachedTexture);
    TEST_ASSERT_EQUAL_PTR(&a, b.pPrevCachedTexture);
    TEST_ASSERT_NULL(b.pNextCachedTexture);
    TEST_ASSERT_EQUAL_size_t(total / 2u - 2u * 32u, pDevice->availableMemory);

    // A used texture moves to the end of the list with the frame's count.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_UpdateFrameCount(&a);
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(2u, a.frameNum);
    TEST_ASSERT_EQUAL_PTR(&a, b.pNextCachedTexture);
    TEST_ASSERT_EQUAL_PTR(&b, a.pPrevCachedTexture);
    TEST_ASSERT_NULL(a.pNextCachedTexture);
    TEST_ASSERT_NULL(b.pPrevCachedTexture);

    // An uncached texture only gets the frame count.
    static tSystemTexture loose;
    std::memset(&loose, 0, sizeof(loose));
    std3D_UpdateFrameCount(&loose);
    TEST_ASSERT_EQUAL_size_t(2u, loose.frameNum);
    TEST_ASSERT_NULL(loose.pNextCachedTexture);

    // Clearing one unlinks it and destroys its render texture.
    const render::TextureHandle handleB = CachedHandle(b);
    std3D_ClearSystemTexture(&b);
    TEST_ASSERT_NULL(b.pCachedTexture);
    TEST_ASSERT_NULL(a.pPrevCachedTexture);
    TEST_ASSERT_EQUAL_size_t(total / 2u - 32u, pDevice->availableMemory);
    size_t numWarnings = 0;
    TEST_ASSERT_TRUE(ReadTexture(handleB, 0, &numWarnings).empty());
    TEST_ASSERT_EQUAL_size_t(1u, numWarnings);

    // The reset releases every cached texture, gives the memory back and restarts the frame count.
    const render::TextureHandle handleA = CachedHandle(a);
    std3D_ResetTextureCache();
    TEST_ASSERT_NULL(a.pCachedTexture);
    TEST_ASSERT_EQUAL_size_t(0u, a.frameNum);
    TEST_ASSERT_NULL(a.pNextCachedTexture);
    TEST_ASSERT_NULL(a.pPrevCachedTexture);
    TEST_ASSERT_EQUAL_size_t(total, pDevice->availableMemory);
    TEST_ASSERT_EQUAL_size_t(2u, std3D_GetMipMapCount(&a)); // the system texture stays
    TEST_ASSERT_TRUE(ReadTexture(handleA, 0).empty());

    std3D_AddToTextureCache(&a, STDCOLOR_FORMAT_RGBA);
    TEST_ASSERT_EQUAL_size_t(1u, a.frameNum);
    TEST_ASSERT_FALSE(Null().readTexture(CachedHandle(a), 0).empty());
    std3D_ClearSystemTexture(&a);
}

TEST(std3DBabylon, AFullCachePurgesATextureOfTheSameSizeNotUsedThisFrame)
{
    OpenStd3D();
    Device3D* pDevice = const_cast<Device3D*>(std3D_GetCurrentDevice());

    static tSystemTexture a;
    static tSystemTexture b;
    static tSystemTexture c;
    MakeCachedTexture(&a, stdColor_cfARGB8888, 1);
    MakeCachedTexture(&b, stdColor_cfARGB8888, 2);

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_UpdateFrameCount(&b); // b is used in frame 2, a is not
    const render::TextureHandle handleA = CachedHandle(a);
    pDevice->availableMemory = 0;
    MakeCachedTexture(&c, stdColor_cfARGB8888, 3);
    EndFrame();

    // a went (its memory came back first), b and c stay, in that order.
    TEST_ASSERT_NULL(a.pCachedTexture);
    TEST_ASSERT_EQUAL_size_t(0u, a.frameNum);
    TEST_ASSERT_EQUAL_PTR(&c, b.pNextCachedTexture);
    TEST_ASSERT_NULL(b.pPrevCachedTexture);
    TEST_ASSERT_EQUAL_size_t(0u, pDevice->availableMemory);
    TEST_ASSERT_TRUE(ReadTexture(handleA, 0).empty());

    std3D_ClearSystemTexture(&a);
    std3D_ClearSystemTexture(&b);
    std3D_ClearSystemTexture(&c);
}

TEST(std3DBabylon, ATextureClearedInASceneLivesUntilTheSubmit)
{
    OpenStd3D();
    static tSystemTexture texture;
    MakeCachedTexture(&texture, stdColor_cfRGB565, 5);
    const render::TextureHandle handle = CachedHandle(texture);

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_ClearSystemTexture(&texture);
    TEST_ASSERT_FALSE(Null().readTexture(handle, 0).empty());
    EndFrame();

    TEST_ASSERT_TRUE(ReadTexture(handle, 0).empty());
}

TEST(std3DBabylon, ScenesRecordAPassWithTheDX9Clears)
{
    OpenStd3D();
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size());

    // The first scene clears the new back buffer (std3DX9.c's viewport clear): black, Z 1.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size()); // nothing reaches the device before the submit
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);

    // Later scenes keep the back buffer, and depth too unless ClearZBuffer came first.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    std3D_ClearZBuffer();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(9u, Null().recorded().size());
    AssertScenePass(3, render::LoadOp::Load, render::LoadOp::Load);
    RecordedAt<render::EndPass>(4);
    AssertSubmitted(5);
    AssertScenePass(6, render::LoadOp::Load, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(7);
    AssertSubmitted(8);
    TEST_ASSERT_TRUE(RecordedAt<render::FrameSubmitted>(8).frame > RecordedAt<render::FrameSubmitted>(5).frame);

    // Inside a scene ClearZBuffer ends the pass and begins one that clears depth only.
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_ClearZBuffer();
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(5u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Load, render::LoadOp::Load);
    RecordedAt<render::EndPass>(1);
    AssertScenePass(2, render::LoadOp::Load, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(3);
    AssertSubmitted(4);
}

TEST(std3DBabylon, SceneCallsOutOfOrderFailLikeDX9)
{
    OpenStd3D();

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    TEST_ASSERT_NOT_EQUAL(0, std3D_StartScene()); // D3DERR_INVALIDCALL in std3DX9.c
    std3D_EndScene();
    std3D_EndScene();                              // no scene: logged, nothing recorded
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());

    std3D_Close();
    TEST_ASSERT_NOT_EQUAL(0, std3D_StartScene());
    std3D_ClearZBuffer();
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
}

TEST(std3DBabylon, ADisplaySizeChangeRecreatesTheBackBuffer)
{
    OpenStd3D(320u, 200u);
    TEST_ASSERT_EQUAL_UINT32(320u, stdRenderBabylon_GetBackBufferWidth());
    TEST_ASSERT_EQUAL_UINT32(200u, stdRenderBabylon_GetBackBufferHeight());
    TEST_ASSERT_EQUAL_size_t(320u * 200u * 4u, Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());
    TEST_ASSERT_TRUE(stdRenderBabylon_GetDepthFormat() == render::TextureFormat::D24S8);

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    const render::TextureHandle oldColor = stdRenderBabylon_GetColorTarget();
    const render::TextureHandle oldDepth = stdRenderBabylon_GetDepthTarget();

    stdDisplay_SetDefaultResolution(64u, 48u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();

    TEST_ASSERT_FALSE(stdRenderBabylon_GetColorTarget() == oldColor);
    TEST_ASSERT_FALSE(stdRenderBabylon_GetDepthTarget() == oldDepth);
    TEST_ASSERT_EQUAL_UINT32(64u, stdRenderBabylon_GetBackBufferWidth());
    TEST_ASSERT_EQUAL_UINT32(48u, stdRenderBabylon_GetBackBufferHeight());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    TEST_ASSERT_EQUAL_size_t(64u * 48u * 4u, Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());

    TEST_ASSERT_TRUE(ReadTexture(oldColor, 0).empty());
}

TEST(std3DBabylon, DepthIsD32FWithoutD24S8)
{
    UseNullDevice(render::NullDeviceDesc{ { render::TextureFormat::D24S8 } });
    OpenStd3D();
    TEST_ASSERT_TRUE(stdRenderBabylon_GetDepthFormat() == render::TextureFormat::D32F);
    TEST_ASSERT_EQUAL_INT(1, std3D_GetCurrentDevice()->hasZBuffer);
}

TEST(std3DBabylon, BadAndEmptyDrawsRecordNothing)
{
    OpenStd3D();
    StdVertexTL aVerts[3];
    std::memset(aVerts, 0, sizeof(aVerts));
    uint16_t aIndices[3]    = { 0, 1, 2 };
    uint16_t aBadIndices[3] = { 0, 1, 3 };

    // Outside a scene std3DX9.c's draws failed (D3DERR_INVALIDCALL).
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 3u);
    std3D_DrawLineStrip(aVerts, 3u);
    std3D_DrawPointList(aVerts, 3u);

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aBadIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, nullptr, 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, nullptr, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, std3D_g_maxVertices + 1u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 2u); // no whole triangle
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 0u, aIndices, 0u);
    std3D_DrawLineStrip(aVerts, 1u);                                               // no line
    std3D_DrawLineStrip(nullptr, 2u);
    std3D_DrawPointList(aVerts, 0u);
    std3D_DrawPointList(aVerts, std3D_g_maxVertices + 1u);
    std3D_SetWireframeRenderState();
    EndFrame();

    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
}

namespace
{
    StdVertexTL Vertex(float x, float y, float z, float rhw, uint32_t color, float u = 0.0f, float v = 0.0f)
    {
        StdVertexTL vertex;
        vertex.sx       = x;
        vertex.sy       = y;
        vertex.sz       = z;
        vertex.rhw      = rhw;
        vertex.color    = color;
        vertex.specular = 0xFF000000u;
        vertex.tu       = u;
        vertex.tv       = v;
        return vertex;
    }

    template <class T>
    std::vector<std::byte> BytesOf(const T* pItems, size_t count)
    {
        const std::byte* pBytes = reinterpret_cast<const std::byte*>(pItems);
        return std::vector<std::byte>(pBytes, pBytes + count * sizeof(T));
    }

    // Fragment uniform block 0 of the std3D shaders: fog params, fog color, alpha reference / 255.
    std::vector<std::byte> FragmentUniforms(const float (&aFogParams)[4], const float (&aFogColor)[4], float alphaRef)
    {
        float aBlock[12] = { 0.0f };
        std::memcpy(aBlock, aFogParams, sizeof(aFogParams));
        std::memcpy(aBlock + 4, aFogColor, sizeof(aFogColor));
        aBlock[8] = alphaRef;
        return BytesOf(aBlock, 12u);
    }

    std::vector<std::byte> NoFogUniforms(float alphaRef)
    {
        const float aZero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        return FragmentUniforms(aZero, aZero, alphaRef);
    }

    std::vector<std::byte> ViewportUniforms(float width, float height)
    {
        const float aViewport[4] = { 0.0f, 0.0f, width, height };
        return BytesOf(aViewport, 4u);
    }

    // The pipeline std3DX9.c's Direct3D state amounts to (InitRenderState, SetRenderState): blending
    // SRCALPHA, INVSRCALPHA for color and alpha, Z test LESSEQUAL, Z write unless
    // STD3D_RS_ZWRITE_DISABLED, no culling; StdVertexTL as the vertex layout.
    render::PipelineDesc ExpectedPipeline(render::PrimitiveType primitive, bool bZWrite)
    {
        render::PipelineDesc desc;
        desc.vertexShader   = stdBabylon::shaders::std3dVert();
        desc.fragmentShader = primitive == render::PrimitiveType::TriangleList ? stdBabylon::shaders::std3dFrag()
                                                                               : stdBabylon::shaders::std3dWireframeFrag();
        desc.vertexLayout.bufferStrides = { 32u };
        desc.vertexLayout.attributes    = {
            render::VertexAttribute{ 0, 0, render::VertexFormat::Float4, 0 },
            render::VertexAttribute{ 1, 0, render::VertexFormat::UByte4Norm, 16 },
            render::VertexAttribute{ 2, 0, render::VertexFormat::UByte4Norm, 20 },
            render::VertexAttribute{ 3, 0, render::VertexFormat::Float2, 24 },
        };
        desc.primitive = primitive;
        render::BlendState blend;
        blend.enabled  = true;
        blend.srcColor = render::BlendFactor::SrcAlpha;
        blend.dstColor = render::BlendFactor::OneMinusSrcAlpha;
        blend.colorOp  = render::BlendOp::Add;
        blend.srcAlpha = render::BlendFactor::SrcAlpha;
        blend.dstAlpha = render::BlendFactor::OneMinusSrcAlpha;
        blend.alphaOp  = render::BlendOp::Add;
        desc.colorTargets = { render::ColorTargetDesc{ render::TextureFormat::Bgra8, blend } };
        desc.depthFormat  = render::TextureFormat::D24S8;
        desc.depth        = render::DepthState{ true, bZWrite, render::CompareOp::LessOrEqual };
        desc.cull         = render::CullMode::None;
        return desc;
    }

    // The fragment PushUniforms of the record, in order.
    std::vector<std::vector<std::byte>> RecordedFragmentUniforms()
    {
        std::vector<std::vector<std::byte>> aUniforms;
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            const render::PushUniforms* pPush = std::get_if<render::PushUniforms>(&command);
            if ( pPush && pPush->stage == render::ShaderStage::Fragment )
            {
                aUniforms.push_back(pPush->data);
            }
        }
        return aUniforms;
    }
}

TEST(std3DBabylon, TwoDrawsRecordTheirStreamExactly)
{
    OpenStd3D();
    static tSystemTexture texture;
    MakeCachedTexture(&texture, stdColor_cfARGB8888, 3);
    const render::TextureHandle textureHandle = CachedHandle(texture);

    // What the pipelines are built from: the shaders' resources as std3DBabylon.cpp binds them.
    TEST_ASSERT_EQUAL_UINT32(0u, stdBabylon::shaders::std3dVert().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dVert().uniformCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dFrag().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dFrag().uniformCount);
    TEST_ASSERT_EQUAL_UINT32(0u, stdBabylon::shaders::std3dWireframeFrag().textureCount);
    TEST_ASSERT_EQUAL_UINT32(1u, stdBabylon::shaders::std3dWireframeFrag().uniformCount);

    const StdVertexTL aTriangle[3] = { Vertex(1.0f, 2.0f, 0.5f, 1.0f, 0xFF102030u, 0.0f, 0.0f),
        Vertex(9.0f, 2.0f, 0.5f, 1.0f, 0xFF405060u, 1.0f, 0.0f), Vertex(1.0f, 7.0f, 0.5f, 1.0f, 0xFF708090u, 0.0f, 1.0f) };
    const uint16_t aTriangleIndices[3] = { 0u, 1u, 2u };
    const StdVertexTL aQuad[4] = { Vertex(0.0f, 0.0f, 0.25f, 0.5f, 0x80FFFFFFu), Vertex(4.0f, 0.0f, 0.25f, 0.5f, 0x80FFFFFFu),
        Vertex(4.0f, 4.0f, 0.25f, 0.5f, 0x80FFFFFFu), Vertex(0.0f, 4.0f, 0.25f, 0.5f, 0x80FFFFFFu) };
    const uint16_t aQuadIndices[7] = { 0u, 1u, 2u, 0u, 2u, 3u, 1u }; // the 7th makes no whole triangle: left out

    const Std3DRenderState textured   = STD3D_RS_TEXFILTER_BILINEAR;
    const Std3DRenderState untextured = (Std3DRenderState)(STD3D_RS_ZWRITE_DISABLED | STD3D_RS_ALPHAREF_SET | STD3D_RS_TEX_CPAMP_U);

    render::PipelineHandle texturedPipeline;
    render::PipelineHandle untexturedPipeline;
    render::BufferHandle vertexBuffer;
    render::BufferHandle indexBuffer;
    render::SamplerHandle texturedSampler;
    render::SamplerHandle untexturedSampler;
    render::TextureHandle whiteTexture;
    for ( int frameNum = 0; frameNum < 2; ++frameNum )
    {
        Null().clearRecorded();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_UpdateFrameCount(&texture);
        std3D_DrawRenderList(texture.pCachedTexture, textured, const_cast<StdVertexTL*>(aTriangle), 3u, const_cast<uint16_t*>(aTriangleIndices), 3u);
        std3D_DrawRenderList(nullptr, untextured, const_cast<StdVertexTL*>(aQuad), 4u, const_cast<uint16_t*>(aQuadIndices), 7u);
        EndFrame();

        // The pipelines are created in the first frame only.
        const size_t numCreated = frameNum == 0 ? 2u : 0u;
        TEST_ASSERT_EQUAL_size_t(numCreated + 18u, Null().recorded().size());
        if ( frameNum == 0 )
        {
            const render::PipelineCreated& first  = RecordedAt<render::PipelineCreated>(0);
            const render::PipelineCreated& second = RecordedAt<render::PipelineCreated>(1);
            TEST_ASSERT_TRUE(first.desc == ExpectedPipeline(render::PrimitiveType::TriangleList, true));
            TEST_ASSERT_TRUE(second.desc == ExpectedPipeline(render::PrimitiveType::TriangleList, false));
            texturedPipeline   = first.pipeline;
            untexturedPipeline = second.pipeline;

            vertexBuffer      = RecordedAt<render::UpdateBuffer>(2).buffer;
            indexBuffer       = RecordedAt<render::UpdateBuffer>(3).buffer;
            texturedSampler   = RecordedAt<render::BindTexture>(12).sampler;
            untexturedSampler = RecordedAt<render::BindTexture>(16).sampler;
            whiteTexture      = RecordedAt<render::BindTexture>(16).texture;
            TEST_ASSERT_FALSE(vertexBuffer == indexBuffer);
            TEST_ASSERT_FALSE(texturedSampler == untexturedSampler);
            TEST_ASSERT_FALSE(whiteTexture == textureHandle);

            // std3DX9.c's white texture for untextured draws: one opaque white texel.
            AssertBytes(std::vector<std::byte>(4u, std::byte{ 0xFF }), Null().readTexture(whiteTexture, 0));
        }

        // Each frame streams from the start of the buffers, each draw after the one before.
        const size_t i = numCreated;
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i) == (render::UpdateBuffer{ vertexBuffer, 0u, BytesOf(aTriangle, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 1u) == (render::UpdateBuffer{ indexBuffer, 0u, BytesOf(aTriangleIndices, 3u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 2u) == (render::UpdateBuffer{ vertexBuffer, 96u, BytesOf(aQuad, 4u) }));
        TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(i + 3u) == (render::UpdateBuffer{ indexBuffer, 6u, BytesOf(aQuadIndices, 6u) }));

        const render::LoadOp load = frameNum == 0 ? render::LoadOp::Clear : render::LoadOp::Load;
        AssertScenePass(i + 4u, load, load);
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(i + 5u) == (render::PushUniforms{ render::ShaderStage::Vertex, 0u, ViewportUniforms(640.0f, 480.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(i + 6u) == render::SetPipeline{ texturedPipeline });
        TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(i + 7u) == (render::BindVertexBuffer{ 0u, vertexBuffer, 0u }));
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(i + 8u) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(0.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindIndexBuffer>(i + 9u) == (render::BindIndexBuffer{ indexBuffer, render::IndexType::Uint16, 0u }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindTexture>(i + 10u) == (render::BindTexture{ render::ShaderStage::Fragment, 0u, textureHandle, texturedSampler }));
        TEST_ASSERT_TRUE(RecordedAt<render::DrawIndexed>(i + 11u) == (render::DrawIndexed{ 3u, 0u, 0 }));
        TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(i + 12u) == render::SetPipeline{ untexturedPipeline });
        TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(i + 13u) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(160.0f / 255.0f) }));
        TEST_ASSERT_TRUE(RecordedAt<render::BindTexture>(i + 14u) == (render::BindTexture{ render::ShaderStage::Fragment, 0u, whiteTexture, untexturedSampler }));
        TEST_ASSERT_TRUE(RecordedAt<render::DrawIndexed>(i + 15u) == (render::DrawIndexed{ 6u, 3u, 3 }));
        RecordedAt<render::EndPass>(i + 16u);
        AssertSubmitted(i + 17u);
    }

    std3D_ClearSystemTexture(&texture);
}

TEST(std3DBabylon, AFrameThatNeedsMoreGrowsTheBuffers)
{
    OpenStd3D();

    // Two draws of 20000 points: 1.28 MB of vertices, more than the first 1 MiB vertex buffer holds.
    static StdVertexTL aPoints[20000];
    for ( size_t i = 0; i < std::size(aPoints); ++i )
    {
        aPoints[i] = Vertex((float)(i % 640u), (float)(i / 640u), 0.5f, 1.0f, 0xFFFFFFFFu);
    }

    std3D_SetWireframeRenderState();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawPointList(aPoints, std::size(aPoints));
    std3D_DrawPointList(aPoints, std::size(aPoints));
    EndFrame();

    // The second draw goes to a bigger buffer from its start; the frame's first draw keeps the old one.
    TEST_ASSERT_EQUAL_size_t(13u, Null().recorded().size());
    const render::PipelineCreated& created = RecordedAt<render::PipelineCreated>(0);
    TEST_ASSERT_TRUE(created.desc == ExpectedPipeline(render::PrimitiveType::PointList, true));
    const render::BufferHandle oldBuffer = RecordedAt<render::UpdateBuffer>(1).buffer;
    const render::BufferHandle newBuffer = RecordedAt<render::UpdateBuffer>(2).buffer;
    TEST_ASSERT_FALSE(oldBuffer == newBuffer);
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(1) == (render::UpdateBuffer{ oldBuffer, 0u, BytesOf(aPoints, 20000u) }));
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(2) == (render::UpdateBuffer{ newBuffer, 0u, BytesOf(aPoints, 20000u) }));
    AssertScenePass(3, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::PushUniforms>(4);
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(5) == render::SetPipeline{ created.pipeline });
    TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(6) == (render::BindVertexBuffer{ 0u, oldBuffer, 0u }));
    RecordedAt<render::PushUniforms>(7);
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(8) == (render::Draw{ 20000u, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(9) == (render::BindVertexBuffer{ 0u, newBuffer, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(10) == (render::Draw{ 20000u, 0u }));
    RecordedAt<render::EndPass>(11);
    AssertSubmitted(12);

    // The old buffer went with the submit; the next frame streams into the new one from its start.
    size_t numWarnings = 0;
    {
        babylon::log::CaptureSink sink;
        (void)Null().readBuffer(oldBuffer, 0, 32u);
        numWarnings = sink.records().size();
    }
    TEST_ASSERT_EQUAL_size_t(1u, numWarnings);

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawPointList(aPoints, 2u);
    EndFrame();
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(0) == (render::UpdateBuffer{ newBuffer, 0u, BytesOf(aPoints, 2u) }));
}

TEST(std3DBabylon, WireframeDrawsUseTheirPipelinesAndNoTexture)
{
    OpenStd3D(320u, 200u);
    const StdVertexTL aLine[3] = { Vertex(1.0f, 1.0f, 0.5f, 1.0f, 0xFF00FF00u), Vertex(9.0f, 1.0f, 0.5f, 1.0f, 0xFF00FF00u),
        Vertex(9.0f, 9.0f, 0.5f, 1.0f, 0xFF00FF00u) };
    const StdVertexTL aPoints[2] = { Vertex(3.0f, 4.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(5.0f, 6.0f, 0.5f, 1.0f, 0xFFFF0000u) };

    // The wireframe state keeps Z write off from the state before and drops fog.
    std3D_SetRenderState((Std3DRenderState)(STD3D_RS_ZWRITE_DISABLED | STD3D_RS_FOG_ENABLED | STD3D_RS_ALPHAREF_SET));
    std3D_SetWireframeRenderState();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawLineStrip(const_cast<StdVertexTL*>(aLine), 3u);
    std3D_DrawPointList(const_cast<StdVertexTL*>(aPoints), 2u);
    EndFrame();

    TEST_ASSERT_EQUAL_size_t(14u, Null().recorded().size());
    const render::PipelineCreated& lines  = RecordedAt<render::PipelineCreated>(0);
    const render::PipelineCreated& points = RecordedAt<render::PipelineCreated>(1);
    TEST_ASSERT_TRUE(lines.desc == ExpectedPipeline(render::PrimitiveType::LineStrip, false));
    TEST_ASSERT_TRUE(points.desc == ExpectedPipeline(render::PrimitiveType::PointList, false));
    const render::BufferHandle vertexBuffer = RecordedAt<render::UpdateBuffer>(2).buffer;
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(2) == (render::UpdateBuffer{ vertexBuffer, 0u, BytesOf(aLine, 3u) }));
    TEST_ASSERT_TRUE(RecordedAt<render::UpdateBuffer>(3) == (render::UpdateBuffer{ vertexBuffer, 96u, BytesOf(aPoints, 2u) }));
    AssertScenePass(4, render::LoadOp::Clear, render::LoadOp::Clear);
    TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(5) == (render::PushUniforms{ render::ShaderStage::Vertex, 0u, ViewportUniforms(320.0f, 200.0f) }));
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(6) == render::SetPipeline{ lines.pipeline });
    TEST_ASSERT_TRUE(RecordedAt<render::BindVertexBuffer>(7) == (render::BindVertexBuffer{ 0u, vertexBuffer, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::PushUniforms>(8) == (render::PushUniforms{ render::ShaderStage::Fragment, 0u, NoFogUniforms(160.0f / 255.0f) }));
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(9) == (render::Draw{ 3u, 0u }));
    TEST_ASSERT_TRUE(RecordedAt<render::SetPipeline>(10) == render::SetPipeline{ points.pipeline });
    TEST_ASSERT_TRUE(RecordedAt<render::Draw>(11) == (render::Draw{ 2u, 3u }));
    RecordedAt<render::EndPass>(12);
    AssertSubmitted(13);
}

TEST(std3DBabylon, ShaderFogChangesWithTheFogBitOnly)
{
    OpenStd3D();
    StdVertexTL aVerts[3] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu), Vertex(8.0f, 0.0f, 0.5f, 1.0f, 0xFFFFFFFFu),
        Vertex(0.0f, 8.0f, 0.5f, 1.0f, 0xFFFFFFFFu) };
    uint16_t aIndices[3] = { 0u, 1u, 2u };
    const Std3DRenderState fog   = STD3D_RS_FOG_ENABLED;
    const Std3DRenderState noFog = (Std3DRenderState)0;

    // std3DX9.c's shader fog: the end scaled by 2 - density, the values taken when the fog bit
    // changes (stdShader_SetFog), zeroed when it goes off (stdShader_DisableFog, the color stays).
    std3D_EnableFog(1, 1.0f);
    std3D_SetFog(0.25f, 0.5f, 0.75f, 2.0f, 50.0f);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);
    std3D_SetFog(1.0f, 0.0f, 0.0f, 10.0f, 20.0f);
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);   // the bit stayed: the old values, nothing pushed
    std3D_DrawRenderList(nullptr, noFog, aVerts, 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);   // now the new ones
    std3D_DrawRenderList(nullptr, noFog, aVerts, 3u, aIndices, 3u);
    std3D_EnableFog(0, 1.0f);
    std3D_DrawRenderList(nullptr, fog, aVerts, 3u, aIndices, 3u);   // fog is off: zeros, as pushed before
    EndFrame();

    const float aFog1[4]      = { 2.0f, 50.0f, 1.0f / 48.0f, 1.0f };
    const float aFogColor1[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
    const float aFog2[4]      = { 10.0f, 20.0f, 1.0f / 10.0f, 1.0f };
    const float aFogColor2[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    const float aOff[4]       = { 0.0f, 0.0f, 0.0f, 0.0f };
    const std::vector<std::vector<std::byte>> aExpected = {
        FragmentUniforms(aFog1, aFogColor1, 0.0f),
        FragmentUniforms(aOff, aFogColor1, 0.0f),
        FragmentUniforms(aFog2, aFogColor2, 0.0f),
        FragmentUniforms(aOff, aFogColor2, 0.0f),
    };
    const std::vector<std::vector<std::byte>> aPushed = RecordedFragmentUniforms();
    TEST_ASSERT_EQUAL_size_t(aExpected.size(), aPushed.size());
    for ( size_t i = 0; i < aExpected.size(); ++i )
    {
        AssertBytes(aExpected[i], aPushed[i]);
    }
}

TEST(std3DBabylon, ProjectionAndFogFollowDX9)
{
    TEST_ASSERT_EQUAL_INT((int)0x80070057L, std3D_SetProjection(1.0f, 1.0f, 1.001f));
    TEST_ASSERT_EQUAL_INT((int)0x80070057L, std3D_SetProjection(0.0f, 1.0f, 10.0f));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 0.1f, 100.0f));

    // std3DX9.c keeps the fog density only with an open device.
    std3D_EnableFog(1, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, std3D_g_fogDensity);
    OpenStd3D();
    std3D_EnableFog(1, 0.5f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, std3D_g_fogDensity);
    std3D_SetFog(0.1f, 0.2f, 0.3f, 4.0f, 100.0f);
    TEST_ASSERT_EQUAL_FLOAT(0.5f, std3D_g_fogDensity);
    TEST_ASSERT_EQUAL_INT(0, std3D_SetMipmapFilter(STD3D_MIPMAPFILTER_TRILINEAR));
}

TEST(std3DBabylon, DisplayEnvironmentListsTheRenderDevice)
{
    StdDisplayEnvironment* pEnv = std3D_BuildDisplayEnvironment();
    TEST_ASSERT_NOT_NULL(pEnv);
    TEST_ASSERT_EQUAL_size_t(1u, pEnv->numInfos);
    TEST_ASSERT_EQUAL_size_t(1u, pEnv->aDisplayInfos[0].numDevices);
    TEST_ASSERT_EQUAL_STRING("Jones3D Babylon Display (Babylon render, null)", pEnv->aDisplayInfos[0].aDevices[0].deviceDescription);
    std3D_FreeDisplayEnvironment(pEnv);
}

TEST(std3DBabylon, ACaptureReplaysIntoTheBabylonBackend)
{
    static StdVertexTL aVertices[3] = {
        {  1.0f,  2.0f, 4.0f, 0.5f, 0x11223344u, 0x55667788u,  0.25f, -0.5f },
        {  0.0f,  0.0f, 0.0f, 0.0f, 0xFFFFFFFFu, 0x00000000u,  0.0f,   0.0f },
        { -1.0f,  1.5f, 0.0f, 1.0f, 0x00FF00FFu, 0xDEADBEEFu,  1.0f,   2.0f },
    };
    static uint16_t aIndices[3] = { 0u, 1u, 2u };

    // Recorded through the forwarders on this backend, with a cached texture drawn in two scenes.
    TEST_ASSERT_TRUE(stdDisplayCapture_Open(kCapturePath, "babylon"));
    OpenStd3D();
    static tSystemTexture texture;
    MakeCachedTexture(&texture, stdColor_cfARGB8888, 7);
    for ( int frame = 0; frame < 2; ++frame )
    {
        std3D_ClearZBuffer();
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_UpdateFrameCount(&texture);
        std3D_DrawRenderList(texture.pCachedTexture, STD3D_RS_ZWRITE_DISABLED, aVertices, 3u, aIndices, 3u);
        EndFrame();
    }
    std3D_ClearSystemTexture(&texture);
    CloseStd3D();
    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    // The replay drives the same backends: no error, the mode set's clear and present, the same two
    // frames, the same capture again.
    UseNullDevice();
    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(kCapturePath, kReplayPath));
    TEST_ASSERT_EQUAL_size_t(28u, Null().recorded().size());
    RecordedAt<render::BeginPass>(0);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
    RecordedAt<render::PipelineCreated>(3); // once, in the first frame
    for ( size_t frame = 0; frame < 2; ++frame )
    {
        const size_t first = 4u + frame * 12u;
        RecordedAt<render::UpdateBuffer>(first);
        RecordedAt<render::UpdateBuffer>(first + 1u);
        AssertScenePassLoads(first + 2u, frame == 0 ? render::LoadOp::Clear : render::LoadOp::Load, render::LoadOp::Clear);
        const render::DrawIndexed& draw = RecordedAt<render::DrawIndexed>(first + 9u);
        TEST_ASSERT_EQUAL_UINT32(3u, draw.indexCount);
        RecordedAt<render::EndPass>(first + 10u);
        AssertSubmitted(first + 11u);
    }

    const std::vector<char> in  = ReadFile(kCapturePath);
    const std::vector<char> out = ReadFile(kReplayPath);
    TEST_ASSERT_TRUE(in.size() > 16u);
    TEST_ASSERT_EQUAL_size_t(in.size(), out.size());
    TEST_ASSERT_EQUAL_MEMORY(in.data(), out.data(), in.size());
}

TEST_GROUP_RUNNER(std3DBabylon)
{
    RUN_TEST_CASE(std3DBabylon, StartupFailsWithoutARenderDevice);
    RUN_TEST_CASE(std3DBabylon, OneDeviceDescribesTheRenderDevice);
    RUN_TEST_CASE(std3DBabylon, TextureFormatsAreTheDX9Formats);
    RUN_TEST_CASE(std3DBabylon, FormatsTheDeviceCannotSampleAreLeftOut);
    RUN_TEST_CASE(std3DBabylon, TexturesUploadEveryMipByteExactPerFormat);
    RUN_TEST_CASE(std3DBabylon, OtherLayoutsAreConvertedToTheFormatOfTheirType);
    RUN_TEST_CASE(std3DBabylon, MipFilterNoneUploadsOneLevel);
    RUN_TEST_CASE(std3DBabylon, CacheKeepsTheDX9ListAndMemory);
    RUN_TEST_CASE(std3DBabylon, AFullCachePurgesATextureOfTheSameSizeNotUsedThisFrame);
    RUN_TEST_CASE(std3DBabylon, ATextureClearedInASceneLivesUntilTheSubmit);
    RUN_TEST_CASE(std3DBabylon, ScenesRecordAPassWithTheDX9Clears);
    RUN_TEST_CASE(std3DBabylon, SceneCallsOutOfOrderFailLikeDX9);
    RUN_TEST_CASE(std3DBabylon, ADisplaySizeChangeRecreatesTheBackBuffer);
    RUN_TEST_CASE(std3DBabylon, DepthIsD32FWithoutD24S8);
    RUN_TEST_CASE(std3DBabylon, BadAndEmptyDrawsRecordNothing);
    RUN_TEST_CASE(std3DBabylon, TwoDrawsRecordTheirStreamExactly);
    RUN_TEST_CASE(std3DBabylon, AFrameThatNeedsMoreGrowsTheBuffers);
    RUN_TEST_CASE(std3DBabylon, WireframeDrawsUseTheirPipelinesAndNoTexture);
    RUN_TEST_CASE(std3DBabylon, ShaderFogChangesWithTheFogBitOnly);
    RUN_TEST_CASE(std3DBabylon, ProjectionAndFogFollowDX9);
    RUN_TEST_CASE(std3DBabylon, DisplayEnvironmentListsTheRenderDevice);
    RUN_TEST_CASE(std3DBabylon, ACaptureReplaysIntoTheBabylonBackend);
}

TEST_GROUP(stdRenderBabylonOption);

TEST_SETUP(stdRenderBabylonOption)
{
}

TEST_TEAR_DOWN(stdRenderBabylonOption)
{
}

TEST(stdRenderBabylonOption, TakesThePairAndKeepsTheRest)
{
    char aProgram[] = "Jones3D-headless", aFrames[] = "--frames", aFive[] = "5", aRender[] = "--render",
        aGpu[] = "gpu", aLoad[] = "--load", aSave[] = "x.nds";
    char* argv[] = { aProgram, aFrames, aFive, aRender, aGpu, aLoad, aSave, nullptr };
    int argc = 7;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_EQUAL_INT(5, argc);
    TEST_ASSERT_EQUAL_PTR(aProgram, argv[0]);
    TEST_ASSERT_EQUAL_PTR(aFrames, argv[1]);
    TEST_ASSERT_EQUAL_PTR(aFive, argv[2]);
    TEST_ASSERT_EQUAL_PTR(aLoad, argv[3]);
    TEST_ASSERT_EQUAL_PTR(aSave, argv[4]);
    TEST_ASSERT_NULL(argv[5]);
}

TEST(stdRenderBabylonOption, TheLastPairWins)
{
    char aProgram[] = "p", aRender[] = "--render", aGpu[] = "gpu", aRender2[] = "--render", aNull[] = "null";
    char* argv[] = { aProgram, aRender, aGpu, aRender2, aNull, nullptr };
    int argc = 5;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Gpu;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Null);
    TEST_ASSERT_EQUAL_INT(1, argc);
    TEST_ASSERT_NULL(argv[1]);
}

TEST(stdRenderBabylonOption, NoPairChangesNothing)
{
    char aProgram[] = "p", aState[] = "--state", aFile[] = "s.txt";
    char* argv[] = { aProgram, aState, aFile, nullptr };
    int argc = 3;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Gpu;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_EQUAL_PTR(aState, argv[1]);
    TEST_ASSERT_EQUAL_PTR(aFile, argv[2]);
}

TEST(stdRenderBabylonOption, AMissingOrUnknownValueIsRefused)
{
    char aProgram[] = "p", aRender[] = "--render", aVulkan[] = "vulkan", aRender2[] = "--render", aGpu[] = "gpu";
    char* argvUnknown[] = { aProgram, aRender2, aGpu, aRender, aVulkan, nullptr };
    int argc = 5;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;

    TEST_ASSERT_FALSE(stdRenderBabylon_TakeRenderOption(&argc, argvUnknown, &type));
    TEST_ASSERT_EQUAL_INT(5, argc);
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Null);
    TEST_ASSERT_EQUAL_PTR(aRender2, argvUnknown[1]);
    TEST_ASSERT_EQUAL_PTR(aVulkan, argvUnknown[4]);

    char* argvMissing[] = { aProgram, aRender, nullptr };
    argc = 2;
    TEST_ASSERT_FALSE(stdRenderBabylon_TakeRenderOption(&argc, argvMissing, &type));
    TEST_ASSERT_EQUAL_INT(2, argc);
    TEST_ASSERT_EQUAL_PTR(aRender, argvMissing[1]);
}

TEST_GROUP_RUNNER(stdRenderBabylonOption)
{
    RUN_TEST_CASE(stdRenderBabylonOption, TakesThePairAndKeepsTheRest);
    RUN_TEST_CASE(stdRenderBabylonOption, TheLastPairWins);
    RUN_TEST_CASE(stdRenderBabylonOption, NoPairChangesNothing);
    RUN_TEST_CASE(stdRenderBabylonOption, AMissingOrUnknownValueIsRefused);
}

TEST_GROUP(stdRenderReplayBabylonOption);

TEST_SETUP(stdRenderReplayBabylonOption)
{
}

TEST_TEAR_DOWN(stdRenderReplayBabylonOption)
{
}

TEST(stdRenderReplayBabylonOption, TakesTheOptionsAndKeepsTheRest)
{
    char aProgram[] = "Jones3D-replay", aRender[] = "--render", aGpu[] = "gpu", aFramesOut[] = "--frames-out",
        aDir[] = "frames", aEvery[] = "--every", aThree[] = "3", aCapture[] = "--capture", aOut[] = "out.rcap";
    char* argv[] = { aProgram, aRender, aGpu, aFramesOut, aDir, aEvery, aThree, aCapture, aOut, nullptr };
    int argc = 9;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_TRUE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, true, &output, &pError));
    TEST_ASSERT_NULL(pError);
    TEST_ASSERT_EQUAL_STRING("frames", output.pDirectory);
    TEST_ASSERT_EQUAL_UINT32(3u, output.every);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_EQUAL_PTR(aProgram, argv[0]);
    TEST_ASSERT_EQUAL_PTR(aCapture, argv[1]);
    TEST_ASSERT_EQUAL_PTR(aOut, argv[2]);
    TEST_ASSERT_NULL(argv[3]);
}

TEST(stdRenderReplayBabylonOption, FramesOutNeedsTheGpu)
{
    char aProgram[] = "p", aFramesOut[] = "--frames-out", aDir[] = "frames";
    char* argv[] = { aProgram, aFramesOut, aDir, nullptr };
    int argc = 3;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, false, &output, &pError));
    TEST_ASSERT_NOT_NULL(pError);
    TEST_ASSERT_EQUAL_INT(3, argc);
    TEST_ASSERT_NULL(output.pDirectory);
}

TEST(stdRenderReplayBabylonOption, EveryNeedsFramesOut)
{
    char aProgram[] = "p", aEvery[] = "--every", aTwo[] = "2";
    char* argv[] = { aProgram, aEvery, aTwo, nullptr };
    int argc = 3;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;

    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, true, &output, &pError));
    TEST_ASSERT_NOT_NULL(pError);
    TEST_ASSERT_EQUAL_INT(3, argc);
}

TEST(stdRenderReplayBabylonOption, AMissingOrBadValueIsRefused)
{
    char aProgram[] = "p", aFramesOut[] = "--frames-out", aEmpty[] = "", aDir[] = "frames", aEvery[] = "--every",
        aZero[] = "0", aJunk[] = "2x";
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;
    int argc;

    char* argvMissing[] = { aProgram, aFramesOut, nullptr };
    argc = 2;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvMissing, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(2, argc);

    char* argvEmpty[] = { aProgram, aFramesOut, aEmpty, nullptr };
    argc = 3;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvEmpty, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(3, argc);

    char* argvZero[] = { aProgram, aFramesOut, aDir, aEvery, aZero, nullptr };
    argc = 5;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvZero, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(5, argc);

    char* argvJunk[] = { aProgram, aFramesOut, aDir, aEvery, aJunk, nullptr };
    argc = 5;
    TEST_ASSERT_FALSE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argvJunk, true, &output, &pError));
    TEST_ASSERT_EQUAL_INT(5, argc);
}

TEST_GROUP_RUNNER(stdRenderReplayBabylonOption)
{
    RUN_TEST_CASE(stdRenderReplayBabylonOption, TakesTheOptionsAndKeepsTheRest);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, FramesOutNeedsTheGpu);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, EveryNeedsFramesOut);
    RUN_TEST_CASE(stdRenderReplayBabylonOption, AMissingOrBadValueIsRefused);
}

TEST_GROUP(std3DBabylonGpu);

namespace
{
    // The windowed case's SDL objects, released in the tear-down, which also runs after a failed
    // assert: the device first, then the window, then the platform.
    std::unique_ptr<babylon::platform::Platform> std3DBabylonGpuTest_pPlatform;
    std::unique_ptr<babylon::platform::Window> std3DBabylonGpuTest_pWindow;

    const char* const kReplayFramesCapturePath = "std3DBabylonTest_ReplayFrames.rcap";
    const char* const kReplayFramesDir         = "std3DBabylonTest_ReplayFrames";

    void RemoveReplayFrames()
    {
        std::remove(kReplayFramesCapturePath);
        std::error_code error;
        std::filesystem::remove_all(kReplayFramesDir, error);
    }
}

TEST_SETUP(std3DBabylonGpu)
{
    StdGeneralTest_Startup();
    stdRenderCapture_ResetGlobals();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
    RemoveReplayFrames();
}

TEST_TEAR_DOWN(std3DBabylonGpu)
{
    stdDisplayCapture_Close();
    stdRenderCapture_ResetGlobals();
    CloseStd3D();
    stdRenderBabylon_Close();
    DropDevice();
    std3DBabylonGpuTest_pWindow.reset();
    std3DBabylonGpuTest_pPlatform.reset();
    RemoveReplayFrames();
    StdGeneralTest_Shutdown();
}

TEST(std3DBabylonGpu, TexturesReadBackByteExactPerFormat)
{
    UseDevice(CreateGpuDevice(nullptr));
    if ( !std3DBabylonTest_pDevice )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    OpenStd3D();
    TEST_ASSERT_EQUAL_size_t(5u, std3D_GetNumTextureFormats());

    const ColorInfo* aFormats[] = { &stdColor_cfRGB8888, &stdColor_cfARGB8888, &stdColor_cfRGB565,
        &stdColor_cfARGB5551, &stdColor_cfARGB4444 };
    uint8_t seed = 11;
    for ( const ColorInfo* pCi : aFormats )
    {
        static tSystemTexture texture;
        std::vector<std::byte> mip0;
        std::vector<std::byte> mip1;
        MakeCachedTexture(&texture, *pCi, seed, &mip0, &mip1);
        seed = (uint8_t)(seed + 29u);

        const bool bOpaque = pCi == &stdColor_cfRGB8888;
        const render::TextureHandle handle = CachedHandle(texture);
        AssertBytes(bOpaque ? Opaque(mip0) : mip0, ReadTexture(handle, 0));
        AssertBytes(bOpaque ? Opaque(mip1) : mip1, ReadTexture(handle, 1));
        std3D_ClearSystemTexture(&texture);
    }

    // A scene on the GPU: the first pass clears the back buffer to transparent black.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    const std::vector<std::byte> pixels = ReadTexture(stdRenderBabylon_GetColorTarget(), 0);
    TEST_ASSERT_EQUAL_size_t(640u * 480u * 4u, pixels.size());
    TEST_ASSERT_EQUAL_INT(0, (int)pixels[0]);
    TEST_ASSERT_EQUAL_INT(0, (int)pixels[pixels.size() - 1]);
}

TEST(std3DBabylonGpu, TheWindowedFramePresentsTheBackBuffer)
{
    std3DBabylonGpuTest_pPlatform = babylon::platform::createSdlPlatform();
    if ( !std3DBabylonGpuTest_pPlatform )
    {
        if ( GpuRequired() )
        {
            TEST_FAIL_MESSAGE("no SDL platform (run with SDL_VIDEO_DRIVER=offscreen)");
        }
        TEST_IGNORE_MESSAGE("no SDL platform");
    }

    std3DBabylonGpuTest_pWindow =
        std3DBabylonGpuTest_pPlatform->createWindow(babylon::platform::WindowDesc{ "std3DBabylonTest", { 64, 48 } });
    TEST_ASSERT_NOT_NULL(std3DBabylonGpuTest_pWindow.get());
    UseDevice(CreateGpuDevice(std3DBabylonGpuTest_pWindow.get()));
    if ( !std3DBabylonTest_pDevice )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(std3DBabylonTest_pDevice->info().hasSwapchain);

    // Two frames on the back buffer, each submitted with the present pass to the swapchain. Every
    // command must be accepted: the device warns about none.
    std::vector<std::string> aWarnings;
    bool bOpened   = false;
    bool bResized  = false;
    bool bRecorded = true;
    bool bSubmitted = true;
    {
        babylon::log::CaptureSink sink;
        bOpened = stdRenderBabylon_Open(std3DBabylonTest_pDevice.get());
        bool bRecreated = false;
        bResized = bOpened && stdRenderBabylon_SetBackBufferSize(32u, 24u, &bRecreated) && bRecreated;
        for ( int frame = 0; bResized && frame < 2; ++frame )
        {
            render::PassDesc pass;
            pass.colorTargets.push_back(render::ColorAttachment{ stdRenderBabylon_GetColorTarget(), 0,
                render::LoadOp::Clear, render::StoreOp::Store, render::ClearColor{ 1.0f, 0.0f, 0.0f, 1.0f } });
            bRecorded  = stdRenderBabylon_GetFrame().beginPass(pass) && stdRenderBabylon_GetFrame().endPass() && bRecorded;
            bSubmitted = stdRenderBabylon_SubmitFrame() && !stdRenderBabylon_HasFrame() && bSubmitted;
        }

        for ( const babylon::log::CapturedRecord& record : sink.records() )
        {
            aWarnings.push_back(record.message);
        }
    }

    TEST_ASSERT_TRUE(bOpened);
    TEST_ASSERT_TRUE(bResized);
    TEST_ASSERT_TRUE(bRecorded);
    TEST_ASSERT_TRUE(bSubmitted);
    for ( const std::string& warning : aWarnings )
    {
        TEST_FAIL_MESSAGE(warning.c_str());
    }
}

namespace
{
    // The back buffer as the last RenderScene read it back: Bgra8, rows top first.
    std::vector<std::byte> std3DBabylonGpuTest_pixels;

    // std3D on the SDL GPU device at width x height; false when there is no GPU (the test is then
    // ignored, or failed with BABYLON_REQUIRE_GPU=1).
    bool OpenGpuStd3D(uint32_t width, uint32_t height)
    {
        UseDevice(CreateGpuDevice(nullptr));
        if ( !std3DBabylonTest_pDevice )
        {
            return false;
        }
        OpenStd3D(width, height);
        return true;
    }

    // Runs draws in one scene, then reads the back buffer back. Every render warning fails the test:
    // a draw the device dropped draws nothing.
    template <class F>
    void RenderScene(F draws)
    {
        std::vector<std::string> aWarnings;
        int startResult = 1;
        {
            babylon::log::CaptureSink sink;
            startResult = std3D_StartScene();
            if ( startResult == 0 )
            {
                draws();
                std3D_EndScene();
                (void)stdDisplay_Update(); // no assert while the sink is attached
            }
            std3DBabylonGpuTest_pixels = std3DBabylonTest_pDevice->readTexture(stdRenderBabylon_GetColorTarget(), 0);
            for ( const babylon::log::CapturedRecord& record : sink.records() )
            {
                aWarnings.push_back(record.message);
            }
        }

        TEST_ASSERT_EQUAL_INT(0, startResult);
        for ( const std::string& warning : aWarnings )
        {
            TEST_FAIL_MESSAGE(warning.c_str());
        }
        TEST_ASSERT_EQUAL_size_t((size_t)stdRenderBabylon_GetBackBufferWidth() * stdRenderBabylon_GetBackBufferHeight() * 4u,
            std3DBabylonGpuTest_pixels.size());
    }

    // The pixel at (x, y) of the last RenderScene is r, g, b, a, each within tolerance.
    void AssertPixel(uint32_t x, uint32_t y, int r, int g, int b, int a, int tolerance = 1)
    {
        const size_t at = ((size_t)y * stdRenderBabylon_GetBackBufferWidth() + x) * 4u;
        TEST_ASSERT_TRUE(at + 4u <= std3DBabylonGpuTest_pixels.size());
        const int actual[4] = { (int)std3DBabylonGpuTest_pixels[at + 2], (int)std3DBabylonGpuTest_pixels[at + 1],
            (int)std3DBabylonGpuTest_pixels[at], (int)std3DBabylonGpuTest_pixels[at + 3] };
        const int expected[4] = { r, g, b, a };
        char aMessage[128];
        std::snprintf(aMessage, sizeof(aMessage), "pixel (%u, %u) is (%d, %d, %d, %d), expected (%d, %d, %d, %d)", x, y,
            actual[0], actual[1], actual[2], actual[3], r, g, b, a);
        for ( int i = 0; i < 4; ++i )
        {
            TEST_ASSERT_INT_WITHIN_MESSAGE(tolerance, expected[i], actual[i], aMessage);
        }
    }

    void AssertClear(uint32_t x, uint32_t y)
    {
        AssertPixel(x, y, 0, 0, 0, 0, 0);
    }

    // A quad from (x0, y0) to (x1, y1) in screen pixels, texture coordinates 0 to 1, rhwLeft on the
    // left edge and rhwRight on the right one; two triangles split along (x0, y0) to (x1, y1).
    void DrawQuad(tSysTexture* pTex, Std3DRenderState state, float x0, float y0, float x1, float y1, float z, uint32_t color,
        float rhwLeft = 1.0f, float rhwRight = 1.0f)
    {
        StdVertexTL aVerts[4] = { Vertex(x0, y0, z, rhwLeft, color, 0.0f, 0.0f), Vertex(x1, y0, z, rhwRight, color, 1.0f, 0.0f),
            Vertex(x1, y1, z, rhwRight, color, 1.0f, 1.0f), Vertex(x0, y1, z, rhwLeft, color, 0.0f, 1.0f) };
        uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        std3D_DrawRenderList(pTex, state, aVerts, 4u, aIndices, 6u);
    }

    // A width x height texture of ci with one texel value each (its bytes little endian), cached.
    void MakeTexture(tSystemTexture* pTexture, const ColorInfo& ci, uint32_t width, uint32_t height, const std::vector<uint32_t>& aTexels)
    {
        tVBuffer* pVBuffer = NewVBuffer(ci, width, height, 0);
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pVBuffer));
        const size_t texelBytes = ci.bpp / 8u;
        for ( uint32_t y = 0; y < height; ++y )
        {
            for ( uint32_t x = 0; x < width; ++x )
            {
                uint8_t* pTexel = pVBuffer->pPixels + y * pVBuffer->rasterInfo.rowSize + x * texelBytes;
                for ( size_t i = 0; i < texelBytes; ++i )
                {
                    pTexel[i] = (uint8_t)(aTexels[y * width + x] >> (8u * i));
                }
            }
        }
        TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pVBuffer));

        std::memset(pTexture, 0, sizeof(*pTexture));
        std3D_AllocSystemTexture(pTexture, &pVBuffer, 1u, std3D_GetColorFormat(&ci));
        std3D_AddToTextureCache(pTexture, std3D_GetColorFormat(&ci));
        stdDisplay_VBufferFree(pVBuffer);
        TEST_ASSERT_NOT_NULL(pTexture->pCachedTexture);
    }

    // A texel of ci from channel values in the format's own bits.
    uint32_t Texel(const ColorInfo& ci, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
    {
        return (r << ci.redPosShift) | (g << ci.greenPosShift) | (b << ci.bluePosShift) | (ci.alphaBPP ? a << ci.alphaPosShift : 0u);
    }

    uint32_t MaxOf(uint32_t bits)
    {
        return (1u << bits) - 1u;
    }

    // A channel of bits bits as an 8-bit unorm value, as the GPU converts it; 255 without bits.
    int Unorm8(uint32_t value, uint32_t bits)
    {
        return bits ? (int)std::lround(value * 255.0 / MaxOf(bits)) : 255;
    }
}

TEST(std3DBabylonGpu, AVertexColoredTriangleInterpolatesItsColors)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    RenderScene([]
    {
        StdVertexTL aVerts[3] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(64.0f, 0.0f, 0.5f, 1.0f, 0xFF00FF00u),
            Vertex(0.0f, 64.0f, 0.5f, 1.0f, 0xFF0000FFu) };
        uint16_t aIndices[3] = { 0u, 1u, 2u };
        std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 3u);
    });

    // Direct3D 9 pixel centers are at integer coordinates: pixel (x, y) is weighted x / 64 green and
    // y / 64 blue.
    const uint32_t aPixels[][2] = { { 8u, 8u }, { 40u, 10u }, { 10u, 40u }, { 1u, 1u } };
    for ( const auto& pixel : aPixels )
    {
        const double green = pixel[0] / 64.0;
        const double blue  = pixel[1] / 64.0;
        AssertPixel(pixel[0], pixel[1], (int)std::lround(255.0 * (1.0 - green - blue)), (int)std::lround(255.0 * green),
            (int)std::lround(255.0 * blue), 255, 2);
    }
    AssertClear(40u, 40u);
    AssertClear(63u, 63u);
}

TEST(std3DBabylonGpu, TexturedQuadsSampleEveryTextureFormat)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_EQUAL_size_t(5u, std3D_GetNumTextureFormats());

    const ColorInfo* aFormats[] = { &stdColor_cfRGB8888, &stdColor_cfARGB8888, &stdColor_cfRGB565, &stdColor_cfARGB5551,
        &stdColor_cfARGB4444 };
    for ( const ColorInfo* pCi : aFormats )
    {
        // 2 x 2 texels: red, green, blue, half gray, all opaque; on an 8 x 8 quad with point filtering
        // each covers 4 x 4 pixels.
        const ColorInfo& ci = *pCi;
        const uint32_t maxA = MaxOf(ci.alphaBPP);
        const uint32_t aChannels[4][3] = { { MaxOf(ci.redBPP), 0u, 0u }, { 0u, MaxOf(ci.greenBPP), 0u }, { 0u, 0u, MaxOf(ci.blueBPP) },
            { MaxOf(ci.redBPP) / 2u, MaxOf(ci.greenBPP) / 2u, MaxOf(ci.blueBPP) / 2u } };
        std::vector<uint32_t> aTexels;
        for ( const auto& channels : aChannels )
        {
            aTexels.push_back(Texel(ci, channels[0], channels[1], channels[2], maxA));
        }

        static tSystemTexture texture;
        MakeTexture(&texture, ci, 2u, 2u, aTexels);
        RenderScene([&]
        {
            DrawQuad(texture.pCachedTexture, (Std3DRenderState)0, 0.0f, 0.0f, 8.0f, 8.0f, 0.5f, 0xFFFFFFFFu);
        });

        const uint32_t aPixels[4][2] = { { 1u, 1u }, { 5u, 1u }, { 1u, 5u }, { 6u, 6u } };
        for ( size_t i = 0; i < 4; ++i )
        {
            AssertPixel(aPixels[i][0], aPixels[i][1], Unorm8(aChannels[i][0], ci.redBPP), Unorm8(aChannels[i][1], ci.greenBPP),
                Unorm8(aChannels[i][2], ci.blueBPP), 255);
        }
        std3D_ClearSystemTexture(&texture);
    }
}

TEST(std3DBabylonGpu, TheAlphaTestDiscardsAlphaNotAboveTheReference)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // White texels of alpha 0, 128, 160 and 192, each 2 pixels wide on an 8 x 2 quad, drawn in front
    // with Z write; then an opaque green quad behind both. A fragment that passed the test wrote its Z
    // and hides the green even with alpha 0; a discarded one lets it through.
    static tSystemTexture texture;
    MakeTexture(&texture, stdColor_cfARGB8888, 4u, 1u, { 0x00FFFFFFu, 0x80FFFFFFu, 0xA0FFFFFFu, 0xC0FFFFFFu });
    RenderScene([]
    {
        DrawQuad(texture.pCachedTexture, STD3D_RS_ALPHAREF_SET, 0.0f, 0.0f, 8.0f, 2.0f, 0.2f, 0xFFFFFFFFu); // ref 160
        DrawQuad(texture.pCachedTexture, (Std3DRenderState)0, 0.0f, 4.0f, 8.0f, 6.0f, 0.2f, 0xFFFFFFFFu);    // ref 0
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 8.0f, 6.0f, 0.8f, 0xFF00FF00u);
    });

    // What passed is blended over the transparent black clear: rgb * alpha, alpha * alpha.
    AssertPixel(0u, 0u, 0, 255, 0, 255);
    AssertPixel(2u, 0u, 0, 255, 0, 255);
    AssertPixel(4u, 0u, 0, 255, 0, 255);  // 160 is not greater than 160
    AssertPixel(6u, 0u, 192, 192, 192, 145);
    AssertPixel(0u, 4u, 0, 255, 0, 255);  // 0 is not greater than 0
    AssertPixel(2u, 4u, 128, 128, 128, 64);
    AssertPixel(4u, 4u, 160, 160, 160, 100);
    AssertPixel(6u, 4u, 192, 192, 192, 145);
    std3D_ClearSystemTexture(&texture);
}

TEST(std3DBabylonGpu, DrawsBlendWithSourceAlpha)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 8.0f, 8.0f, 0.5f, 0xFFFF0000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 8.0f, 8.0f, 0.5f, 0x800000FFu); // blue at 128 / 255
    });

    // blue * a + red * (1 - a); alpha a * a + 1 * (1 - a).
    const double a = 128.0 / 255.0;
    AssertPixel(4u, 4u, (int)std::lround(255.0 * (1.0 - a)), 0, (int)std::lround(255.0 * a),
        (int)std::lround(255.0 * (a * a + 1.0 - a)));
}

TEST(std3DBabylonGpu, TheZTestIsLessOrEqualAndZWritesFollowTheState)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    RenderScene([]
    {
        // Near red without Z write, far green: green.
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED, 0.0f, 0.0f, 8.0f, 8.0f, 0.2f, 0xFFFF0000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 8.0f, 8.0f, 0.8f, 0xFF00FF00u);
        // Near red with Z write, far green: red.
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 24.0f, 8.0f, 0.2f, 0xFFFF0000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 24.0f, 8.0f, 0.8f, 0xFF00FF00u);
        // The same Z twice: D3DCMP_LESSEQUAL passes the second.
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 40.0f, 8.0f, 0.5f, 0xFFFF0000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 40.0f, 8.0f, 0.5f, 0xFF00FF00u);
    });

    AssertPixel(4u, 4u, 0, 255, 0, 255);
    AssertPixel(20u, 4u, 255, 0, 0, 255);
    AssertPixel(36u, 4u, 0, 255, 0, 255);
}

TEST(std3DBabylonGpu, FogIsLinearInWBetweenStartAndEnd)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // Blue fog from w 2 to w 50 (density 1: the end is not scaled).
    std3D_EnableFog(1, 1.0f);
    std3D_SetFog(0.0f, 0.0f, 1.0f, 2.0f, 50.0f);
    RenderScene([]
    {
        DrawQuad(nullptr, STD3D_RS_FOG_ENABLED, 0.0f, 0.0f, 8.0f, 8.0f, 0.5f, 0xFFFFFFFFu, 1.0f, 1.0f);                 // w 1: none
        DrawQuad(nullptr, STD3D_RS_FOG_ENABLED, 8.0f, 0.0f, 16.0f, 8.0f, 0.5f, 0xFFFFFFFFu, 1.0f / 26.0f, 1.0f / 26.0f);   // w 26: half
        DrawQuad(nullptr, STD3D_RS_FOG_ENABLED, 16.0f, 0.0f, 24.0f, 8.0f, 0.5f, 0xFFFFFFFFu, 1.0f / 100.0f, 1.0f / 100.0f); // w 100: all
        DrawQuad(nullptr, (Std3DRenderState)0, 24.0f, 0.0f, 32.0f, 8.0f, 0.5f, 0xFFFFFFFFu, 1.0f / 100.0f, 1.0f / 100.0f);   // no fog bit
    });

    AssertPixel(4u, 4u, 255, 255, 255, 255);
    AssertPixel(12u, 4u, 128, 128, 255, 255);
    AssertPixel(20u, 4u, 0, 0, 255, 255);
    AssertPixel(28u, 4u, 255, 255, 255, 255);
}

TEST(std3DBabylonGpu, LinesAndPointsLightTheirPixels)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    RenderScene([]
    {
        std3D_SetWireframeRenderState();
        StdVertexTL aLine[3] = { Vertex(2.0f, 10.0f, 0.5f, 1.0f, 0xFF00FF00u), Vertex(20.0f, 10.0f, 0.5f, 1.0f, 0xFF00FF00u),
            Vertex(20.0f, 30.0f, 0.5f, 1.0f, 0xFF00FF00u) };
        std3D_DrawLineStrip(aLine, 3u);
        StdVertexTL aPoints[2] = { Vertex(5.0f, 40.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(40.0f, 3.0f, 0.5f, 1.0f, 0xFFFF0000u) };
        std3D_DrawPointList(aPoints, 2u);
    });

    // Both segments of the strip, one pixel wide.
    for ( uint32_t x = 6u; x <= 16u; ++x )
    {
        AssertPixel(x, 10u, 0, 255, 0, 255);
        AssertClear(x, 9u);
        AssertClear(x, 11u);
    }
    for ( uint32_t y = 14u; y <= 26u; ++y )
    {
        AssertPixel(20u, y, 0, 255, 0, 255);
        AssertClear(19u, y);
        AssertClear(21u, y);
    }

    // Each point is the one pixel it names.
    AssertPixel(5u, 40u, 255, 0, 0, 255);
    AssertPixel(40u, 3u, 255, 0, 0, 255);
    AssertClear(4u, 40u);
    AssertClear(6u, 40u);
    AssertClear(5u, 39u);
    AssertClear(5u, 41u);
    AssertClear(39u, 3u);
    AssertClear(40u, 4u);
}

TEST(std3DBabylonGpu, TexturingIsPerspectiveCorrect)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // A 256 x 1 texture whose texel i has red i, on a 64 x 32 quad with rhw 1 on the left edge and
    // 1/4 on the right one, point filtered and clamped.
    std::vector<uint32_t> aTexels;
    for ( uint32_t i = 0; i < 256u; ++i )
    {
        aTexels.push_back(0xFF000000u | (i << 16));
    }
    static tSystemTexture texture;
    MakeTexture(&texture, stdColor_cfARGB8888, 256u, 1u, aTexels);
    const float rhwLeft  = 1.0f;
    const float rhwRight = 0.25f;
    RenderScene([&]
    {
        DrawQuad(texture.pCachedTexture, (Std3DRenderState)(STD3D_RS_TEX_CPAMP_U | STD3D_RS_TEX_CPAMP_V), 0.0f, 0.0f, 64.0f, 32.0f, 0.5f,
            0xFFFFFFFFu, rhwLeft, rhwRight);
    });

    // At the fraction s of the way across, u = s * rhwRight / ((1 - s) * rhwLeft + s * rhwRight),
    // far from the affine u = s; one pixel above the split diagonal and one below.
    const uint32_t aPixels[][2] = { { 32u, 8u }, { 16u, 24u }, { 48u, 20u } };
    for ( const auto& pixel : aPixels )
    {
        const double s = pixel[0] / 64.0;
        const double u = s * rhwRight / ((1.0 - s) * rhwLeft + s * rhwRight);
        const int texel = (int)std::floor(u * 256.0);
        TEST_ASSERT_TRUE(std::abs(texel - (int)(s * 256.0)) > 20);
        AssertPixel(pixel[0], pixel[1], texel, 0, 0, 255, 1);
    }
    std3D_ClearSystemTexture(&texture);
}

TEST(std3DBabylonGpu, ReplayWritesFrameImages)
{
    // A tiny capture with one 64 x 64 frame: a red quad over the transparent black clear. The Null
    // device records the calls; the replay below makes them again on the GPU device.
    UseNullDevice();
    TEST_ASSERT_TRUE(stdDisplayCapture_Open(kReplayFramesCapturePath, "babylon"));
    OpenStd3D(64u, 64u);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    {
        StdVertexTL aVerts[4] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(64.0f, 0.0f, 0.5f, 1.0f, 0xFFFF0000u),
            Vertex(64.0f, 64.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(0.0f, 64.0f, 0.5f, 1.0f, 0xFFFF0000u) };
        uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 4u, aIndices, 6u);
    }
    std3D_EndScene();
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_Update());
    CloseStd3D();
    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    UseDevice(CreateGpuDevice(nullptr));
    if ( !std3DBabylonTest_pDevice )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    char aProgram[] = "Jones3D-replay", aRender[] = "--render", aGpu[] = "gpu", aFramesOut[] = "--frames-out",
        aEvery[] = "--every", aOne[] = "1";
    char aDir[128];
    std::snprintf(aDir, sizeof(aDir), "%s", kReplayFramesDir);
    char* argv[] = { aProgram, aRender, aGpu, aFramesOut, aDir, aEvery, aOne, nullptr };
    int argc = 7;
    StdRenderBabylonDeviceType type = StdRenderBabylonDeviceType::Null;
    StdRenderReplayBabylonFrameOutput output;
    const char* pError = nullptr;
    TEST_ASSERT_TRUE(stdRenderBabylon_TakeRenderOption(&argc, argv, &type));
    TEST_ASSERT_TRUE(type == StdRenderBabylonDeviceType::Gpu);
    TEST_ASSERT_TRUE(stdRenderReplayBabylon_TakeFrameOptions(&argc, argv, true, &output, &pError));
    TEST_ASSERT_NULL(pError);
    TEST_ASSERT_TRUE(stdRenderReplayBabylon_BeginFrames(&output));

    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_RunWithFrameCallback(kReplayFramesCapturePath, nullptr,
        stdRenderReplayBabylon_CaptureFrame, &output));
    TEST_ASSERT_EQUAL_size_t(1u, output.frames);
    TEST_ASSERT_EQUAL_size_t(1u, output.images);

    const std::vector<char> bmp = ReadFile((std::string(kReplayFramesDir) + "/frame_00000.bmp").c_str());
    TEST_ASSERT_TRUE(bmp.size() > 54u);
    TEST_ASSERT_EQUAL_INT('B', bmp[0]);
    TEST_ASSERT_EQUAL_INT('M', bmp[1]);
    const auto readU16 = [&bmp](size_t at)
    {
        return (uint32_t)(uint8_t)bmp[at] | (uint32_t)(uint8_t)bmp[at + 1u] << 8;
    };
    const auto readU32 = [&bmp](size_t at)
    {
        return (uint32_t)(uint8_t)bmp[at] | (uint32_t)(uint8_t)bmp[at + 1u] << 8
            | (uint32_t)(uint8_t)bmp[at + 2u] << 16 | (uint32_t)(uint8_t)bmp[at + 3u] << 24;
    };
    TEST_ASSERT_EQUAL_UINT32(54u, readU32(10u));
    TEST_ASSERT_EQUAL_UINT32(40u, readU32(14u));
    TEST_ASSERT_EQUAL_UINT32(64u, readU32(18u));
    TEST_ASSERT_EQUAL_UINT32(64u, readU32(22u));
    TEST_ASSERT_EQUAL_UINT32(1u, readU16(26u));
    TEST_ASSERT_EQUAL_UINT32(24u, readU16(28u));
    TEST_ASSERT_EQUAL_UINT32(64u * 3u * 64u, readU32(34u));

    // BMP rows are bottom up, so the pixel at (1, 1) from the top is in row 62. It is red: BGR.
    const size_t at = 54u + (size_t)(64u - 1u - 1u) * 64u * 3u + 1u * 3u;
    TEST_ASSERT_TRUE(at + 3u <= bmp.size());
    TEST_ASSERT_EQUAL_INT(0, (uint8_t)bmp[at + 0u]);
    TEST_ASSERT_EQUAL_INT(0, (uint8_t)bmp[at + 1u]);
    TEST_ASSERT_EQUAL_INT(255, (uint8_t)bmp[at + 2u]);
}

TEST_GROUP_RUNNER(std3DBabylonGpu)
{
    RUN_TEST_CASE(std3DBabylonGpu, TexturesReadBackByteExactPerFormat);
    RUN_TEST_CASE(std3DBabylonGpu, TheWindowedFramePresentsTheBackBuffer);
    RUN_TEST_CASE(std3DBabylonGpu, AVertexColoredTriangleInterpolatesItsColors);
    RUN_TEST_CASE(std3DBabylonGpu, TexturedQuadsSampleEveryTextureFormat);
    RUN_TEST_CASE(std3DBabylonGpu, TheAlphaTestDiscardsAlphaNotAboveTheReference);
    RUN_TEST_CASE(std3DBabylonGpu, DrawsBlendWithSourceAlpha);
    RUN_TEST_CASE(std3DBabylonGpu, TheZTestIsLessOrEqualAndZWritesFollowTheState);
    RUN_TEST_CASE(std3DBabylonGpu, FogIsLinearInWBetweenStartAndEnd);
    RUN_TEST_CASE(std3DBabylonGpu, LinesAndPointsLightTheirPixels);
    RUN_TEST_CASE(std3DBabylonGpu, TexturingIsPerspectiveCorrect);
    RUN_TEST_CASE(std3DBabylonGpu, ReplayWritesFrameImages);
}

// The replay suite of stdRenderReplayTest.c, a C file.
extern "C" void TEST_stdRenderReplay_GROUP_RUNNER(void);

// The display suites (stdDisplayBabylonTest.cpp).
void TEST_stdDisplayBabylon_GROUP_RUNNER(void);
void TEST_stdDisplayBabylonGpu_GROUP_RUNNER(void);

static void runAllTests(void)
{
    RUN_TEST_GROUP(std3DBabylon);
    RUN_TEST_GROUP(stdRenderBabylonOption);
    RUN_TEST_GROUP(stdRenderReplayBabylonOption);
    RUN_TEST_GROUP(std3DBabylonGpu);
    RUN_TEST_GROUP(stdDisplayBabylon);
    RUN_TEST_GROUP(stdDisplayBabylonGpu);
    UseNullDevice();
    TEST_stdRenderReplay_GROUP_RUNNER();
    DropDevice();
}

int main(int argc, char** argv)
{
    std::unique_ptr<babylon::files::FileSystem> pFileSystem = babylon::files::createSdlFileSystem();
    std::unique_ptr<babylon::time::Clock> pClock = babylon::time::createSdlClock();
    babylon::time::NullClock virtualClock;
    babylon::platform::NullPlatform platform;
    babylon::cvars::Registry cvars;
    if ( !stdBabylonCvars_RegisterGame(cvars) )
    {
        fprintf(stderr, "ERROR: std3DBabylonTests: could not register the game cvars!\n");
        return 1;
    }

    cvars.finishStartup();
    std3DBabylonTest_context = StdBabylonContext{ pClock.get(), &virtualClock, pFileSystem.get(), &platform, &platform,
        nullptr, &cvars, nullptr };
    stdBabylon_SetContext(&std3DBabylonTest_context);
    int exitCode = UnityMain(argc, const_cast<const char**>(argv), runAllTests);
    stdBabylon_SetContext(nullptr);
    DropDevice();
    return exitCode;
}
