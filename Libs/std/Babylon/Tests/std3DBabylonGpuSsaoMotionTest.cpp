// The std3DBabylonGpu ambient occlusion and motion blur tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

using namespace std3DBabylonTest;

namespace
{
    constexpr float MotionBlurPi = 3.14159265358979323846f;

    // The rdCamera model matrix of a camera turned yawDegrees about the world's up axis and placed
    // at (x, y, z): right, forward and up basis vectors, then the position.
    void SetMotionBlurCamera(float yawDegrees, float x, float y, float z, bool bCut)
    {
        const float yaw = yawDegrees * MotionBlurPi / 180.0f;
        const float c = std::cos(yaw);
        const float s = std::sin(yaw);
        const float aMatrix[12] = {
            c, s, 0.0f,        // right
            -s, c, 0.0f,       // forward
            0.0f, 0.0f, 1.0f,  // up
            x, y, z
        };
        stdRenderBabylon_SetCameraTransform(aMatrix, bCut);
    }

    float MotionBlurSceneDepth()
    {
        return SceneDepthForEyeDepth(20.0f, 0.1f, 100.0f);
    }

    // A black far plane with a white spot at the centre, all at one ordinary depth.
    void DrawMotionBlurSpotScene(uint32_t size)
    {
        const float z = MotionBlurSceneDepth();
        const float half = (float)size * 0.5f;
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, (float)size, (float)size, z, 0xFF000000u);
        DrawQuad(nullptr, (Std3DRenderState)0, half - 4.0f, half - 4.0f,
            half + 4.0f, half + 4.0f, z, 0xFFFFFFFFu);
    }

    int MotionBlurRedAt(const std::vector<std::byte>& pixels, uint32_t width, uint32_t x, uint32_t y)
    {
        return (int)pixels[((size_t)y * width + x) * 4u + 2u];
    }

    // Everything but the motion-blur stage off, so only it changes the picture.
    void UseMotionBlurOnly()
    {
        TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
        stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);
    }
}

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

TEST(std3DBabylonGpu, SsaoOffLeavesTheImageByteIdentical)
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
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    TEST_ASSERT_EQUAL_INT(0, stdRenderBabylon_GetSsao());
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);

    RenderScene(DrawSsaoCreaseScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    RenderScene(DrawSsaoCreaseScene);
    const std::vector<std::byte> offAgain = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());

    // On with only a sky surface and with no geometry at all: every pixel is left unchanged.
    const auto drawSky = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF204060u);
    };
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(1));
    RenderScene(drawSky);
    const std::vector<std::byte> skyOn = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    RenderScene(drawSky);
    const std::vector<std::byte> skyOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(skyOff.data(), skyOn.data(), skyOn.size());

    const auto drawNothing = [] {};
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(1));
    RenderScene(drawNothing);
    const std::vector<std::byte> emptyOn = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    RenderScene(drawNothing);
    const std::vector<std::byte> emptyOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_MEMORY(emptyOff.data(), emptyOn.data(), emptyOn.size());
}

TEST(std3DBabylonGpu, SsaoDarkensACreaseAndLeavesOpenFlatAreas)
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
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    RenderScene(DrawSsaoCreaseScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(1));
    RenderScene(DrawSsaoCreaseScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    // Near the vertical crease on the far wall the picture darkens.
    const float offCrease = SsaoMeanBrightness(off, 64u, 33u, 4u, 38u, 18u);
    const float onCrease  = SsaoMeanBrightness(on, 64u, 33u, 4u, 38u, 18u);
    std::printf("M9.17b SSAO: crease mean %.2f off, %.2f on\n",
        (double)offCrease, (double)onCrease);
    TEST_ASSERT_TRUE(onCrease < offCrease - 1.0f);

    // A flat area away from every crease is unchanged within the capture's rounding.
    const float offOpen = SsaoMeanBrightness(off, 64u, 4u, 4u, 12u, 18u);
    const float onOpen  = SsaoMeanBrightness(on, 64u, 4u, 4u, 12u, 18u);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, offOpen, onOpen);
}

