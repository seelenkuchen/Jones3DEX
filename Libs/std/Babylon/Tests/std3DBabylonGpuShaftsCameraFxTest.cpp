// The std3DBabylonGpu light shaft and camera effect tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

TEST(std3DBabylonGpu, LightShaftsOffLeavesTheImageByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));

    const auto drawScene = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0xFFFFFFFFu);
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF204060u);
    };

    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    RenderScene(drawScene);
    const std::vector<std::byte> offAgain = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(off.size(), offAgain.size());
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());
    TEST_ASSERT_FALSE(stdRenderBabylon_GetLightShaftCentroidForTest(nullptr, nullptr, nullptr));
}

TEST(std3DBabylonGpu, LightShaftsUseTheBrightSkyCentroidAndLeaveOtherScenesAlone)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(3));
    TEST_ASSERT_EQUAL_INT(0, std3D_SetProjection(1.5f, 1.0f, 100.0f));

    // A bright sky quad on the left half, over a dark lower half. Its luminance-weighted centroid
    // is at about (0.25, 0.25) in image uv.
    const auto drawSky = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 64.0f, 64.0f, 0.95f, 0xFF101010u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0xFFFFFFFFu);
    };
    const auto drawNoSky = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 64.0f, 64.0f, 0.95f, 0xFF101010u);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0xFFFFFFFFu);
    };

    stdRenderBabylon_SetLightShafts(0);
    RenderScene(drawSky);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    stdRenderBabylon_SetLightShafts(3);
    std3D_SetFrameTime(0.016f);
    RenderScene(drawSky);
    float u = -1.0f;
    float v = -1.0f;
    float weight = 0.0f;
    TEST_ASSERT_TRUE(stdRenderBabylon_GetLightShaftCentroidForTest(&u, &v, &weight));
    std::printf("M9.16 light shafts: centroid (%g, %g), weight %g\n", (double)u, (double)v, (double)weight);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, u);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, v);
    TEST_ASSERT_TRUE(weight > 0.0f);

    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(off != on);

    // Bright geometry that is not tagged sky contributes no mask, so the stage adds exactly zero.
    std3D_SetFrameTime(0.016f);
    RenderScene(drawNoSky);
    const std::vector<std::byte> noSky = CapturePresented(64u, 64u);
    stdRenderBabylon_SetLightShafts(0);
    RenderScene(drawNoSky);
    const std::vector<std::byte> noSkyOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(noSkyOff.size(), noSky.size());
    TEST_ASSERT_EQUAL_MEMORY(noSkyOff.data(), noSky.data(), noSky.size());
}

TEST(std3DBabylonGpu, LightShaftsAddMuchLessToSkyPixelsThanToGeometry)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));

    // A full-screen mid-grey sky with a small ordinary-grey quad over it. The uniform sky makes the
    // radial blur nearly constant, so the two pixels start at the same scene value and see about the
    // same shaft value; only the normal tag differs. The composite scales the shafts by 0.15 on the
    // sky tag, keeping the clouds crisp, and at full strength on ordinary geometry.
    const auto drawScene = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 64.0f, 64.0f, 0.9f, 0xFF808080u);
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 32.0f, 34.0f, 34.0f, 0.5f, 0xFF808080u);
    };
    const auto redChannel = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    stdRenderBabylon_SetLightShafts(0);
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    const int skyOff      = redChannel(off, 10u, 10u);
    const int ordinaryOff = redChannel(off, 32u, 32u);

    stdRenderBabylon_SetLightShafts(1); // Low, strength 0.25
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    const int skyAdded      = redChannel(on, 10u, 10u) - skyOff;
    const int ordinaryAdded = redChannel(on, 32u, 32u) - ordinaryOff;

    // The two pixels have different shaft values (the ordinary quad is a hole in the mask), so
    // compare the added light per unit of the half-res blur the composite actually samples.
    float skyShaft = 0.0f;
    float ordinaryShaft = 0.0f;
    TEST_ASSERT_TRUE(stdRenderBabylon_GetLightShaftValueForTest(10u, 10u, &skyShaft));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetLightShaftValueForTest(32u, 32u, &ordinaryShaft));
    TEST_ASSERT_TRUE(skyShaft > 0.0f);
    TEST_ASSERT_TRUE(ordinaryShaft > 0.0f);
    const float skyPerShaft      = (float)skyAdded / skyShaft;
    const float ordinaryPerShaft = (float)ordinaryAdded / ordinaryShaft;
    std::printf("M9.16 light shafts sky/geometry: added %d/255 to sky (%.3f per shaft), %d/255 to geometry (%.3f per shaft)\n",
        skyAdded, (double)skyPerShaft, ordinaryAdded, (double)ordinaryPerShaft);
    TEST_ASSERT_TRUE(ordinaryPerShaft > 20.0f);
    TEST_ASSERT_TRUE(skyAdded >= 0);
    TEST_ASSERT_TRUE(skyPerShaft <= ordinaryPerShaft * 0.15f + 3.0f);

    // Sanity: the normal target still holds the two tags the composite reads.
    const std::vector<std::byte> aNormals = std3DBabylonTest_pDevice->readTexture(
        stdRenderBabylon_GetNormalTarget(), 0);
    const bool b10 = stdRenderBabylon_GetNormalFormat() == render::TextureFormat::R10G10B10A2;
    TEST_ASSERT_EQUAL_INT(b10 ? 2 : 170, NormalTexel(aNormals, 10u, 10u)[3]);
    TEST_ASSERT_EQUAL_INT(b10 ? 3 : 255, NormalTexel(aNormals, 32u, 32u)[3]);
}

