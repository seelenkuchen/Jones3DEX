// The std3DBabylonGpu device, draw and present tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

namespace
{
    const char* const kReplayFramesCapturePath = "std3DBabylonTest_ReplayFrames.rcap";
    const char* const kReplayFramesDir         = "std3DBabylonTest_ReplayFrames";

    void RemoveReplayFrames()
    {
        std::remove(kReplayFramesCapturePath);
        std::error_code error;
        std::filesystem::remove_all(kReplayFramesDir, error);
    }
}

namespace
{
    void AssertClear(uint32_t x, uint32_t y)
    {
        AssertPixel(x, y, 0, 0, 0, 0, 0);
    }

    // A channel of bits bits as an 8-bit unorm value, as the GPU converts it; 255 without bits.
    int Unorm8(uint32_t value, uint32_t bits)
    {
        return bits ? (int)std::lround(value * 255.0 / MaxOf(bits)) : 255;
    }
}

namespace
{
    // The constant normal (0, 0, 1) of std3d.frag.hlsl encoded as n * 0.5 + 0.5, ordinary tag 1 (x and y
    // are a half, which rounds either way), or the clear 0.
    void AssertNormal(const std::vector<std::byte>& aBytes, uint32_t x, uint32_t y, bool bDrawn)
    {
        const bool b10   = stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2;
        const int maxRgb = b10 ? 1023 : 255;
        const int maxA   = b10 ? 3 : 255;
        const std::array<int, 4> texel = NormalTexel(aBytes, x, y);
        char aMessage[128];
        std::snprintf(aMessage, sizeof(aMessage), "normal (%u, %u) is (%d, %d, %d, %d)", x, y, texel[0], texel[1], texel[2], texel[3]);
        if ( !bDrawn )
        {
            for ( int i = 0; i < 4; ++i )
            {
                TEST_ASSERT_EQUAL_INT_MESSAGE(0, texel[i], aMessage);
            }
            return;
        }
        TEST_ASSERT_TRUE_MESSAGE(texel[0] == maxRgb / 2 || texel[0] == maxRgb / 2 + 1, aMessage);
        TEST_ASSERT_TRUE_MESSAGE(texel[1] == maxRgb / 2 || texel[1] == maxRgb / 2 + 1, aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(maxRgb, texel[2], aMessage);
        TEST_ASSERT_EQUAL_INT_MESSAGE(maxA, texel[3], aMessage);
    }
}

TEST_GROUP(std3DBabylonGpu);

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
    TEST_ASSERT_EQUAL_size_t(640u * 480u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        pixels.size());
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
    // 1/4 on the right one, point filtered and clamped. The mip filter is None so the test reads
    // the perspective-correct texel itself rather than a generated, averaged level.
    TEST_ASSERT_EQUAL_INT(0, std3D_SetMipmapFilter(STD3D_MIPMAPFILTER_NONE));
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
        aEvery[] = "--every", aOne[] = "1", aNormals[] = "--normals";
    char aDir[128];
    std::snprintf(aDir, sizeof(aDir), "%s", kReplayFramesDir);
    char* argv[] = { aProgram, aRender, aGpu, aFramesOut, aDir, aEvery, aOne, aNormals, nullptr };
    int argc = 8;
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

    // --normals: the normal target next to it, the same size. The classic quad wrote the constant
    // normal (0, 0, 1), encoded (0.5, 0.5, 1): blue 255, green and red a half (127 or 128).
    const std::vector<char> normals = ReadFile((std::string(kReplayFramesDir) + "/normals_00000.bmp").c_str());
    TEST_ASSERT_EQUAL_size_t(bmp.size(), normals.size());
    TEST_ASSERT_EQUAL_MEMORY(bmp.data(), normals.data(), 54u);
    TEST_ASSERT_EQUAL_INT(255, (uint8_t)normals[at + 0u]);
    TEST_ASSERT_INT_WITHIN(1, 127, (uint8_t)normals[at + 1u]);
    TEST_ASSERT_INT_WITHIN(1, 127, (uint8_t)normals[at + 2u]);
    TEST_ASSERT_TRUE((uint8_t)normals[at + 1u] >= 127u && (uint8_t)normals[at + 2u] >= 127u);
}