TEST(std3DBabylonGpu, SsaoNearObjectLeavesTheBackgroundRingAlone)
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
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    RenderScene(DrawSsaoNearObjectScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(3));
    RenderScene(DrawSsaoNearObjectScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    // The ring 3 to 12 pixels outside the 27..37 x 11..53 object: a nearer object this far in front
    // of the wall must not darken it on High.
    const uint32_t x0 = 27u;
    const uint32_t x1 = 37u;
    const uint32_t y0 = 11u;
    const uint32_t y1 = 53u;
    double offSum = 0.0;
    double onSum  = 0.0;
    size_t count  = 0u;
    for ( int y = (int)y0 - 12; y < (int)y1 + 12; ++y )
    {
        for ( int x = (int)x0 - 12; x < (int)x1 + 12; ++x )
        {
            if ( x < 0 || x >= 64 || y < 0 || y >= 64 )
            {
                continue;
            }
            if ( x >= (int)x0 - 3 && x < (int)x1 + 3 && y >= (int)y0 - 3 && y < (int)y1 + 3 )
            {
                continue;
            }

            const size_t at = ((size_t)y * 64u + (size_t)x) * 4u;
            offSum += (double)off[at + 0u] + (double)off[at + 1u] + (double)off[at + 2u];
            onSum  += (double)on[at + 0u] + (double)on[at + 1u] + (double)on[at + 2u];
            count  += 3u;
        }
    }

    TEST_ASSERT_TRUE(count > 0u);
    const float offRing = (float)(offSum / (double)count);
    const float onRing  = (float)(onSum / (double)count);
    std::printf("M9.17b SSAO near-object ring: %.2f off, %.2f on\n",
        (double)offRing, (double)onRing);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, offRing, onRing);
}

TEST(std3DBabylonGpu, SsaoLeavesSkyAndTheHudLayerPixelExact)
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
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);

    const auto drawScene = []
    {
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, 0.97f, 0xFF808080u);
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 2.0f, 2.0f, 28.0f, 30.0f, 0.95f, 0xFF204060u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 35.0f, 64.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(3));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    const size_t skyPixel = ((size_t)10u * 64u + 10u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 0u], (int)on[skyPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 1u], (int)on[skyPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 2u], (int)on[skyPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 3u], (int)on[skyPixel + 3u]);

    const size_t hudPixel = ((size_t)10u * 64u + 33u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 0u], (int)on[hudPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 1u], (int)on[hudPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 2u], (int)on[hudPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 3u], (int)on[hudPixel + 3u]);
}

TEST(std3DBabylonGpu, SsaoLevelsDarkenMonotonically)
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
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);

    auto capture = [](int level)
    {
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(level));
        RenderScene(DrawSsaoCreaseScene);
        return CapturePresented(64u, 64u);
    };

    const std::vector<std::byte> off  = capture(0);
    const std::vector<std::byte> low  = capture(1);
    const std::vector<std::byte> medium = capture(2);
    const std::vector<std::byte> high = capture(3);

    const float offMean    = SsaoMeanBrightness(off, 64u, 32u, 2u, 44u, 30u);
    const float lowMean    = SsaoMeanBrightness(low, 64u, 32u, 2u, 44u, 30u);
    const float mediumMean = SsaoMeanBrightness(medium, 64u, 32u, 2u, 44u, 30u);
    const float highMean   = SsaoMeanBrightness(high, 64u, 32u, 2u, 44u, 30u);
    std::printf("M9.17b SSAO levels: %.2f %.2f %.2f %.2f\n",
        (double)offMean, (double)lowMean, (double)mediumMean, (double)highMean);
    TEST_ASSERT_TRUE(lowMean < offMean);
    TEST_ASSERT_TRUE(mediumMean < lowMean);
    TEST_ASSERT_TRUE(highMean < mediumMean);
}

