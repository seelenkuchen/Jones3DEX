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
#include "std3DBabylonTestSupport.h"

#include <std/Babylon/stdDisplayBabylonInternal.h>

using namespace std3DBabylonTest;

namespace
{
    bool SameColorInfo(const ColorInfo& a, const ColorInfo& b)
    {
        return std::memcmp(&a, &b, sizeof(ColorInfo)) == 0;
    }

    // Writes one encoded texel of ci into a locked VBuffer.
    void SetTexel(tVBuffer* pVBuffer, const ColorInfo& ci, uint32_t x, uint32_t y, uint32_t value)
    {
        const size_t texelBytes = ci.bpp / 8u;
        uint8_t* pTexel = pVBuffer->pPixels + y * pVBuffer->rasterInfo.rowSize + x * texelBytes;
        for ( size_t i = 0; i < texelBytes; ++i )
        {
            pTexel[i] = (uint8_t)(value >> (8u * i));
        }
    }

    // One little-endian texel of upload bytes.
    uint32_t ReadTexel(const std::vector<std::byte>& data, size_t index, size_t texelBytes)
    {
        uint32_t value = 0;
        for ( size_t i = 0; i < texelBytes; ++i )
        {
            value |= (uint32_t)(uint8_t)data[index * texelBytes + i] << (8u * i);
        }
        return value;
    }

    // The last texture bound by a recorded draw.
    render::BindTexture LastBoundTexture()
    {
        const std::vector<render::RecordedCommand>& recorded = Null().recorded();
        for ( auto it = recorded.rbegin(); it != recorded.rend(); ++it )
        {
            if ( const render::BindTexture* pBind = std::get_if<render::BindTexture>(&*it) )
            {
                return *pBind;
            }
        }

        TEST_FAIL_MESSAGE("no BindTexture recorded");
        return {};
    }

    uint8_t Expand5(uint32_t value)
    {
        return (uint8_t)((value << 3) | (value >> 2));
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
    TEST_ASSERT_TRUE(pDevice->bAnisotropicFilteringSupported);
    TEST_ASSERT_TRUE(pDevice->bMipmapAutoGenSupported);
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

TEST(std3DBabylon, SingleLevelTexturesGetAGeneratedMipChain)
{
    OpenStd3D(); // the default trilinear mip filter generates

    // Four 2x2 blocks, each one colour, so every generated level has an exact expected texel.
    const uint8_t aBlocks[4][3] = {
        { 0, 0, 0 }, { 40, 80, 120 }, { 160, 120, 80 }, { 200, 160, 40 },
    };
    tVBuffer* pVBuffer = NewVBuffer(stdColor_cfARGB8888, 4u, 4u, 0);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pVBuffer));
    for ( uint32_t y = 0; y < 4u; ++y )
    {
        for ( uint32_t x = 0; x < 4u; ++x )
        {
            const uint8_t* pBlock = aBlocks[(y / 2u) * 2u + x / 2u];
            SetTexel(pVBuffer, stdColor_cfARGB8888, x, y,
                stdColor_EncodeRGBA(&stdColor_cfARGB8888, pBlock[0], pBlock[1], pBlock[2], 255));
        }
    }
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pVBuffer));

    static tSystemTexture texture;
    std3D_AllocSystemTexture(&texture, &pVBuffer, 1u, STDCOLOR_FORMAT_RGBA);
    TEST_ASSERT_EQUAL_size_t(1u, std3D_GetMipMapCount(&texture)); // one source level
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGBA);
    TEST_ASSERT_NOT_NULL(texture.pCachedTexture);

    // The generated 2x2 level holds the blocks; 4x4 has three levels.
    const std::vector<std::byte> mip1 = Null().readTexture(CachedHandle(texture), 1);
    TEST_ASSERT_EQUAL_size_t(2u * 2u * 4u, mip1.size());
    for ( size_t i = 0; i < 4u; ++i )
    {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        stdColor_DecodeRGBA(ReadTexel(mip1, i, 4u), &stdColor_cfARGB8888, &r, &g, &b, &a);
        TEST_ASSERT_EQUAL_INT(aBlocks[i][0], r);
        TEST_ASSERT_EQUAL_INT(aBlocks[i][1], g);
        TEST_ASSERT_EQUAL_INT(aBlocks[i][2], b);
        TEST_ASSERT_EQUAL_INT(0xFF, a);
    }

    // The 1x1 level is the average of the four blocks.
    const std::vector<std::byte> mip2 = Null().readTexture(CachedHandle(texture), 2);
    TEST_ASSERT_EQUAL_size_t(4u, mip2.size());
    uint8_t r = 0, g = 0, b = 0, a = 0;
    stdColor_DecodeRGBA(ReadTexel(mip2, 0, 4u), &stdColor_cfARGB8888, &r, &g, &b, &a);
    TEST_ASSERT_EQUAL_INT(100, r);
    TEST_ASSERT_EQUAL_INT(90, g);
    TEST_ASSERT_EQUAL_INT(60, b);
    TEST_ASSERT_EQUAL_INT(0xFF, a);

    // A 4x4 texture has no level 3.
    TEST_ASSERT_TRUE(ReadTexture(CachedHandle(texture), 3).empty());

    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(pVBuffer);
}