TEST(std3DBabylonGpu, TheNormalTargetHoldsTheConstantNormalWhereZIsWritten)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    RenderScene([]
    {
        // With Z write: the normal. Without: none. Lines and points with Z write: the normal.
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 16.0f, 16.0f, 0.5f, 0xFFFF0000u);
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED, 32.0f, 0.0f, 48.0f, 16.0f, 0.5f, 0x80FF0000u);
        // A translucent quad without Z write over one with it leaves its normal.
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 16.0f, 48.0f, 0.5f, 0xFF00FF00u);
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED, 0.0f, 32.0f, 16.0f, 48.0f, 0.25f, 0x800000FFu);
        // The wireframe state keeps the Z write of the state before it.
        StdVertexTL aPoint[1] = { Vertex(40.0f, 40.0f, 0.5f, 1.0f, 0xFFFFFFFFu) };
        std3D_SetRenderState((Std3DRenderState)0);
        std3D_SetWireframeRenderState();
        std3D_DrawPointList(aPoint, 1u);
    });

    const std::vector<std::byte> aNormals = std3DBabylonTest_pDevice->readTexture(stdRenderBabylon_GetNormalTarget(), 0);
    TEST_ASSERT_EQUAL_size_t(64u * 64u * 4u, aNormals.size());
    AssertNormal(aNormals, 8u, 8u, true);
    AssertNormal(aNormals, 40u, 8u, false);
    AssertNormal(aNormals, 8u, 40u, true);
    AssertNormal(aNormals, 40u, 40u, true);
    AssertNormal(aNormals, 24u, 24u, false);
    AssertNormal(aNormals, 63u, 63u, false);

    // The color is what it was without the normal target.
    AssertPixel(8u, 8u, 255, 0, 0, 255);
    AssertPixel(40u, 40u, 255, 255, 255, 255);
    AssertClear(24u, 24u);
}

TEST(std3DBabylonGpu, ALitQuadInterpolatesItsNormalsAndKeepsTheClassicColor)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // A quad over the target, split along (0, 0) to (64, 64), with w 1 (no perspective) and opaque
    // colors from red to gray (opaque, so the second scene below, which loads the first one's color,
    // replaces it). Its normals in rdroid's camera space (x right, y forward, z up): top left
    // (0, -1, 0), facing the camera; top right and bottom right (1, 0, 0); bottom left (0, 0, 1), up.
    // In the normal target's space (x right, y up, z toward the viewer): (0, 0, 1), (1, 0, 0), (0, 1, 0).
    const StdVertexTL aTl[4] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFF0000u), Vertex(64.0f, 0.0f, 0.5f, 1.0f, 0xFF00FF00u),
        Vertex(64.0f, 64.0f, 0.5f, 1.0f, 0xFF0000FFu), Vertex(0.0f, 64.0f, 0.5f, 1.0f, 0xFF808080u) };
    static StdVertexLit aLit[4];
    aLit[0] = LitVertex(aTl[0], -1.0f, 10.0f, 1.0f, 0.0f, -1.0f, 0.0f);
    aLit[1] = LitVertex(aTl[1], 1.0f, 10.0f, 1.0f, 1.0f, 0.0f, 0.0f);
    aLit[2] = LitVertex(aTl[2], 1.0f, 10.0f, -1.0f, 1.0f, 0.0f, 0.0f);
    aLit[3] = LitVertex(aTl[3], -1.0f, 10.0f, -1.0f, 0.0f, 0.0f, 1.0f);

    // With a light list in the light buffer (M6.9c); these vertices do not ask for per-pixel lights (M10.2).
    const StdLight aLights[2] = {
        { 0.0f, 5.0f, 0.0f, 8.0f, 1.0f, 1.0f, 1.0f, 0.0f, 4.0f, 0.2f, 0.1f, STD_LIGHT_ENABLED },
        { 2.0f, 9.0f, 1.0f, 3.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.5f, 0.2f, 0.1f, STD_LIGHT_ENABLED },
    };
    std3D_SetLights(aLights, 2u);
    RenderScene([]
    {
        uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, aLit, 4u, aIndices, 6u);
    });
    const std::vector<std::byte> aLitColor = std3DBabylonGpuTest_pixels;
    const std::vector<std::byte> aNormals  = std3DBabylonTest_pDevice->readTexture(stdRenderBabylon_GetNormalTarget(), 0);
    TEST_ASSERT_EQUAL_size_t(64u * 64u * 4u, aNormals.size());

    // Pixel (x, y) is at screen (x, y) (Direct3D 9 pixel centers). Above the diagonal it lies in the
    // triangle (0, 1, 2) and its normal is (x, 0, 64 - x) / 64; below it in (0, 2, 3), with
    // (x, y - x, 64 - y) / 64. Written normalized as n * 0.5 + 0.5.
    const bool b10   = stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2;
    const int maxRgb = b10 ? 1023 : 255;
    const int maxA   = b10 ? 3 : 255;
    const uint32_t aPixels[][2] = { { 48u, 16u }, { 60u, 4u }, { 16u, 48u }, { 4u, 60u }, { 20u, 30u }, { 1u, 1u }, { 62u, 62u } };
    for ( const auto& pixel : aPixels )
    {
        const double x = pixel[0];
        const double y = pixel[1];
        double n[3]    = { x, 0.0, 64.0 - x };
        if ( y > x )
        {
            n[1] = y - x;
            n[2] = 64.0 - y;
        }
        const double length = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        const std::array<int, 4> texel = NormalTexel(aNormals, pixel[0], pixel[1]);
        char aMessage[128];
        std::snprintf(aMessage, sizeof(aMessage), "normal (%u, %u) is (%d, %d, %d, %d)", pixel[0], pixel[1], texel[0], texel[1], texel[2], texel[3]);
        for ( int i = 0; i < 3; ++i )
        {
            const int expected = (int)std::lround((n[i] / length * 0.5 + 0.5) * maxRgb);
            TEST_ASSERT_INT_WITHIN_MESSAGE(b10 ? 3 : 1, expected, texel[i], aMessage);
        }
        TEST_ASSERT_EQUAL_INT_MESSAGE(maxA, texel[3], aMessage);
    }

    // The same quad drawn the classic way: the same color, byte for byte, and the constant normal.
    RenderScene([&aTl]
    {
        StdVertexTL aVerts[4] = { aTl[0], aTl[1], aTl[2], aTl[3] };
        uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
        std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 4u, aIndices, 6u);
    });
    AssertBytes(std3DBabylonGpuTest_pixels, aLitColor);
    const std::vector<std::byte> aClassicNormals = std3DBabylonTest_pDevice->readTexture(stdRenderBabylon_GetNormalTarget(), 0);
    AssertNormal(aClassicNormals, 48u, 16u, true);
    AssertNormal(aClassicNormals, 16u, 48u, true);
}

