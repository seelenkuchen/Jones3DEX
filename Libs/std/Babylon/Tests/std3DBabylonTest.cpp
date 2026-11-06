// Added: Unity Fixture suites of the Babylon column's std3D backend (std3DBabylon.cpp) and its frame
// owner (stdRenderBabylon.cpp), run through the public std3D_ functions and the capture forwarders in
// front of them, on a babylon::render::NullDevice whose recorded command stream and texture contents
// are compared with values worked out here from the DX9 backend's rules. The GPU group repeats the
// texture readback and checks the windowed present on the SDL GPU device; without a GPU it is
// ignored, or fails with BABYLON_REQUIRE_GPU=1. The render capture replay suite (stdRenderReplayTest.c)
// runs here once more, replaying into this backend instead of std3DNull.c.
//
// System textures in the tests are static: a failed assert leaves a test by longjmp, and the
// tear-down's std3D_Shutdown then still walks the cache they may be linked into.
//
// The executable has its own main: it installs the adapter context with the SDL file system and
// clock that the std file and time adapters need, and each test puts its own render device in it.
#include <unity_fixture.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include <babylon/time/Clock.h>
#include <babylon/time/NullClock.h>
#include <babylon/time/SdlClock.h>

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

    // The display at width x height (0 keeps the Null display's default 640 x 480), then std3D.
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
        const char* pValue = std::getenv("BABYLON_REQUIRE_GPU");
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
    DropDevice();
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Startup());
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_Open(0u));

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
    TEST_ASSERT_EQUAL_STRING("Jones3D Null Display (Babylon render, null)", pDevice->deviceDescription);
    TEST_ASSERT_EQUAL_STRING("stdDisplayNull", pDevice->deviceName);

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
    TEST_ASSERT_EQUAL_size_t(total, pDevice->availableMemory * 2u); // the Null display reports half free

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
    std3D_EndScene();
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
    std3D_EndScene();

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
    std3D_EndScene();

    TEST_ASSERT_TRUE(ReadTexture(handle, 0).empty());
}

TEST(std3DBabylon, ScenesRecordAPassWithTheDX9Clears)
{
    OpenStd3D();
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size());

    // The first scene clears the new back buffer (std3DX9.c's viewport clear): black, Z 1.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    TEST_ASSERT_EQUAL_size_t(0u, Null().recorded().size()); // nothing reaches the device before the submit
    std3D_EndScene();
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);

    // Later scenes keep the back buffer, and depth too unless ClearZBuffer came first.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
    std3D_ClearZBuffer();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();
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
    std3D_EndScene();
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
    std3D_EndScene();
    const render::TextureHandle oldColor = stdRenderBabylon_GetColorTarget();
    const render::TextureHandle oldDepth = stdRenderBabylon_GetDepthTarget();

    stdDisplay_SetDefaultResolution(64u, 48u);
    TEST_ASSERT_EQUAL_INT(0, stdDisplay_SetMode(0u, 0, 1u));
    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_EndScene();

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

TEST(std3DBabylon, DrawsAreCheckedAndRecordNothingYet)
{
    OpenStd3D();
    StdVertexTL aVerts[3];
    std::memset(aVerts, 0, sizeof(aVerts));
    uint16_t aIndices[3]    = { 0, 1, 2 };
    uint16_t aBadIndices[3] = { 0, 1, 3 };

    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    std3D_DrawRenderList(nullptr, STD3D_RS_ZWRITE_DISABLED, aVerts, 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aBadIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, nullptr, 3u, aIndices, 3u);
    std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, std3D_g_maxVertices + 1u, aIndices, 3u);
    std3D_DrawLineStrip(aVerts, 3u);
    std3D_DrawLineStrip(nullptr, 2u);
    std3D_DrawPointList(aVerts, 3u);
    std3D_DrawPointList(aVerts, std3D_g_maxVertices + 1u);
    std3D_SetWireframeRenderState();
    std3D_EndScene();

    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
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
    TEST_ASSERT_EQUAL_STRING("Jones3D Null Display (Babylon render, null)", pEnv->aDisplayInfos[0].aDevices[0].deviceDescription);
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
        std3D_EndScene();
    }
    std3D_ClearSystemTexture(&texture);
    CloseStd3D();
    stdDisplayCapture_Close();
    TEST_ASSERT_FALSE(stdRenderCapture_IsOpen());

    // The replay drives the same backend: no error, the same two frames, the same capture again.
    UseNullDevice();
    TEST_ASSERT_EQUAL_INT(0, stdRenderReplay_Run(kCapturePath, kReplayPath));
    TEST_ASSERT_EQUAL_size_t(6u, Null().recorded().size());
    AssertScenePassLoads(0, render::LoadOp::Clear, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(1);
    AssertSubmitted(2);
    AssertScenePassLoads(3, render::LoadOp::Load, render::LoadOp::Clear);
    RecordedAt<render::EndPass>(4);
    AssertSubmitted(5);

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
    RUN_TEST_CASE(std3DBabylon, DrawsAreCheckedAndRecordNothingYet);
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

TEST_GROUP(std3DBabylonGpu);

namespace
{
    // The windowed case's SDL objects, released in the tear-down, which also runs after a failed
    // assert: the device first, then the window, then the platform.
    std::unique_ptr<babylon::platform::Platform> std3DBabylonGpuTest_pPlatform;
    std::unique_ptr<babylon::platform::Window> std3DBabylonGpuTest_pWindow;
}

TEST_SETUP(std3DBabylonGpu)
{
    StdGeneralTest_Startup();
    std3D_ResetGlobals();
    stdDisplay_ResetGlobals();
}

TEST_TEAR_DOWN(std3DBabylonGpu)
{
    CloseStd3D();
    stdRenderBabylon_Close();
    DropDevice();
    std3DBabylonGpuTest_pWindow.reset();
    std3DBabylonGpuTest_pPlatform.reset();
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
    std3D_EndScene();
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

TEST_GROUP_RUNNER(std3DBabylonGpu)
{
    RUN_TEST_CASE(std3DBabylonGpu, TexturesReadBackByteExactPerFormat);
    RUN_TEST_CASE(std3DBabylonGpu, TheWindowedFramePresentsTheBackBuffer);
}

// The replay suite of stdRenderReplayTest.c, a C file.
extern "C" void TEST_stdRenderReplay_GROUP_RUNNER(void);

static void runAllTests(void)
{
    RUN_TEST_GROUP(std3DBabylon);
    RUN_TEST_GROUP(stdRenderBabylonOption);
    RUN_TEST_GROUP(std3DBabylonGpu);
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