TEST(std3DBabylon, GeneratedMipsKeepTheColourKeyColourOut)
{
    OpenStd3D();

    // A colour-keyed texture (1-bit alpha): an opaque red half and a transparent green half.
    // Mask averaging may only take the red, so the green key colour never reaches a visible texel.
    const uint32_t red   = (31u << 10) | (1u << 15); // R5=31, A1=1
    const uint32_t green = (31u << 5);               // G5=31, A1=0
    tVBuffer* pVBuffer = NewVBuffer(stdColor_cfARGB5551, 4u, 4u, 0);
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferLock(pVBuffer));
    for ( uint32_t y = 0; y < 4u; ++y )
    {
        for ( uint32_t x = 0; x < 4u; ++x )
        {
            SetTexel(pVBuffer, stdColor_cfARGB5551, x, y, x < 2u ? red : green);
        }
    }
    TEST_ASSERT_EQUAL_INT(1, stdDisplay_VBufferUnlock(pVBuffer));

    static tSystemTexture texture;
    std3D_AllocSystemTexture(&texture, &pVBuffer, 1u, STDCOLOR_FORMAT_RGBA_1BITALPHA);
    std3D_AddToTextureCache(&texture, STDCOLOR_FORMAT_RGBA_1BITALPHA);
    TEST_ASSERT_NOT_NULL(texture.pCachedTexture);

    // The 2x2 level: left texels stay opaque red, right ones transparent with no green.
    const std::vector<std::byte> mip1 = Null().readTexture(CachedHandle(texture), 1);
    TEST_ASSERT_EQUAL_size_t(2u * 2u * 2u, mip1.size());
    for ( size_t i = 0; i < 4u; ++i )
    {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        stdColor_DecodeRGBA(ReadTexel(mip1, i, 2u), &stdColor_cfARGB5551, &r, &g, &b, &a);
        if ( i % 2u == 0u )
        {
            TEST_ASSERT_EQUAL_INT(0xFF, r);
            TEST_ASSERT_EQUAL_INT(0, g);
            TEST_ASSERT_EQUAL_INT(0, b);
            TEST_ASSERT_EQUAL_INT(0xFF, a);
        }
        else
        {
            TEST_ASSERT_EQUAL_INT(0, a);
            TEST_ASSERT_EQUAL_INT(0, g); // the transparent green key colour didn't bleed
        }
    }

    // The 1x1 level averages only the opaque red texels; the result is still opaque red.
    const std::vector<std::byte> mip2 = Null().readTexture(CachedHandle(texture), 2);
    TEST_ASSERT_EQUAL_size_t(2u, mip2.size());
    uint8_t r = 0, g = 0, b = 0, a = 0;
    stdColor_DecodeRGBA(ReadTexel(mip2, 0, 2u), &stdColor_cfARGB5551, &r, &g, &b, &a);
    TEST_ASSERT_EQUAL_INT(0xFF, r);
    TEST_ASSERT_EQUAL_INT(0, g);
    TEST_ASSERT_EQUAL_INT(0, b);
    TEST_ASSERT_EQUAL_INT(0xFF, a);

    std3D_ClearSystemTexture(&texture);
    stdDisplay_VBufferFree(pVBuffer);
}