TEST(std3DBabylonGpu, PerPixelLightsAddNdotLOnlyWhereTheVertexAsks)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // Three black lit quads, left to right, each with a constant view position and normal:
    // - left: facing the light at view (0, 5, 0), 5 units from view (0, 10, 0), so the ramp is
    //   (8 - 5) / (8 - 4) = 0.75 and the fragment is 0.75 white;
    // - middle: the same position but 15 units away, beyond minRadius 8, so dark;
    // - right: the same position as the left one but its normal faces away, so dark.
    // The specular blue byte asks for the lights, as rdCache sets it in per-pixel mode.
    const auto makeLitQuad = [](float x0, float x1, float vy, float ny, bool bAddLights)
    {
        const StdVertexTL aTl[4] = { Vertex(x0, 0.0f, 0.5f, 1.0f, 0xFF000000u), Vertex(x1, 0.0f, 0.5f, 1.0f, 0xFF000000u),
            Vertex(x1, 64.0f, 0.5f, 1.0f, 0xFF000000u), Vertex(x0, 64.0f, 0.5f, 1.0f, 0xFF000000u) };
        StdVertexLit aLit[4];
        for ( size_t i = 0; i < 4u; ++i )
        {
            aLit[i] = LitVertex(aTl[i], 0.0f, vy, 0.0f, 0.0f, ny, 0.0f);
            aLit[i].tl.specular = bAddLights ? 0xFFFFFFFFu : 0xFF000000u;
        }

        return std::array<StdVertexLit, 4>{ aLit[0], aLit[1], aLit[2], aLit[3] };
    };

    const auto aLeft   = makeLitQuad(0.0f, 20.0f, 10.0f, -1.0f, true);
    const auto aMiddle = makeLitQuad(22.0f, 42.0f, 20.0f, -1.0f, true);
    const auto aRight  = makeLitQuad(44.0f, 64.0f, 10.0f, 1.0f, true);
    const auto aClassic = makeLitQuad(0.0f, 20.0f, 10.0f, -1.0f, false);

    // rdroid camera space: (0, 5, 0) is five units in front of the left and right quads.
    const StdLight light = { 0.0f, 5.0f, 0.0f, 8.0f, 1.0f, 1.0f, 1.0f, 0.0f, 4.0f, 0.2f, 0.1f, STD_LIGHT_ENABLED };
    std3D_SetLights(&light, 1u);
    const uint16_t aIndices[6] = { 0u, 1u, 2u, 0u, 2u, 3u };
    RenderScene([&]
    {
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aLeft.data()), 4u, const_cast<uint16_t*>(aIndices), 6u);
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aMiddle.data()), 4u, const_cast<uint16_t*>(aIndices), 6u);
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aRight.data()), 4u, const_cast<uint16_t*>(aIndices), 6u);
    });

    AssertPixel(10u, 32u, 191, 191, 191, 255, 2);
    AssertPixel(32u, 32u, 0, 0, 0, 255, 0);
    AssertPixel(54u, 32u, 0, 0, 0, 255, 0);

    // The same left quad without the flag stays black: the classic vertex adds no light.
    RenderScene([&]
    {
        std3D_DrawRenderListLit(nullptr, (Std3DRenderState)0, const_cast<StdVertexLit*>(aClassic.data()), 4u, const_cast<uint16_t*>(aIndices), 6u);
    });
    AssertPixel(10u, 32u, 0, 0, 0, 255, 0);
}

