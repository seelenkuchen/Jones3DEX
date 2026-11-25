// The std3DBabylonGpu sharpening, anti-aliasing, bloom, depth of field and water tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

TEST(std3DBabylonGpu, SharpeningSteepensAStepEdgeAndLeavesFlatAreas)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // A vertical step between mid-grey levels (an edge at 0 and 1 is clamped by CAS and would
    // not move), one pixel per texel.
    std::vector<uint32_t> aTexels(64u * 64u);
    for ( uint32_t y = 0; y < 64u; ++y )
    {
        for ( uint32_t x = 0; x < 64u; ++x )
        {
            const uint32_t value = x < 32u ? 64u : 192u;
            aTexels[(size_t)y * 64u + x] = Texel(stdColor_cfARGB8888, value, value, value,
                MaxOf(8u));
        }
    }
    static tSystemTexture texture;
    MakeTexture(&texture, stdColor_cfARGB8888, 64u, 64u, aTexels);

    auto drawScene = []
    {
        DrawQuad(texture.pCachedTexture, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f,
            0xFFFFFFFFu);
    };

    auto redChannel = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    // Off is the plain tone-mapped present, byte for byte.
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    const int darkOff  = redChannel(off, 31u, 32u);
    const int lightOff = redChannel(off, 32u, 32u);
    TEST_ASSERT_INT_WITHIN(1, 64, darkOff);
    TEST_ASSERT_INT_WITHIN(1, 192, lightOff);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    RenderScene(drawScene);
    const std::vector<std::byte> zero = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(off.data(), zero.data(), off.size());

    // On: the pixels beside the edge move away from the mean, and the far flat areas stay put.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.8f));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(redChannel(on, 31u, 32u) < darkOff);
    TEST_ASSERT_TRUE(redChannel(on, 32u, 32u) > lightOff);
    for ( uint32_t y = 4u; y < 60u; ++y )
    {
        for ( uint32_t x = 4u; x < 60u; ++x )
        {
            if ( x == 31u || x == 32u )
            {
                continue;
            }
            TEST_ASSERT_INT_WITHIN(1, redChannel(off, x, y), redChannel(on, x, y));
        }
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    std3D_ClearSystemTexture(&texture);
}

TEST(std3DBabylonGpu, SharpeningLeavesTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.8f));

    // A uniform grey scene behind a one-pixel green HUD line: the scene may be sharpened, the HUD
    // is composited after it and keeps its exact colour and width.
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, 0.5f, 0xFF404040u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    });

    std3DBabylonGpuTest_pixels = CapturePresented(32u, 16u);
    AssertPixel(16u, 8u, 0, 255, 0, 255);
    AssertPixel(15u, 8u, 64, 64, 64, 255);
    AssertPixel(17u, 8u, 64, 64, 64, 255);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
}

