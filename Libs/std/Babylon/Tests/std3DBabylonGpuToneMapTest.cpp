// The std3DBabylonGpu tone mapping and colour grading tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

TEST(std3DBabylonGpu, ToneMapOperatorsAndFloatIntermediates)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    const bool bHdr = stdRenderBabylon_GetColorFormat() == render::TextureFormat::Rgba16F;

    auto draw = [](uint32_t color)
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, color);
    };
    auto red = [](const std::vector<std::byte>& pixels)
    {
        return (int)pixels[((size_t)32u * 64u + 32u) * 4u + 2u];
    };
    auto capture = [&red](uint32_t color)
    {
        RenderScene([color]
        {
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, color);
        });
        return red(CapturePresented(64u, 64u));
    };

    // Operator 0 with exposure 1 is the identity clamp: mid grey stays mid grey.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u });
    const int clampHalf = capture(0xFF808080u);
    TEST_ASSERT_INT_WITHIN(1, 128, clampHalf);

    // Operator 1 (Neutral) leaves the midtone nearly unchanged.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 1u });
    const int neutralHalf = capture(0xFF808080u);
    TEST_ASSERT_INT_WITHIN(2, clampHalf, neutralHalf);

    // Operator 2 (ACES fitted) is monotonic over the scene's range.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 2u });
    const int acesHalf  = capture(0xFF808080u);
    const int acesThree = capture(0xFFC0C0C0u);
    const int acesOne   = capture(0xFFFFFFFFu);
    TEST_ASSERT_TRUE(acesHalf < acesThree);
    TEST_ASSERT_TRUE(acesThree < acesOne);

    // Exposure is applied before the curve: 0.25 at 2.0 equals 0.5 at 1.0 under Neutral.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 2.0f, 1u });
    const int doubleExposure = capture(0xFF404040u);
    TEST_ASSERT_INT_WITHIN(2, neutralHalf, doubleExposure);

    // A post pass that doubles its input writes 2.0; the chain keeps it because its target is the
    // scene's Rgba16F format. Only an HDR intermediate preserves that value.
    TEST_ASSERT_TRUE(stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postDoubleFrag()));
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 1u });
    RenderScene([&] { draw(0xFFFFFFFFu); });
    TEST_ASSERT_EQUAL_size_t(64u * 64u * render::bytesPerTexel(stdRenderBabylon_GetColorFormat()),
        std3DBabylonTest_pDevice->readTexture(stdRenderBabylon_GetPresentSource(), 0).size());
    const int compressed = red(CapturePresented(64u, 64u));
    TEST_ASSERT_TRUE(compressed < 250);
    TEST_ASSERT_TRUE(compressed > neutralHalf);

    // Half exposure on the doubled 2.0 is 1.0: 255 through an HDR intermediate, 128 after an
    // intermediate that clamped 2.0 to 1.0.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 0.5f, 0u });
    RenderScene([&] { draw(0xFFFFFFFFu); });
    const int halfExposure = red(CapturePresented(64u, 64u));
    TEST_ASSERT_INT_WITHIN(2, bHdr ? 255 : 128, halfExposure);
}

TEST(std3DBabylonGpu, ToneMapBrightnessIsIdentityAtOneAndGammaAfterTheOperator)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    auto center = [](const std::vector<std::byte>& pixels)
    {
        return (int)pixels[((size_t)32u * 64u + 32u) * 4u + 2u];
    };
    auto capture = [&center](uint32_t color)
    {
        RenderScene([color]
        {
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, color);
        });
        return center(CapturePresented(64u, 64u));
    };

    // Brightness 1.0 skips the pow and presents the operator's output unchanged.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f });
    const int identity = capture(0xFF404040u);

    // 1.5 brightens the 0.25 scene value to pow(0.25, 1/1.5) within one 8-bit step.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.5f });
    const int brightened = capture(0xFF404040u);
    const int expected = (int)std::lround(255.0 * std::pow(64.0 / 255.0, 1.0 / 1.5));
    TEST_ASSERT_INT_WITHIN(1, expected, brightened);
    TEST_ASSERT_TRUE(brightened > identity);
}