TEST(std3DBabylon, AnisotropyAndCrispPixelsReachTheSampler)
{
    OpenStd3D();

    static tSystemTexture texture;
    MakeCachedTexture(&texture, stdColor_cfARGB8888, 5);

    static StdVertexTL aVertices[3] = {
        { 1.0f, 2.0f, 4.0f, 0.5f, 0xFFFFFFFFu, 0u, 0.0f, 0.0f },
        { 3.0f, 2.0f, 4.0f, 0.5f, 0xFFFFFFFFu, 0u, 1.0f, 0.0f },
        { 2.0f, 4.0f, 4.0f, 0.5f, 0xFFFFFFFFu, 0u, 0.5f, 1.0f },
    };
    static uint16_t aIndices[3] = { 0u, 1u, 2u };
    const auto drawWithState = [&](Std3DRenderState state)
    {
        TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
        std3D_DrawRenderList(texture.pCachedTexture, state, aVertices, 3u, aIndices, 3u);
        EndFrame();
        return LastBoundTexture();
    };

    // An anisotropic draw takes the configured level.
    std3DBabylon_SetAnisotropicFilterForTest(8);
    render::BindTexture bind = drawWithState(STD3D_RS_TEXFILTER_ANISOTROPIC);
    std::optional<render::SamplerDesc> desc = Null().samplerDesc(bind.sampler);
    TEST_ASSERT_TRUE(desc.has_value());
    TEST_ASSERT_EQUAL_FLOAT(8.0f, desc->maxAnisotropy);
    TEST_ASSERT_TRUE(desc->minFilter == render::Filter::Linear);
    TEST_ASSERT_TRUE(desc->magFilter == render::Filter::Linear);
    TEST_ASSERT_TRUE(desc->mipFilter == render::Filter::Linear); // trilinear, the default

    // Off falls back to plain linear.
    std3DBabylon_SetAnisotropicFilterForTest(1);
    bind = drawWithState(STD3D_RS_TEXFILTER_ANISOTROPIC);
    desc = Null().samplerDesc(bind.sampler);
    TEST_ASSERT_TRUE(desc.has_value());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, desc->maxAnisotropy);
    TEST_ASSERT_TRUE(desc->minFilter == render::Filter::Linear);

    // Crisp pixels makes min and mag nearest and leaves the mip filter as configured.
    std3DBabylon_SetTexturePixelatedForTest(true);
    bind = drawWithState(STD3D_RS_TEXFILTER_BILINEAR);
    desc = Null().samplerDesc(bind.sampler);
    TEST_ASSERT_TRUE(desc.has_value());
    TEST_ASSERT_TRUE(desc->minFilter == render::Filter::Nearest);
    TEST_ASSERT_TRUE(desc->magFilter == render::Filter::Nearest);
    TEST_ASSERT_TRUE(desc->mipFilter == render::Filter::Linear);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, desc->maxAnisotropy);

    std3DBabylon_SetTexturePixelatedForTest(false);
    std3D_ClearSystemTexture(&texture);
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
    TEST_ASSERT_EQUAL_size_t(320u * 200u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());
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
    TEST_ASSERT_EQUAL_size_t(64u * 48u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());

    TEST_ASSERT_TRUE(ReadTexture(oldColor, 0).empty());
}