TEST(std3DBabylonGpu, SsaoProjectionIsAspectCorrect)
{
    if ( !OpenGpuStd3D(64u, 48u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    // rdCamera's fov is the horizontal fov at 4:3; at 90 degrees the vertical tangent is 0.75 and
    // the horizontal one is 1.0 at 4:3.
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);
    float tanHalfX = 0.0f;
    float tanHalfY = 0.0f;
    TEST_ASSERT_TRUE(stdRenderBabylon_GetSsaoProjectionForTest(&tanHalfX, &tanHalfY));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.75f, tanHalfY);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 1.0f, tanHalfX);

    // At 16:9 the vertical field stays the same and the horizontal one widens (Hor+).
    bool bRecreated = false;
    TEST_ASSERT_TRUE(stdRenderBabylon_SetBackBufferSize(64u, 36u, &bRecreated));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetSsaoProjectionForTest(&tanHalfX, &tanHalfY));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 0.75f, tanHalfY);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-5f, 4.0f / 3.0f, tanHalfX);
}

TEST(std3DBabylonGpu, SsaoLeavesTheFirstPersonOverlayAlone)
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
    stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);

    // The crease scene fills the frame, then a small ordinary quad at sz = 0.05 stands in for the
    // first-person overlay range (0..0.1). Its depth neighbours are geometry, so a blur that used
    // them would darken it.
    const auto drawScene = []
    {
        DrawSsaoCreaseScene();
        DrawQuad(nullptr, (Std3DRenderState)0, 30.0f, 30.0f, 34.0f, 34.0f, 0.05f, 0xFFE0E0E0u);
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(3));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    const size_t overlayPixel = ((size_t)32u * 64u + 32u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 0u], (int)on[overlayPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 1u], (int)on[overlayPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 2u], (int)on[overlayPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 3u], (int)on[overlayPixel + 3u]);
}


TEST(std3DBabylonGpu, MotionBlurOffIsByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseMotionBlurOnly();
    const auto scene = [] { DrawMotionBlurSpotScene(64u); };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(0));
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, true);
    RenderScene(scene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    // On with a real rotation changes the picture.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(3));
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(10.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(off != on);

    // Off again skips every pass and is byte-identical to the first Off frame.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(0));
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> offAgain = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(off.size(), offAgain.size());
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());
}