TEST(std3DBabylonGpu, ColorGradingNeutralIsTheIdentity)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    auto capture = [](uint32_t color)
    {
        RenderScene([color]
        {
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, color);
        });
        return CapturePresented(64u, 64u);
    };
    auto channel = [](const std::vector<std::byte>& pixels, size_t index)
    {
        return (int)pixels[((size_t)32u * 64u + 32u) * 4u + index];
    };

    // Preset 0 skips the lookup: mid grey stays exactly mid grey.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f, 0u });
    const std::vector<std::byte> neutralGrey = capture(0xFF808080u);
    TEST_ASSERT_INT_WITHIN(1, 128, channel(neutralGrey, 2u));
    TEST_ASSERT_INT_WITHIN(1, 128, channel(neutralGrey, 1u));
    TEST_ASSERT_INT_WITHIN(1, 128, channel(neutralGrey, 0u));

    // A colored draw is unchanged by Neutral and changed by Vivid.
    const std::vector<std::byte> neutralColor = capture(0xFFFF4020u);
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f, 4u });
    const std::vector<std::byte> vividColor = capture(0xFFFF4020u);
    int maxDifference = 0;
    for ( size_t i = 0; i < neutralColor.size() && i < vividColor.size(); ++i )
    {
        maxDifference = std::max(maxDifference,
            std::abs((int)neutralColor[i] - (int)vividColor[i]));
    }
    TEST_ASSERT_TRUE(maxDifference >= 2);

    // Back at Neutral the presented image is byte-identical to the first Neutral capture.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f, 0u });
    const std::vector<std::byte> neutralAgain = capture(0xFFFF4020u);
    TEST_ASSERT_EQUAL_size_t(neutralColor.size(), neutralAgain.size());
    for ( size_t i = 0; i < neutralColor.size(); ++i )
    {
        TEST_ASSERT_EQUAL_INT((int)neutralColor[i], (int)neutralAgain[i]);
    }

    // Six known colors across the frame come back unchanged: Neutral is the identity, not just a
    // grey-preserving curve.
    const uint32_t aColors[6] = { 0xFF000000u, 0xFF204060u, 0xFF804020u, 0xFF808080u,
        0xFFC0A080u, 0xFFFFFFFFu };
    RenderScene([&aColors]
    {
        for ( size_t cur = 0; cur < 6u; ++cur )
        {
            DrawQuad(nullptr, (Std3DRenderState)0, (float)cur * 10.0f, 0.0f,
                (float)(cur + 1u) * 10.0f, 64.0f, 0.5f, aColors[cur]);
        }
    });
    const std::vector<std::byte> stripes = CapturePresented(64u, 64u);
    for ( size_t cur = 0; cur < 6u; ++cur )
    {
        const size_t at = ((size_t)32u * 64u + cur * 10u + 5u) * 4u;
        const int expected[3] = { (int)((aColors[cur] >> 16u) & 0xFFu),
            (int)((aColors[cur] >> 8u) & 0xFFu), (int)(aColors[cur] & 0xFFu) };
        const int actual[3] = { (int)stripes[at + 2u], (int)stripes[at + 1u],
            (int)stripes[at] };
        for ( size_t channelIndex = 0; channelIndex < 3u; ++channelIndex )
        {
            TEST_ASSERT_INT_WITHIN(1, expected[channelIndex], actual[channelIndex]);
        }
    }
}