TEST(std3DBabylon, ARenderScaleSizesTheSceneTargetsAndKeepsTheDisplaySize)
{
    OpenStd3D(32u, 16u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(2.0f));
    TEST_ASSERT_EQUAL_FLOAT(2.0f, stdRenderBabylon_GetRenderScale());
    // The game's display size is unchanged; only the GPU scene targets grew.
    TEST_ASSERT_EQUAL_UINT32(32u, stdRenderBabylon_GetBackBufferWidth());
    TEST_ASSERT_EQUAL_UINT32(16u, stdRenderBabylon_GetBackBufferHeight());
    TEST_ASSERT_EQUAL_UINT32(64u, stdRenderBabylon_GetRenderWidth());
    TEST_ASSERT_EQUAL_UINT32(32u, stdRenderBabylon_GetRenderHeight());
    TEST_ASSERT_EQUAL_size_t(64u * 32u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());
    TEST_ASSERT_EQUAL_size_t(64u * 32u * render::bytesPerTexel(stdRenderBabylon_GetNormalFormat()),
        Null().readTexture(stdRenderBabylon_GetNormalTarget(), 0).size());

    // The scene fills the scaled target, and a present at the display size succeeds.
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    const render::TextureHandle present = Null().createTexture(
        render::TextureDesc{ 32u, 16u, 1u, render::TextureFormat::Bgra8,
            render::TextureUsage::ColorTarget });
    TEST_ASSERT_FALSE(present.isNull());
    TEST_ASSERT_TRUE(stdRenderBabylon_RenderPresentToTexture(present, 32u, 16u));
    TEST_ASSERT_EQUAL_size_t(32u * 16u * 4u, Null().readTexture(present, 0).size());
    Null().destroyTexture(present);

    // The read-back image has the display size.
    std::vector<std::byte> aPixels(32u * 16u * 4u);
    uint32_t width  = 0;
    uint32_t height = 0;
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(aPixels.data(), &width, &height));
    TEST_ASSERT_EQUAL_UINT32(32u, width);
    TEST_ASSERT_EQUAL_UINT32(16u, height);

    // 50% makes the scene target smaller; the display and read-back sizes stay.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(0.5f));
    TEST_ASSERT_EQUAL_UINT32(32u, stdRenderBabylon_GetBackBufferWidth());
    TEST_ASSERT_EQUAL_UINT32(16u, stdRenderBabylon_GetBackBufferHeight());
    TEST_ASSERT_EQUAL_UINT32(16u, stdRenderBabylon_GetRenderWidth());
    TEST_ASSERT_EQUAL_UINT32(8u, stdRenderBabylon_GetRenderHeight());
    TEST_ASSERT_EQUAL_size_t(16u * 8u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(aPixels.data(), &width, &height));
    TEST_ASSERT_EQUAL_UINT32(32u, width);
    TEST_ASSERT_EQUAL_UINT32(16u, height);
}

TEST(std3DBabylon, RenderScaleOneRoundTripsTheBackBufferBytes)
{
    OpenStd3D(16u, 8u);

    std::vector<std::byte> aExpected(16u * 8u * 4u);
    for ( uint32_t y = 0; y < 8u; ++y )
    {
        for ( uint32_t x = 0; x < 16u; ++x )
        {
            std::byte* pTexel = aExpected.data() + ((size_t)y * 16u + x) * 4u;
            pTexel[0] = (std::byte)((x * 7u + y * 13u) & 0xFFu);
            pTexel[1] = (std::byte)((x * 11u + y * 5u) & 0xFFu);
            pTexel[2] = (std::byte)((x * 3u + y * 17u) & 0xFFu);
            pTexel[3] = std::byte{ 0xFF };
        }
    }

    // At 100% the bytes go into the scene target and come back unchanged: no resampling runs.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_WriteBackBuffer(aExpected.data()));
    std::vector<std::byte> aActual(16u * 8u * 4u);
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(aActual.data(), nullptr, nullptr));
    AssertBytes(aExpected, aActual);
}