TEST(std3DBabylonGpu, SmaaBlendsAHardDiagonalEdgeAndLeavesFarPixels)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // A 64 x 64 image with a hard 45-degree edge: white where x >= y, black elsewhere. Drawn 1:1,
    // the scene has an exact black/white diagonal before anti-aliasing.
    std::vector<uint32_t> aTexels(64u * 64u);
    for ( uint32_t y = 0; y < 64u; ++y )
    {
        for ( uint32_t x = 0; x < 64u; ++x )
        {
            const uint32_t value = x >= y ? MaxOf(8u) : 0u;
            aTexels[(size_t)y * 64u + x] = Texel(stdColor_cfARGB8888, value, value, value, MaxOf(8u));
        }
    }
    static tSystemTexture texture;
    MakeTexture(&texture, stdColor_cfARGB8888, 64u, 64u, aTexels);

    auto drawScene = []
    {
        DrawQuad(texture.pCachedTexture, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f,
            0xFFFFFFFFu);
    };

    auto redChannel = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    // Off: the hard edge keeps the two colours apart.
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_INT(255, redChannel(off, 55u, 5u));
    TEST_ASSERT_EQUAL_INT(0, redChannel(off, 5u, 55u));
    TEST_ASSERT_EQUAL_INT(255, redChannel(off, 40u, 20u));
    TEST_ASSERT_EQUAL_INT(0, redChannel(off, 20u, 40u));
    // Nothing is blended with the option off: the edge keeps the two colours apart.
    size_t numOffBlended = 0;
    for ( uint32_t y = 4u; y < 60u; ++y )
    {
        for ( uint32_t x = 4u; x < 60u; ++x )
        {
            const int value = redChannel(off, x, y);
            if ( value > 16 && value < 239 )
            {
                ++numOffBlended;
            }
        }
    }
    TEST_ASSERT_EQUAL_size_t(0u, numOffBlended);

    // On: pixels along the diagonal carry intermediate values, the far sides stay untouched.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(true));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_INT(255, redChannel(on, 55u, 5u));
    TEST_ASSERT_EQUAL_INT(0, redChannel(on, 5u, 55u));

    size_t numBlended = 0;
    for ( uint32_t y = 4u; y < 60u; ++y )
    {
        for ( uint32_t x = 4u; x < 60u; ++x )
        {
            const int value = redChannel(on, x, y);
            if ( value > 16 && value < 239 )
            {
                ++numBlended;
            }
        }
    }
    TEST_ASSERT_TRUE(numBlended > 0u);

    std3D_ClearSystemTexture(&texture);
}

TEST(std3DBabylonGpu, SmaaLeavesTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(true));

    // A uniform scene behind a one-pixel green HUD line: anti-aliasing must not blur the HUD into
    // its neighbours, and the line stays the exact HUD colour.
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, 0.5f, 0xFF000000u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    });

    std3DBabylonGpuTest_pixels = CapturePresented(32u, 16u);
    AssertPixel(16u, 8u, 0, 255, 0, 255, 0);
    AssertPixel(15u, 8u, 0, 0, 0, 255, 0);
    AssertPixel(17u, 8u, 0, 0, 0, 255, 0);
}

TEST(std3DBabylonGpu, BloomSpreadsABrightSpotAndLeavesFarBlack)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // An opaque black background with a small white spot at the centre.
    auto drawScene = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.9f, 0xFF000000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 28.0f, 28.0f, 36.0f, 36.0f, 0.5f, 0xFFFFFFFFu);
    };

    auto redChannel = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_INT(255, redChannel(off, 32u, 32u));
    TEST_ASSERT_EQUAL_INT(0, redChannel(off, 39u, 32u));
    TEST_ASSERT_EQUAL_INT(0, redChannel(off, 60u, 60u));

    // On: the neighbourhood of the spot picks up a falloff, the far black stays black.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.15f));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    const int near   = redChannel(on, 39u, 32u);
    const int farther = redChannel(on, 43u, 32u);
    const int far    = redChannel(on, 60u, 60u);
    std::printf("M9.12 bloom falloff: spot 255, 3 px away %d/255, 7 px away %d/255, far corner %d/255\n",
        near, farther, far);
    TEST_ASSERT_TRUE(near > 0);
    TEST_ASSERT_TRUE(near >= farther);
    TEST_ASSERT_TRUE(far <= 1);
    TEST_ASSERT_EQUAL_INT(255, redChannel(on, 32u, 32u));

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
}

TEST(std3DBabylonGpu, BloomLeavesASubThresholdImageUnchanged)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // 0x40 grey is luma ~0.25, below the soft knee's 0.6 - 0.3 start, so nothing contributes.
    auto drawScene = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF404040u);
    };

    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.15f));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    TEST_ASSERT_EQUAL_size_t(off.size(), on.size());
    int maxDiff = 0;
    for ( size_t i = 0; i < off.size(); ++i )
    {
        maxDiff = std::max(maxDiff, std::abs((int)off[i] - (int)on[i]));
    }
    std::printf("M9.12 bloom sub-threshold grey: max per-channel difference %d/255\n", maxDiff);
    TEST_ASSERT_TRUE(maxDiff <= 1);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
}