TEST(std3DBabylonGpu, ColorGradingPresetsMapGreyAndKeepBlackAndWhite)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    auto capture = [](uint32_t color)
    {
        RenderScene([color]
        {
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, color);
        });
        return CapturePresented(64u, 64u);
    };
    auto channel = [](const std::vector<std::byte>& pixels, size_t index)
    {
        return (int)pixels[((size_t)32u * 64u + 32u) * 4u + index];
    };

    int aMidGrey[6][3] = {};
    for ( uint32_t preset = 0; preset <= 5u; ++preset )
    {
        stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f, preset });
        const std::vector<std::byte> grey = capture(0xFF808080u);
        aMidGrey[preset][0] = channel(grey, 2u);
        aMidGrey[preset][1] = channel(grey, 1u);
        aMidGrey[preset][2] = channel(grey, 0u);

        // Black stays black and white stays near white under every preset.
        const std::vector<std::byte> black = capture(0xFF000000u);
        for ( size_t cur = 0; cur < 3u; ++cur )
        {
            TEST_ASSERT_INT_WITHIN(2, 0, channel(black, cur));
        }

        const std::vector<std::byte> white = capture(0xFFFFFFFFu);
        for ( size_t cur = 0; cur < 3u; ++cur )
        {
            TEST_ASSERT_INT_WITHIN(15, 255, channel(white, cur));
        }

        for ( size_t cur = 0; cur < 3u; ++cur )
        {
            TEST_ASSERT_INT_WITHIN(20, 128, aMidGrey[preset][cur]);
        }
    }

    // The mid-grey direction of each preset.
    TEST_ASSERT_INT_WITHIN(1, 128, aMidGrey[0][0]);
    TEST_ASSERT_INT_WITHIN(1, 128, aMidGrey[0][1]);
    TEST_ASSERT_INT_WITHIN(1, 128, aMidGrey[0][2]);
    TEST_ASSERT_TRUE(aMidGrey[1][0] > aMidGrey[1][1]);
    TEST_ASSERT_TRUE(aMidGrey[1][1] > aMidGrey[1][2]);
    TEST_ASSERT_TRUE(aMidGrey[2][2] > aMidGrey[2][0]);
    TEST_ASSERT_TRUE(aMidGrey[3][1] > aMidGrey[3][0]);
    TEST_ASSERT_TRUE(aMidGrey[3][1] > aMidGrey[3][2]);
    TEST_ASSERT_INT_WITHIN(3, 128, aMidGrey[4][0]);
    TEST_ASSERT_INT_WITHIN(3, 128, aMidGrey[4][1]);
    TEST_ASSERT_INT_WITHIN(3, 128, aMidGrey[4][2]);
    TEST_ASSERT_INT_WITHIN(3, 128, aMidGrey[5][0]);
    TEST_ASSERT_INT_WITHIN(3, 128, aMidGrey[5][1]);
    TEST_ASSERT_INT_WITHIN(3, 128, aMidGrey[5][2]);
}

TEST(std3DBabylonGpu, ColorGradingLutInterpolatesBetweenSlices)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // Vivid's blue output rises quickly with blue, so a color between two slices must read between
    // the colors at those slices rather than at either one.
    stdRenderBabylon_SetToneMap(StdRenderBabylonToneMap{ 1.0f, 0u, 1.0f, 4u });
    auto blue = [](uint32_t color)
    {
        RenderScene([color]
        {
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, color);
        });
        const std::vector<std::byte> pixels = CapturePresented(64u, 64u);
        return (int)pixels[((size_t)32u * 64u + 32u) * 4u];
    };

    // Blue 165 and 173 sit just inside slices 20 and 21; blue 169 lies between them.
    const int slice20 = blue(0xFF8080A5u);
    const int between = blue(0xFF8080A9u);
    const int slice21 = blue(0xFF8080ADu);
    TEST_ASSERT_TRUE(slice20 < between);
    TEST_ASSERT_TRUE(between < slice21);
    TEST_ASSERT_TRUE(between - slice20 >= 3);
    TEST_ASSERT_TRUE(slice21 - between >= 3);
}