TEST(std3DBabylonGpu, APostPassChangesThePresentedImageNotTheBackBuffer)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postInvertFrag()));

    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 16.0f, 16.0f, 0.5f, 0xFFFF4020u);
    });

    // The back buffer, and what stdDisplay and the replay read of it, is what the scene drew.
    AssertPixel(8u, 8u, 255, 64, 32, 255, 0);
    AssertClear(40u, 40u);
    std::vector<std::byte> aBackBuffer(64u * 64u * 4u);
    TEST_ASSERT_TRUE(stdRenderBabylon_ReadBackBuffer(aBackBuffer.data(), nullptr, nullptr));
    AssertBytes(std3DBabylonGpuTest_pixels, aBackBuffer);

    // The presented image is inverted, alpha kept. The chain's intermediate is the scene's
    // Rgba16F format, so the check goes through the tone map's Bgra8 presented image.
    TEST_ASSERT_FALSE(stdRenderBabylon_GetPresentSource() == stdRenderBabylon_GetColorTarget());
    std3DBabylonGpuTest_pixels = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(64u * 64u * 4u, std3DBabylonGpuTest_pixels.size());
    AssertPixel(8u, 8u, 0, 191, 223, 255, 0);
    AssertPixel(40u, 40u, 255, 255, 255, 0, 0);

    // Inverted twice, the presented image is the back buffer again.
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postInvertFrag()));
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 16.0f, 16.0f, 0.5f, 0xFFFF4020u);
    });
    const std::vector<std::byte> twice = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(std3DBabylonGpuTest_pixels.size(), twice.size());
    for ( size_t i = 0; i < twice.size(); ++i )
    {
        TEST_ASSERT_INT_WITHIN(1, (int)std3DBabylonGpuTest_pixels[i], (int)twice[i]);
    }
}