TEST(std3DBabylonGpu, LightShaftsLeaveTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));

    const auto drawScene = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 32.0f, 16.0f, 0.9f, 0xFFFFFFFFu);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    };

    stdRenderBabylon_SetLightShafts(0);
    RenderScene(drawScene);
    const std::vector<std::byte> hudOff = CapturePresented(32u, 16u);

    stdRenderBabylon_SetLightShafts(3);
    std3D_SetFrameTime(0.016f);
    RenderScene(drawScene);
    const std::vector<std::byte> hudOn = CapturePresented(32u, 16u);

    // The HUD's green line is composited after the shaft stage: its pixels are unchanged.
    for ( uint32_t x = 16u; x <= 16u; ++x )
    {
        const size_t at = ((size_t)8u * 32u + x) * 4u;
        TEST_ASSERT_EQUAL_INT((int)hudOff[at + 2u], (int)hudOn[at + 2u]);
        TEST_ASSERT_EQUAL_INT((int)hudOff[at + 1u], (int)hudOn[at + 1u]);
        TEST_ASSERT_EQUAL_INT((int)hudOff[at + 0u], (int)hudOn[at + 0u]);
        TEST_ASSERT_EQUAL_INT((int)hudOff[at + 3u], (int)hudOn[at + 3u]);
    }
}

TEST(std3DBabylonGpu, CameraEffectsOffLeaveTheImageByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));

    const auto drawScene = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 64.0f, 64.0f, 0.95f, 0xFF101010u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0xFFFFFFFFu);
    };

    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    // Each option on, then off again: the image must be the same bytes as the first capture.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(true));
    RenderScene(drawScene);
    const std::vector<std::byte> vignette = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(off != vignette);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
    RenderScene(drawScene);
    const std::vector<std::byte> vignetteOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(off.data(), vignetteOff.data(), off.size());

    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(true));
    std3D_SetFrameTime(0.016f);
    RenderScene(drawScene);
    const std::vector<std::byte> grain = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(off != grain);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));
    RenderScene(drawScene);
    const std::vector<std::byte> grainOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(off.data(), grainOff.data(), off.size());

    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(true));
    std3D_SetFrameTime(0.016f);
    RenderScene(drawScene);
    const std::vector<std::byte> flare = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(off != flare);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    RenderScene(drawScene);
    const std::vector<std::byte> flareOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(off.data(), flareOff.data(), off.size());
}

TEST(std3DBabylonGpu, VignetteDarkensTheCornersAndLeavesTheCentre)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));

    const auto drawFlat = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF808080u);
    };
    const auto red = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
    RenderScene(drawFlat);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(true));
    RenderScene(drawFlat);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    // The centre is untouched, within one 8-bit step, and the corner is clearly darker.
    TEST_ASSERT_INT_WITHIN(1, red(off, 32u, 32u), red(on, 32u, 32u));
    TEST_ASSERT_TRUE(red(on, 63u, 63u) < red(off, 63u, 63u) - 20);
    TEST_ASSERT_TRUE(red(on, 0u, 0u) < red(off, 0u, 0u) - 20);
}