TEST(std3DBabylonGpu, TheWindowedFramePresentsThroughThePostChain)
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

    // Two windowed frames through a two-pass chain: every command accepted, no warning.
    std::vector<std::string> aWarnings;
    bool bOpened    = false;
    bool bAdded     = false;
    bool bSubmitted = true;
    {
        babylon::log::CaptureSink sink;
        bOpened = stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()) && stdRenderBabylon_SetBackBufferSize(32u, 24u, nullptr);
        bAdded  = bOpened && stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postInvertFrag())
            && stdRenderBabylon_AddPostPass(std3DBabylonTest::shaders::postNormalFrag());
        for ( int frame = 0; bAdded && frame < 2; ++frame )
        {
            render::ClearColor red{ 1.0f, 0.0f, 0.0f, 1.0f };
            bSubmitted = stdRenderBabylon_ClearBackBuffer(red) && stdRenderBabylon_PresentFrame() && bSubmitted;
        }

        for ( const babylon::log::CapturedRecord& record : sink.records() )
        {
            aWarnings.push_back(record.message);
        }
    }

    TEST_ASSERT_TRUE(bOpened);
    TEST_ASSERT_TRUE(bAdded);
    TEST_ASSERT_TRUE(bSubmitted);
    for ( const std::string& warning : aWarnings )
    {
        TEST_FAIL_MESSAGE(warning.c_str());
    }
}

TEST(std3DBabylonGpu, TheHdrTargetAndToneMapMatchTheOldPath)
{
    // A scene with interpolated, opaque and blended color, drawn the same way in both paths.
    auto drawScene = []
    {
        StdVertexTL aVerts[3] = { Vertex(0.0f, 0.0f, 0.5f, 1.0f, 0xFFFF0000u),
            Vertex(64.0f, 0.0f, 0.5f, 1.0f, 0xFF00FF00u),
            Vertex(0.0f, 64.0f, 0.5f, 1.0f, 0xFF0000FFu) };
        uint16_t aIndices[3] = { 0u, 1u, 2u };
        std3D_DrawRenderList(nullptr, (Std3DRenderState)0, aVerts, 3u, aIndices, 3u);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 32.0f, 64.0f, 0.5f, 0xFFFF4020u);
        // One blended quad over the opaque one: blue at 128 / 255.
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED, 0.0f, 32.0f, 32.0f, 64.0f, 0.25f, 0x800000FFu);
    };

    // Renders the scene once through a fresh device and returns the presented image; pbHdr tells
    // whether the frame used Rgba16F.
    auto renderPath = [&drawScene](bool bLegacy, bool* pbHdr)
    {
        stdRenderBabylon_ForceLegacyPresentationForTest(bLegacy);
        if ( !OpenGpuStd3D(64u, 64u) )
        {
            return std::vector<std::byte>{};
        }
        *pbHdr = stdRenderBabylon_GetColorFormat() == render::TextureFormat::Rgba16F;
        RenderScene(drawScene);
        std::vector<std::byte> presented = CapturePresented(64u, 64u);
        CloseStd3D();
        stdRenderBabylon_Close();
        DropDevice();
        return presented;
    };

    bool bHdrHdr = false;
    bool bLegacyHdr = false;
    const std::vector<std::byte> hdr = renderPath(false, &bHdrHdr);
    if ( hdr.empty() )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    const std::vector<std::byte> legacy = renderPath(true, &bLegacyHdr);

    TEST_ASSERT_FALSE(legacy.empty());
    // The default path uses Rgba16F where the device can (the fallback still has to match), and the
    // forced path never does.
    TEST_ASSERT_FALSE(bLegacyHdr);

    TEST_ASSERT_EQUAL_size_t(hdr.size(), legacy.size());
    int maxDiff = 0;
    size_t numAboveOne = 0;
    for ( size_t i = 0; i < hdr.size(); ++i )
    {
        const int diff = std::abs((int)hdr[i] - (int)legacy[i]);
        maxDiff = std::max(maxDiff, diff);
        if ( diff > 1 )
        {
            ++numAboveOne;
        }
    }
    std::printf("M9.3 image identity (%s vs forced Bgra8): %zu bytes, max per-channel difference %d/255, %zu bytes differ by more than 1/255\n",
        bHdrHdr ? "Rgba16F" : "Bgra8 fallback", hdr.size(), maxDiff, numAboveOne);
    TEST_ASSERT_TRUE(maxDiff <= 1);
}