TEST(std3DBabylonGpu, BloomLeavesTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }
    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.15f));

    // A bright scene behind a one-pixel green HUD line: the scene may bloom, the HUD is composited
    // after the tone map and keeps its exact colour.
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, 0.5f, 0xFFFFFFFFu);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    });

    std3DBabylonGpuTest_pixels = CapturePresented(32u, 16u);
    AssertPixel(16u, 8u, 0, 255, 0, 255, 0);
    AssertPixel(15u, 8u, 255, 255, 255, 255, 0);
    AssertPixel(17u, 8u, 255, 255, 255, 255, 0);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
}

TEST(std3DBabylonGpu, DepthOfFieldBlursTheFarOnlyInGameplayAndBothInCutscenes)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // Only depth of field changes the image: no bloom, anti-aliasing or sharpening.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 0.25f, 100.0f));

    // A black far background with a white spot at the centre, and a black near quad on the left
    // with its own white spot. Linear eye depths 99 and 0.5, with the focus at 5. The gentler
    // cutscene ramp needs the near spot well inside the dead zone and the far spot close to the
    // far plane to keep spreading; the near sample sits 5 px from its spot and the far one 6 px.
    const float nearPlane = 0.25f;
    const float farPlane  = 100.0f;
    const float zNear = SceneDepthForEyeDepth(0.5f, nearPlane, farPlane);
    const float zFar  = SceneDepthForEyeDepth(99.0f, nearPlane, farPlane);
    const auto drawScene = [zNear, zFar]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, zFar, 0xFF000000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 30.0f, 30.0f, 34.0f, 34.0f, zFar, 0xFFFFFFFFu);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 24.0f, 64.0f, zNear, 0xFF000000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 8.0f, 30.0f, 12.0f, 34.0f, zNear, 0xFFFFFFFFu);
    };
    const auto redChannel = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    // No focus use: the stage is skipped and both spots stay crisp.
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_INT(0, redChannel(off, 17u, 32u));
    TEST_ASSERT_EQUAL_INT(0, redChannel(off, 40u, 32u));

    // Gameplay: the far spot spreads, the near one stays sharp.
    std3D_SetFocus(5.0f, false, 0.0f);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, true, 1.0f));
    RenderScene(drawScene);
    const std::vector<std::byte> gameplay = CapturePresented(64u, 64u);
    const int gameplayNear = redChannel(gameplay, 17u, 32u);
    const int gameplayFar  = redChannel(gameplay, 40u, 32u);
    std::printf("M9.14 depth of field gameplay: 5 px from the near spot %d/255, from the far spot %d/255\n",
        gameplayNear, gameplayFar);
    TEST_ASSERT_TRUE(gameplayNear <= 1);
    TEST_ASSERT_TRUE(gameplayFar > 0);

    // Cutscene: both spots spread away from the focus.
    std3D_SetFocus(5.0f, true, 0.0f);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(true, false, 1.0f));
    RenderScene(drawScene);
    const std::vector<std::byte> cutscene = CapturePresented(64u, 64u);
    const int cutsceneNear = redChannel(cutscene, 17u, 32u);
    const int cutsceneFar  = redChannel(cutscene, 40u, 32u);
    std::printf("M9.14 depth of field cutscene: 5 px from the near spot %d/255, from the far spot %d/255\n",
        cutsceneNear, cutsceneFar);
    TEST_ASSERT_TRUE(cutsceneNear > 0);
    TEST_ASSERT_TRUE(cutsceneFar > 0);

    // A surface just inside the gameplay sharp zone must stay sharp. With the focus at 5 the blur
    // starts at max(6 * 5, 1.5) = 30 and ramps over max(24 * 5, 6) = 120, so an eye depth of 29.5
    // has a circle of confusion of 0. The old shader read the scene depth as 0..1 instead of the
    // game's 0.1..1.0, which linearised this surface (sz 0.994609) to about 31.7, past the 30
    // start, so it would have blurred. Everything is at the same depth, so with the correct
    // mapping the depth-of-field stage leaves the whole frame byte-identical to the stage off.
    const float sharpZ = SceneDepthForEyeDepth(29.5f, nearPlane, farPlane);
    const auto drawSharpScene = [sharpZ]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, sharpZ, 0xFF000000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 30.0f, 30.0f, 34.0f, 34.0f, sharpZ, 0xFFFFFFFFu);
    };

    std3D_SetFocus(5.0f, false, 0.0f);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    RenderScene(drawSharpScene);
    const std::vector<std::byte> sharpOff = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(redChannel(sharpOff, 32u, 32u) > 0);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, true, 1.0f));
    RenderScene(drawSharpScene);
    const std::vector<std::byte> sharpGameplay = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(sharpOff.data(), sharpGameplay.data(), sharpOff.size());
}