TEST(std3DBabylonGpu, MotionBlurStaticCameraLeavesTheImageByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseMotionBlurOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(1));
    const auto scene = [] { DrawMotionBlurSpotScene(64u); };

    // The first frame establishes the previous transform; the second one sees the same camera.
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(23.0f, 1.0f, 2.0f, 3.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> first = CapturePresented(64u, 64u);

    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(23.0f, 1.0f, 2.0f, 3.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> second = CapturePresented(64u, 64u);

    TEST_ASSERT_EQUAL_size_t(first.size(), second.size());
    TEST_ASSERT_EQUAL_MEMORY(first.data(), second.data(), first.size());
}

TEST(std3DBabylonGpu, MotionBlurRotationSmearsHorizontallyOnly)
{
    if ( !OpenGpuStd3D(96u, 96u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseMotionBlurOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(3));
    const auto scene = [] { DrawMotionBlurSpotScene(96u); };
    const uint32_t centre = 48u;

    // A yaw of 8 degrees is under the snap threshold, so the stage stays active.
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> blurred = CapturePresented(96u, 96u);

    const int centreRed = MotionBlurRedAt(blurred, 96u, centre, centre);
    const int rightRed  = MotionBlurRedAt(blurred, 96u, centre + 5u, centre);
    const int aboveRed  = MotionBlurRedAt(blurred, 96u, centre, centre - 5u);
    std::printf("M9.19 motion blur yaw: centre %d/255, 5 px right %d/255, 5 px above %d/255\n",
        centreRed, rightRed, aboveRed);
    TEST_ASSERT_TRUE(centreRed > 250);
    TEST_ASSERT_TRUE(rightRed > 0);
    TEST_ASSERT_TRUE(aboveRed <= 1);
}

TEST(std3DBabylonGpu, MotionBlurCutPauseAndTeleportSkipTheStage)
{
    if ( !OpenGpuStd3D(96u, 96u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseMotionBlurOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(3));
    const auto scene = [] { DrawMotionBlurSpotScene(96u); };
    const uint32_t centre = 48u;

    // A reference frame with the same rotation, so only the skipped stage differs.
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> crisp = CapturePresented(96u, 96u);
    TEST_ASSERT_TRUE(MotionBlurRedAt(crisp, 96u, centre + 5u, centre) <= 1);

    // A cut resets the previous transform and records no blur.
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, true);
    RenderScene(scene);
    const std::vector<std::byte> cut = CapturePresented(96u, 96u);
    TEST_ASSERT_TRUE(MotionBlurRedAt(cut, 96u, centre, centre) > 250);
    TEST_ASSERT_TRUE(MotionBlurRedAt(cut, 96u, centre + 5u, centre) <= 1);

    // A zero game-time step (paused, a menu) resets it too.
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    stdRenderBabylon_SetFrameTime(0.0f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> paused = CapturePresented(96u, 96u);
    TEST_ASSERT_TRUE(MotionBlurRedAt(paused, 96u, centre, centre) > 250);
    TEST_ASSERT_TRUE(MotionBlurRedAt(paused, 96u, centre + 5u, centre) <= 1);

    // A jump over the teleport threshold resets it and records no blur.
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 100.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> teleported = CapturePresented(96u, 96u);
    TEST_ASSERT_TRUE(MotionBlurRedAt(teleported, 96u, centre, centre) > 250);
    TEST_ASSERT_TRUE(MotionBlurRedAt(teleported, 96u, centre + 5u, centre) <= 1);
}

TEST(std3DBabylonGpu, MotionBlurLeavesTheFirstPersonOverlayAlone)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseMotionBlurOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(3));

    const auto scene = []
    {
        const float z = MotionBlurSceneDepth();
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 64.0f, 64.0f, z, 0xFF202020u);
        // The first-person overlay range is 0..0.1 in rdCamera's scene depth.
        DrawQuad(nullptr, (Std3DRenderState)0, 28.0f, 28.0f, 36.0f, 36.0f, 0.05f, 0xFFE0E0E0u);
    };

    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, true);
    RenderScene(scene);
    const std::vector<std::byte> crisp = CapturePresented(64u, 64u);

    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> blurred = CapturePresented(64u, 64u);

    // The overlay is returned unchanged; the pixel just outside it stays background.
    const size_t overlay = ((size_t)32u * 64u + 32u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)crisp[overlay + 0u], (int)blurred[overlay + 0u]);
    TEST_ASSERT_EQUAL_INT((int)crisp[overlay + 1u], (int)blurred[overlay + 1u]);
    TEST_ASSERT_EQUAL_INT((int)crisp[overlay + 2u], (int)blurred[overlay + 2u]);
    TEST_ASSERT_EQUAL_INT((int)crisp[overlay + 3u], (int)blurred[overlay + 3u]);
    TEST_ASSERT_TRUE(MotionBlurRedAt(blurred, 64u, 40u, 32u) <= 0x24);
}

TEST(std3DBabylonGpu, MotionBlurLeavesTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(32u, 16u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseMotionBlurOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(3));

    const auto scene = []
    {
        const float z = SceneDepthForEyeDepth(20.0f, 0.1f, 100.0f);
        DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, 0.0f, 32.0f, 16.0f, z, 0xFF202020u);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 16.0f, 0.0f, 17.0f, 16.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    };

    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, true);
    RenderScene(scene);
    const std::vector<std::byte> crisp = CapturePresented(32u, 16u);

    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(0.0f, 0.0f, 0.0f, 0.0f, false);
    stdRenderBabylon_SetFrameTime(0.016f);
    SetMotionBlurCamera(8.0f, 0.0f, 0.0f, 0.0f, false);
    RenderScene(scene);
    const std::vector<std::byte> blurred = CapturePresented(32u, 16u);

    const size_t hud = ((size_t)8u * 32u + 16u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)crisp[hud + 0u], (int)blurred[hud + 0u]);
    TEST_ASSERT_EQUAL_INT((int)crisp[hud + 1u], (int)blurred[hud + 1u]);
    TEST_ASSERT_EQUAL_INT((int)crisp[hud + 2u], (int)blurred[hud + 2u]);
    TEST_ASSERT_EQUAL_INT((int)crisp[hud + 3u], (int)blurred[hud + 3u]);
}