TEST(std3DBabylon, ARenderScaleAboveTheDeviceLimitIsClamped)
{
    OpenStd3D(6000u, 16u);
    const uint32_t maxSize = Null().info().limits.maxTextureDimension;

    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(2.0f));
    const uint32_t renderWidth  = stdRenderBabylon_GetRenderWidth();
    const uint32_t renderHeight = stdRenderBabylon_GetRenderHeight();

    TEST_ASSERT_TRUE(renderWidth <= maxSize);
    TEST_ASSERT_TRUE(renderHeight <= maxSize);
    TEST_ASSERT_TRUE(renderWidth < 12000u); // 2.0 * the 6000 pixel display width was clamped
    TEST_ASSERT_EQUAL_UINT32(6000u, stdRenderBabylon_GetBackBufferWidth());
    TEST_ASSERT_EQUAL_UINT32(16u, stdRenderBabylon_GetBackBufferHeight());
    TEST_ASSERT_EQUAL_size_t((size_t)renderWidth * renderHeight
            * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        Null().readTexture(stdRenderBabylon_GetColorTarget(), 0).size());

    // The read-back image stays the display size.
    std::vector<std::byte> aPixels(6000u * 16u * 4u);
    uint32_t width  = 0;
    uint32_t height = 0;
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(aPixels.data(), &width, &height));
    TEST_ASSERT_EQUAL_UINT32(6000u, width);
    TEST_ASSERT_EQUAL_UINT32(16u, height);
}

TEST(std3DBabylon, TheSceneTargetIsBgra8WithoutRgba16F)
{
    UseNullDevice(render::NullDeviceDesc{ { render::TextureFormat::Rgba16F } });
    OpenStd3D();
    TEST_ASSERT_TRUE(stdRenderBabylon_GetColorFormat() == render::TextureFormat::Bgra8);
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    EndFrame();
    TEST_ASSERT_EQUAL_size_t(3u, Null().recorded().size());
    AssertScenePass(0, render::LoadOp::Clear, render::LoadOp::Clear);
}