TEST(std3DBabylonGpu, DepthOfFieldOffIsByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 1.0f, 100.0f));

    const float zFar = 0.99f;
    const auto drawScene = [zFar]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, zFar, 0xFF000000u);
        DrawQuad(nullptr, (Std3DRenderState)0, 30.0f, 30.0f, 34.0f, 34.0f, zFar, 0xFFFFFFFFu);
    };

    std3D_SetFocus(5.0f, false, 0.0f);
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    // Both on: the far spot is blurred.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(true, true, 1.0f));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(off != on);

    // Both off again: the stage is skipped, so the image is byte-identical to the first one.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    RenderScene(drawScene);
    const std::vector<std::byte> offAgain = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(off.size(), offAgain.size());
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());
}

TEST(std3DBabylonGpu, DepthOfFieldLeavesTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, true, 2.0f));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 1.0f, 100.0f));
    std3D_SetFocus(5.0f, false, 0.0f);

    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, 0.99f, 0xFF000000u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    });

    std3DBabylonGpuTest_pixels = CapturePresented(32u, 16u);
    AssertPixel(16u, 8u, 0, 255, 0, 255, 0);
    AssertPixel(15u, 8u, 0, 0, 0, 255, 0);
    AssertPixel(17u, 8u, 0, 0, 0, 255, 0);
}


TEST(std3DBabylonGpu, SurfaceTagsAreWrittenToTheNormalTargetAlpha)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));

    // Three opaque surfaces side by side: ordinary, sky and water. Only the normal target's alpha
    // differs; the color is the same red for all three.
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 16.0f, 64.0f, 0.5f, 0xFFFF0000u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 16.0f, 0.0f, 48.0f, 64.0f, 0.5f, 0xFFFF0000u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_WATER, 48.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFFFF0000u);
    });

    const std::vector<std::byte> aNormals = std3DBabylonTest_pDevice->readTexture(
        stdRenderBabylon_GetNormalTarget(), 0);
    TEST_ASSERT_EQUAL_size_t(64u * 64u * 4u, aNormals.size());
    const bool b10 = stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2;

    // Alpha is the tag: 1 ordinary, 2/3 sky, 1/3 water. R10G10B10A2 stores 3, 2 and 1 in its two
    // alpha bits; Rgba8 stores 255, 170 and 85, which are the same fractions exactly.
    const std::array<int, 3> aExpected = b10
        ? std::array<int, 3>{ 3, 2, 1 }
        : std::array<int, 3>{ 255, 170, 85 };
    const std::array<int, 4> ordinary = NormalTexel(aNormals, 8u, 32u);
    const std::array<int, 4> sky      = NormalTexel(aNormals, 32u, 32u);
    const std::array<int, 4> water    = NormalTexel(aNormals, 56u, 32u);
    TEST_ASSERT_EQUAL_INT(aExpected[0], ordinary[3]);
    TEST_ASSERT_EQUAL_INT(aExpected[1], sky[3]);
    TEST_ASSERT_EQUAL_INT(aExpected[2], water[3]);

    // The normal's red, green and blue are the ordinary constant normal in every case.
    const int half = b10 ? 511 : 127;
    const int full = b10 ? 1023 : 255;
    for ( const std::array<int, 4>* pTexel : { &ordinary, &sky, &water } )
    {
        TEST_ASSERT_INT_WITHIN(1, half, (*pTexel)[0]);
        TEST_ASSERT_INT_WITHIN(1, half, (*pTexel)[1]);
        TEST_ASSERT_EQUAL_INT(full, (*pTexel)[2]);
    }

    // The color target's alpha (presented) is opaque and the color is the red that was drawn, so
    // only the normal target's alpha changed.
    AssertPixel(8u, 32u, 255, 0, 0, 255, 0);
    AssertPixel(32u, 32u, 255, 0, 0, 255, 0);
    AssertPixel(56u, 32u, 255, 0, 0, 255, 0);
}