TEST(std3DBabylonGpu, APostPassReadsTheNormalTarget)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postNormalFrag()));

    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 16.0f, 16.0f, 0.5f, 0xFFFF0000u);
    });

    // The normal's red, green and blue with the scene's alpha: (0.5, 0.5, 1) where drawn, 0 elsewhere.
    std3DBabylonGpuTest_pixels = CapturePresented(64u, 64u);
    AssertPixel(8u, 8u, 128, 128, 255, 255);
    AssertClear(40u, 40u);
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
    RUN_TEST_CASE(std3DBabylonGpu, TheNormalTargetHoldsTheConstantNormalWhereZIsWritten);
    RUN_TEST_CASE(std3DBabylonGpu, ALitQuadInterpolatesItsNormalsAndKeepsTheClassicColor);
    RUN_TEST_CASE(std3DBabylonGpu, PerPixelLightsAddNdotLOnlyWhereTheVertexAsks);
    RUN_TEST_CASE(std3DBabylonGpu, APostPassChangesThePresentedImageNotTheBackBuffer);
    RUN_TEST_CASE(std3DBabylonGpu, APostPassReadsTheNormalTarget);
    RUN_TEST_CASE(std3DBabylonGpu, SurfaceTagsAreWrittenToTheNormalTargetAlpha);
    RUN_TEST_CASE(std3DBabylonGpu, TranslucentWaterWritesItsTagWithoutChangingTheImage);
    RUN_TEST_CASE(std3DBabylonGpu, WaterRipplesOffLeavesTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, WaterRipplesChangeOnlyWaterPixelsAndFollowGameTime);
    RUN_TEST_CASE(std3DBabylonGpu, WaterRipplesLeaveTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoOffLeavesTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoDarkensACreaseAndLeavesOpenFlatAreas);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoNearObjectLeavesTheBackgroundRingAlone);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoLeavesSkyAndTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoLevelsDarkenMonotonically);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoProjectionIsAspectCorrect);
    RUN_TEST_CASE(std3DBabylonGpu, SsaoLeavesTheFirstPersonOverlayAlone);
    RUN_TEST_CASE(std3DBabylonGpu, ContactShadowsOffLeavesTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, ContactShadowPassRunsBetweenWaterAndSsao);
    RUN_TEST_CASE(std3DBabylonGpu, ContactShadowsDarkenTheFloorBesideABox);
    RUN_TEST_CASE(std3DBabylonGpu, ContactShadowLightDirectionMatchesTheCamera);
    RUN_TEST_CASE(std3DBabylonGpu, ContactShadowsOptionDefaultsAndSetting);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowCasterTLInversionMatchesTheLitViewPosition);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowCasterClassicTLInversionMatchesTheViewPosition);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowCasterListTakesOnlyMarkedSceneTrianglesAndResets);
    RUN_TEST_CASE(std3DBabylonGpu, DrawsThingShadowsIsZeroBeforeOpenAndFollowsTheOption);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowsOptionDefaultsAndSetting);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowMapRunsBeforeThePostChainAndReceiveBetweenContactAndSsao);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowsOffLeavesTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, ShadowsDarkenTheFloorBelowTheCasterAndLeaveFarSkyAndHud);
    RUN_TEST_CASE(std3DBabylonGpu, MotionBlurOffIsByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, MotionBlurStaticCameraLeavesTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, MotionBlurRotationSmearsHorizontallyOnly);
    RUN_TEST_CASE(std3DBabylonGpu, MotionBlurCutPauseAndTeleportSkipTheStage);
    RUN_TEST_CASE(std3DBabylonGpu, MotionBlurLeavesTheFirstPersonOverlayAlone);
    RUN_TEST_CASE(std3DBabylonGpu, MotionBlurLeavesTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, LightShaftsOffLeavesTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, LightShaftsUseTheBrightSkyCentroidAndLeaveOtherScenesAlone);
    RUN_TEST_CASE(std3DBabylonGpu, LightShaftsAddMuchLessToSkyPixelsThanToGeometry);
    RUN_TEST_CASE(std3DBabylonGpu, LightShaftsLeaveTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, CameraEffectsOffLeaveTheImageByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, VignetteDarkensTheCornersAndLeavesTheCentre);
    RUN_TEST_CASE(std3DBabylonGpu, FilmGrainIsDeterministicAndFollowsTheFrameCounter);
    RUN_TEST_CASE(std3DBabylonGpu, LensFlareAddsLightAlongTheGhostLine);
    RUN_TEST_CASE(std3DBabylonGpu, LensFlareAddsNothingWithoutASky);
    RUN_TEST_CASE(std3DBabylonGpu, CameraEffectsLeaveTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, CameraEffectsRecordScratchThenTargetAndSurviveReopen);
    RUN_TEST_CASE(std3DBabylonGpu, TheWindowedFramePresentsThroughThePostChain);
    RUN_TEST_CASE(std3DBabylonGpu, TheHdrTargetAndToneMapMatchTheOldPath);
    RUN_TEST_CASE(std3DBabylonGpu, ARenderScaleRendersTheSceneIntoTheDisplaySize);
    RUN_TEST_CASE(std3DBabylonGpu, AHudLayerLineStaysOnePixelAtRenderScaleHalf);
    RUN_TEST_CASE(std3DBabylonGpu, TheHudLayerCompositeMatchesTheSingleScenePassAtScaleOne);
    RUN_TEST_CASE(std3DBabylonGpu, SharpeningSteepensAStepEdgeAndLeavesFlatAreas);
    RUN_TEST_CASE(std3DBabylonGpu, SharpeningLeavesTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, SmaaBlendsAHardDiagonalEdgeAndLeavesFarPixels);
    RUN_TEST_CASE(std3DBabylonGpu, SmaaLeavesTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, BloomSpreadsABrightSpotAndLeavesFarBlack);
    RUN_TEST_CASE(std3DBabylonGpu, BloomLeavesASubThresholdImageUnchanged);
    RUN_TEST_CASE(std3DBabylonGpu, BloomLeavesTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, DepthOfFieldBlursTheFarOnlyInGameplayAndBothInCutscenes);
    RUN_TEST_CASE(std3DBabylonGpu, DepthOfFieldOffIsByteIdentical);
    RUN_TEST_CASE(std3DBabylonGpu, DepthOfFieldLeavesTheHudLayerPixelExact);
    RUN_TEST_CASE(std3DBabylonGpu, ToneMapOperatorsAndFloatIntermediates);
    RUN_TEST_CASE(std3DBabylonGpu, ToneMapBrightnessIsIdentityAtOneAndGammaAfterTheOperator);
}