TEST(std3DBabylonGpu, ARenderScaleRendersTheSceneIntoTheDisplaySize)
{
    // A full-screen quad in the display's screen pixels: the scene fills the scaled target, and the
    // read-back image is always the display size.
    auto drawScene = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, 0.5f, 0xFFFF0000u);
    };

    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // 200%: the scene target is 64x32 and resolves to the 32x16 read-back image.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(2.0f));
    RenderScene(drawScene);
    AssertPixel(16u, 8u, 255, 0, 0, 255);

    // 50%: the scene target is 16x8; the read-back image is still 32x16.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(0.5f));
    RenderScene(drawScene);
    AssertPixel(16u, 8u, 255, 0, 0, 255);
}

TEST(std3DBabylonGpu, AHudLayerLineStaysOnePixelAtRenderScaleHalf)
{
    // The HUD keeps drawing in the game's display pixels. At 50% the scene target is 16x8 but the
    // display-sized HUD layer is 32x16, so a 1-pixel line stays 1 pixel in the presented image.
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(0.5f));

    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, 0.5f, 0xFFFF0000u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    });

    std3DBabylonGpuTest_pixels = CapturePresented(32u, 16u);
    AssertPixel(16u, 8u, 0, 255, 0, 255);
    AssertPixel(15u, 8u, 255, 0, 0, 255);
    AssertPixel(17u, 8u, 255, 0, 0, 255);
}

TEST(std3DBabylonGpu, TheHudLayerCompositeMatchesTheSingleScenePassAtScaleOne)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_SetRenderScale(1.0f));

    // The same red background and two overlapping half-transparent HUD quads, drawn once in the scene
    // pass (the pre-M9.4b path) and once in the HUD layer.
    auto draw = [](bool bHudLayer)
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFFFF0000u);
        if ( bHudLayer )
        {
            std3D_BeginHudLayer();
        }
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED, 0.0f, 0.0f, 32.0f, 64.0f, 0.25f, 0x800000FFu);
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED, 0.0f, 0.0f, 16.0f, 64.0f, 0.20f, 0x8000FF00u);
        if ( bHudLayer )
        {
            std3D_EndHudLayer();
        }
    };

    RenderScene([&draw] { draw(false); });
    const std::vector<std::byte> singlePass = CapturePresented(64u, 64u);

    RenderScene([&draw] { draw(true); });
    const std::vector<std::byte> hudLayer = CapturePresented(64u, 64u);

    TEST_ASSERT_EQUAL_size_t(singlePass.size(), hudLayer.size());
    // The presented frame is opaque; its alpha channel is a leftover of the scene pass's blending and
    // the composite writes 255 there. Compare the visible color channels.
    int maxDiff = 0;
    size_t numAboveOne = 0;
    for ( size_t i = 0; i < singlePass.size(); ++i )
    {
        if ( i % 4u == 3u )
        {
            continue;
        }
        const int diff = std::abs((int)singlePass[i] - (int)hudLayer[i]);
        maxDiff = std::max(maxDiff, diff);
        if ( diff > 1 )
        {
            ++numAboveOne;
        }
    }
    std::printf("M9.4b HUD identity (HUD layer vs single scene pass): %zu color bytes, max per-channel difference %d/255, %zu bytes differ by more than 1/255\n",
        singlePass.size(), maxDiff, numAboveOne);
    TEST_ASSERT_TRUE(maxDiff <= 1);
}