TEST(std3DBabylonGpu, TranslucentWaterWritesItsTagWithoutChangingTheImage)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));

    // A translucent water quad over an opaque ordinary one on the left, and the same translucent
    // quad without the water tag on the right. Only the normal target's alpha may differ.
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 64.0f, 0.7f, 0xFF804020u);
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 64.0f, 64.0f, 0.7f, 0xFF804020u);
        DrawQuad(nullptr, (Std3DRenderState)(STD3D_RS_ZWRITE_DISABLED | STD3D_RS_SURFACE_WATER),
            0.0f, 0.0f, 32.0f, 64.0f, 0.5f, 0x8000C0FFu);
        DrawQuad(nullptr, STD3D_RS_ZWRITE_DISABLED,
            32.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0x8000C0FFu);
    });

    // The translucent water draw did not change the colour blend: both halves are the same.
    const size_t left  = ((size_t)32u * 64u + 8u) * 4u;
    const size_t right = ((size_t)32u * 64u + 56u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)std3DBabylonGpuTest_pixels[left + 0u], (int)std3DBabylonGpuTest_pixels[right + 0u]);
    TEST_ASSERT_EQUAL_INT((int)std3DBabylonGpuTest_pixels[left + 1u], (int)std3DBabylonGpuTest_pixels[right + 1u]);
    TEST_ASSERT_EQUAL_INT((int)std3DBabylonGpuTest_pixels[left + 2u], (int)std3DBabylonGpuTest_pixels[right + 2u]);
    TEST_ASSERT_EQUAL_INT((int)std3DBabylonGpuTest_pixels[left + 3u], (int)std3DBabylonGpuTest_pixels[right + 3u]);

    // The alpha is the tag: water on the left, ordinary on the right; the colour is the normal that
    // was behind both quads, unchanged by the alpha-only tag write.
    const std::vector<std::byte> aNormals = std3DBabylonTest_pDevice->readTexture(
        stdRenderBabylon_GetNormalTarget(), 0);
    TEST_ASSERT_EQUAL_size_t(64u * 64u * 4u, aNormals.size());
    const bool b10 = stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2;
    const std::array<int, 4> water    = NormalTexel(aNormals, 8u, 32u);
    const std::array<int, 4> ordinary = NormalTexel(aNormals, 56u, 32u);
    TEST_ASSERT_EQUAL_INT(b10 ? 1 : 85, water[3]);
    TEST_ASSERT_EQUAL_INT(b10 ? 3 : 255, ordinary[3]);
    for ( int i = 0; i < 3; ++i )
    {
        TEST_ASSERT_EQUAL_INT(ordinary[i], water[i]);
    }
}