TEST(std3DBabylonGpu, FilmGrainIsDeterministicAndFollowsTheFrameCounter)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));

    const auto drawFlat = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF808080u);
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));
    RenderScene(drawFlat);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(true));
    std3D_SetFrameTime(0.016f);
    RenderScene(drawFlat);
    const std::vector<std::byte> first = CapturePresented(64u, 64u);

    // No game time passes: the same frame counter gives exactly the same noise.
    RenderScene(drawFlat);
    const std::vector<std::byte> same = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(first.data(), same.data(), first.size());

    // A later game-time frame changes the grain.
    std3D_SetFrameTime(0.016f);
    RenderScene(drawFlat);
    const std::vector<std::byte> later = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(first != later);

    // The noise changes pixels but stays zero-mean within about one 8-bit step per channel.
    TEST_ASSERT_TRUE(off != first);
    double offSum = 0.0;
    double onSum = 0.0;
    for ( uint32_t y = 0; y < 64u; ++y )
    {
        for ( uint32_t x = 0; x < 64u; ++x )
        {
            const size_t at = ((size_t)y * 64u + x) * 4u;
            offSum += (double)off[at + 2u];
            onSum += (double)first[at + 2u];
        }
    }
    const double offMean = offSum / (64.0 * 64.0);
    const double onMean  = onSum / (64.0 * 64.0);
    std::printf("M9.18 film grain: mean red %.3f off, %.3f on\n", offMean, onMean);
    TEST_ASSERT_TRUE(std::fabs(offMean - onMean) <= 1.0);
}

TEST(std3DBabylonGpu, LensFlareAddsLightAlongTheGhostLine)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));

    const auto drawSky = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 32.0f, 64.0f, 64.0f, 0.95f, 0xFF101010u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 32.0f, 32.0f, 0.9f, 0xFFFFFFFFu);
    };
    const auto red = [](const std::vector<std::byte>& pixels, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * 64u + x) * 4u + 2u];
    };

    // With the bright sky, flare adds light on the dark geometry on the light-to-centre line.
    // The 1.4 ghost of a light at (0.25, 0.25) lands at (0.6, 0.6), pixel (38, 38). This is the
    // first flare frame, so the smoothed strength snaps to the visible-sky value.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    RenderScene(drawSky);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(true));
    RenderScene(drawSky);
    float flareU = -1.0f;
    float flareV = -1.0f;
    float flareStrength = 0.0f;
    TEST_ASSERT_TRUE(stdRenderBabylon_GetLensFlareStateForTest(&flareU, &flareV, &flareStrength));
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, flareU);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.25f, flareV);
    TEST_ASSERT_TRUE(flareStrength > 0.9f);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    const int added = red(on, 38u, 38u) - red(off, 38u, 38u);
    std::printf("M9.18 lens flare: light (%.3f, %.3f) strength %.3f, added %d/255 on the ghost line\n",
        (double)flareU, (double)flareV, (double)flareStrength, added);
    TEST_ASSERT_TRUE(added >= 3);
}

TEST(std3DBabylonGpu, LensFlareAddsNothingWithoutASky)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));

    const auto drawNoSky = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFFFFFFFFu);
    };

    // Bright geometry that is not tagged sky contributes no mask, so the first flare frame has
    // zero strength and must be byte-identical to the path without flare.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    RenderScene(drawNoSky);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(true));
    RenderScene(drawNoSky);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(off.data(), on.data(), off.size());
}

TEST(std3DBabylonGpu, CameraEffectsLeaveTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));

    const auto drawScene = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 32.0f, 16.0f, 0.9f, 0xFFFFFFFFu);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(32u, 16u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(true));
    std3D_SetFrameTime(0.016f);
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(32u, 16u);

    const size_t hud = ((size_t)8u * 32u + 16u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[hud + 0u], (int)on[hud + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[hud + 1u], (int)on[hud + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[hud + 2u], (int)on[hud + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[hud + 3u], (int)on[hud + 3u]);
}