TEST(std3DBabylon, DepthIsD32FWithoutD24S8)
{
    UseNullDevice(render::NullDeviceDesc{ { render::TextureFormat::D24S8 } });
    OpenStd3D();
    TEST_ASSERT_TRUE(stdRenderBabylon_GetDepthFormat() == render::TextureFormat::D32F);
    TEST_ASSERT_EQUAL_INT(1, std3D_GetCurrentDevice()->hasZBuffer);
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
    RUN_TEST_CASE(std3DBabylon, SingleLevelTexturesGetAGeneratedMipChain);
    RUN_TEST_CASE(std3DBabylon, GeneratedMipsKeepTheColourKeyColourOut);
    RUN_TEST_CASE(std3DBabylon, AnisotropyAndCrispPixelsReachTheSampler);
    RUN_TEST_CASE(std3DBabylon, CacheKeepsTheDX9ListAndMemory);
    RUN_TEST_CASE(std3DBabylon, AFullCachePurgesATextureOfTheSameSizeNotUsedThisFrame);
    RUN_TEST_CASE(std3DBabylon, ATextureClearedInASceneLivesUntilTheSubmit);
    RUN_TEST_CASE(std3DBabylon, ScenesRecordAPassWithTheDX9Clears);
    RUN_TEST_CASE(std3DBabylon, SceneCallsOutOfOrderFailLikeDX9);
    RUN_TEST_CASE(std3DBabylon, ADisplaySizeChangeRecreatesTheBackBuffer);
    RUN_TEST_CASE(std3DBabylon, ARenderScaleSizesTheSceneTargetsAndKeepsTheDisplaySize);
    RUN_TEST_CASE(std3DBabylon, RenderScaleOneRoundTripsTheBackBufferBytes);
    RUN_TEST_CASE(std3DBabylon, ARenderScaleAboveTheDeviceLimitIsClamped);
    RUN_TEST_CASE(std3DBabylon, TheSceneTargetIsBgra8WithoutRgba16F);
    RUN_TEST_CASE(std3DBabylon, DepthIsD32FWithoutD24S8);
    RUN_TEST_CASE(std3DBabylon, BadAndEmptyDrawsRecordNothing);
    RUN_TEST_CASE(std3DBabylon, TwoDrawsRecordTheirStreamExactly);
    RUN_TEST_CASE(std3DBabylon, ShadowCasterStateDrawsLikeThePlainState);
    RUN_TEST_CASE(std3DBabylon, AFrameThatNeedsMoreGrowsTheBuffers);
    RUN_TEST_CASE(std3DBabylon, WireframeDrawsUseTheirPipelinesAndNoTexture);
    RUN_TEST_CASE(std3DBabylon, LitDrawsStreamApartWithTheLitPipeline);
    RUN_TEST_CASE(std3DBabylon, LightsReachTheLightBufferOfTheLitDraws);
    RUN_TEST_CASE(std3DBabylon, TheLightBufferIsReleasedAtClose);
    RUN_TEST_CASE(std3DBabylon, ShaderFogChangesWithTheFogBitOnly);
    RUN_TEST_CASE(std3DBabylon, ProjectionAndFogFollowDX9);
    RUN_TEST_CASE(std3DBabylon, DisplayEnvironmentListsTheRenderDevice);
    RUN_TEST_CASE(std3DBabylon, ACaptureReplaysIntoTheBabylonBackend);
    RUN_TEST_CASE(std3DBabylon, TheNormalTargetComesWithTheBackBufferAndClearsWithDepth);
    RUN_TEST_CASE(std3DBabylon, TheNormalTargetIsRgba8WithoutR10G10B10A2);
    RUN_TEST_CASE(std3DBabylon, AnEmptyPostChainPresentsTheBackBuffer);
    RUN_TEST_CASE(std3DBabylon, APostPassRunsAtEachPresentAndLeavesTheBackBuffer);
    RUN_TEST_CASE(std3DBabylon, PostPassesChainAndReadTheNormalAndDepthTargets);
    RUN_TEST_CASE(std3DBabylon, BadPostPassesAreRefused);
    RUN_TEST_CASE(std3DBabylon, AntiAliasingAddsThreeSmaaPassesBeforeThePresent);
    RUN_TEST_CASE(std3DBabylon, SharpeningAddsACasPassAfterTheToneMapOrSmaa);
    RUN_TEST_CASE(std3DBabylon, BloomAddsAPrefilterBlurAndCompositeBeforeTheToneMap);
    RUN_TEST_CASE(std3DBabylon, ThePresentOverlayRecordsAfterTheScenePass);
    RUN_TEST_CASE(std3DBabylon, TheHudOverlayRecordsBeforeThePresentOverlay);
    RUN_TEST_CASE(std3DBabylon, APresentToTextureIncludesTheOverlay);
    RUN_TEST_CASE(std3DBabylon, APresentToTextureIncludesTheHudOverlay);
    RUN_TEST_CASE(std3DBabylon, ToneMapParametersValidateAndDefault);
    RUN_TEST_CASE(std3DBabylon, FocusSmoothingReachesTheTarget);
    RUN_TEST_CASE(std3DBabylon, CloseClearsThePresentOverlay);
}

// The replay suite of stdRenderReplayTest.c, a C file.
extern "C" void TEST_stdRenderReplay_GROUP_RUNNER(void);

// The display suites (stdDisplayBabylonTest.cpp).
void TEST_stdDisplayBabylon_GROUP_RUNNER(void);
void TEST_stdDisplayBabylonGpu_GROUP_RUNNER(void);

// The flight recorder suite (std3DBabylonFlightRecorderTest.cpp).
void TEST_std3DBabylonFlightRecorder_GROUP_RUNNER(void);

static void runAllTests(void)
{
    RUN_TEST_GROUP(std3DBabylon);
    RUN_TEST_GROUP(stdRenderBabylonOption);
    RUN_TEST_GROUP(stdRenderReplayBabylonOption);
    RUN_TEST_GROUP(std3DBabylonGpu);
    RUN_TEST_GROUP(stdDisplayBabylon);
    RUN_TEST_GROUP(stdDisplayBabylonGpu);
    RUN_TEST_GROUP(std3DBabylonFlightRecorder);
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
    // Added: no display test records a flight recorder stream unless it asks to; the flight
    // recorder suite overrides this for its own cases.
    stdDisplayBabylonInternal::stdDisplay_FlightRecorderSetEnabledForTest(false);
    int exitCode = UnityMain(argc, const_cast<const char**>(argv), runAllTests);
    stdBabylon_SetContext(nullptr);
    DropDevice();
    return exitCode;
}