TEST(std3DBabylonGpu, WaterRipplesOffLeavesTheImageByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetWaterRipples());

    const auto drawScene = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.9f, 0xFF204080u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_WATER, 8.0f, 8.0f, 56.0f, 56.0f, 0.5f, 0xFF3080C0u);
    };

    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    RenderScene(drawScene);
    const std::vector<std::byte> offAgain = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(off.size(), offAgain.size());
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());

    // On, but no water in the scene: every pixel is copied through unchanged.
    // Nearer than the water in the scene before, so the depth test lets it replace every pixel.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(true));
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.3f, 0xFF204080u);
    });
    const std::vector<std::byte> onNoWater = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    RenderScene([]
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.3f, 0xFF204080u);
    });
    const std::vector<std::byte> offNoWater = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(offNoWater.size(), onNoWater.size());
    TEST_ASSERT_EQUAL_MEMORY(offNoWater.data(), onNoWater.data(), onNoWater.size());
}

TEST(std3DBabylonGpu, WaterRipplesChangeOnlyWaterPixelsAndFollowGameTime)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetWaterRipples());

    // The left half is water in vertical stripes, so an offset sample picks a different colour; the
    // right half is ordinary geometry and must stay byte-identical.
    const auto drawScene = []
    {
        for ( uint32_t stripe = 0; stripe < 4u; ++stripe )
        {
            const uint32_t color = (stripe & 1u) ? 0xFFC0C0C0u : 0xFF404040u;
            DrawQuad(nullptr, STD3D_RS_SURFACE_WATER, (float)(stripe * 8u), 0.0f,
                (float)(stripe * 8u + 8u), 64.0f, 0.5f, color);
        }
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF204080u);
    };

    std3D_SetFrameTime(0.0f);
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    std3D_SetFrameTime(0.05f);
    RenderScene(drawScene);
    const std::vector<std::byte> moved = CapturePresented(64u, 64u);

    // No game time passes: the phase is frozen, so the image is exactly the same.
    RenderScene(drawScene);
    const std::vector<std::byte> frozen = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(moved.data(), frozen.data(), moved.size());

    // Only water pixels may change, and animation must change some of them.
    size_t numWaterChanged = 0;
    size_t numOrdinaryChanged = 0;
    for ( uint32_t y = 0; y < 64u; ++y )
    {
        for ( uint32_t x = 0; x < 64u; ++x )
        {
            const size_t at = ((size_t)y * 64u + x) * 4u;
            if ( memcmp(&on[at], &moved[at], 4u) != 0 )
            {
                if ( x < 32u )
                {
                    ++numWaterChanged;
                }
                else
                {
                    ++numOrdinaryChanged;
                }
            }
        }
    }
    std::printf("M9.17 water ripples: %zu water pixels changed, %zu ordinary pixels changed\n",
        numWaterChanged, numOrdinaryChanged);
    TEST_ASSERT_TRUE(numWaterChanged > 0u);
    TEST_ASSERT_EQUAL_size_t(0u, numOrdinaryChanged);

    // Off vs on: the water changes and the ordinary half is byte-identical.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    size_t numWaterAgainstOff = 0;
    size_t numOrdinaryAgainstOff = 0;
    for ( uint32_t y = 0; y < 64u; ++y )
    {
        for ( uint32_t x = 0; x < 64u; ++x )
        {
            const size_t at = ((size_t)y * 64u + x) * 4u;
            if ( memcmp(&off[at], &on[at], 4u) != 0 )
            {
                if ( x < 32u )
                {
                    ++numWaterAgainstOff;
                }
                else
                {
                    ++numOrdinaryAgainstOff;
                }
            }
        }
    }
    TEST_ASSERT_TRUE(numWaterAgainstOff > 0u);
    TEST_ASSERT_EQUAL_size_t(0u, numOrdinaryAgainstOff);
}

TEST(std3DBabylonGpu, WaterRipplesLeaveTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));

    const auto drawScene = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_WATER, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF3080C0u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 28.0f, 0.0f, 30.0f, 64.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(true));
    std3D_SetFrameTime(0.05f);
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    // The HUD strip is the same colour on both paths; the water around it differs.
    const size_t hud = ((size_t)32u * 64u + 29u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[hud + 0u], (int)on[hud + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[hud + 1u], (int)on[hud + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[hud + 2u], (int)on[hud + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[hud + 3u], (int)on[hud + 3u]);
}
