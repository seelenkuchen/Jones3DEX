// The std3DBabylonGpu contact-shadow tests.
// Part of the std3D Babylon adapter tests; see std3DBabylonTest.cpp for the fixture overview.
#include "std3DBabylonTestSupport.h"

#include <std/General/stdConfig.h>
#include <std/General/stdJSON.h>

#include <cstdio>

using namespace std3DBabylonTest;

namespace
{
    constexpr const char* kContactShadowConfigPath = "stdRenderBabylonContactShadowTest.json";
    constexpr const char* kContactShadowKey        = "engine.renderer.contactShadows";

    // The fallback reader stdConfig requires; this suite reads only keys the config file holds.
    bool ContactShadowFallbackHasStarted(void)
    {
        return true;
    }

    int ContactShadowFallbackGetInt(const char*, int defaultValue)
    {
        return defaultValue;
    }

    float ContactShadowFallbackGetFloat(const char*, float defaultValue)
    {
        return defaultValue;
    }

    int ContactShadowFallbackGetStr(const char*, char* pDstStr, size_t size,
        const char* pDefaultValue)
    {
        if ( pDstStr && size > 0 )
        {
            pDstStr[0] = '\0';
            if ( pDefaultValue )
            {
                std::snprintf(pDstStr, size, "%s", pDefaultValue);
            }
        }
        return 0;
    }

    // Everything but the contact-shadow stage off, so only it changes the picture.
    void UseContactShadowOnly()
    {
        TEST_ASSERT_TRUE(stdRenderBabylon_SetBloom(0.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetLightShafts(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetAntiAliasing(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSharpening(0.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetDepthOfField(false, false, 1.0f));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetMotionBlur(0));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetLensFlare(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetVignette(false));
        TEST_ASSERT_TRUE(stdRenderBabylon_SetFilmGrain(false));
        stdRenderBabylon_SetProjection(90.0f, 0.1f, 100.0f);
    }

    // A camera looking horizontally along +Y with world Z up: the shader basis is right (1,0,0),
    // up (0,0,1) and forward (0,1,0), so the fixed overhead light points into the screen and up.
    void SetContactShadowCamera()
    {
        const float aMatrix[12] = {
            1.0f, 0.0f, 0.0f, // right
            0.0f, 1.0f, 0.0f, // forward
            0.0f, 0.0f, 1.0f, // up
            0.0f, 0.0f, 0.0f
        };
        stdRenderBabylon_SetCameraTransform(aMatrix, true);
    }

    // The view-space depth of a floor plane at y = -0.18 for a 64 x 64 scene with the test's 90
    // degree fov (vertical tangent 0.75). screenY is a continuous screen row.
    float ContactShadowFloorEyeDepth(float screenY)
    {
        const float v = screenY / 64.0f;
        return -0.18f / ((1.0f - 2.0f * v) * 0.75f);
    }

    // rdCamera's scene depth of the floor at screenY.
    float ContactShadowFloorDepth(float screenY)
    {
        return SceneDepthForEyeDepth(ContactShadowFloorEyeDepth(screenY), 0.1f, 100.0f);
    }

    // The contact-shadow scene: a floor plane below the camera, a box standing on it at the right
    // and, when asked, a sky patch and a first-person overlay. The box front is a constant depth a
    // little in front of the floor where it meets the box, so the floor just left of its visible
    // base is the receiver of a short shadow toward the overhead light.
    void DrawContactShadowScene(bool bSkyAndOverlay)
    {
        const uint32_t grey = 0xFF909090u;
        // Thin strips: a per-pixel step quantises the floor depth too coarsely for a 0.03 contact
        // thickness at these distances, and the quantisation error would read as self-shadow.
        constexpr uint32_t kSubSteps = 8u;
        for ( uint32_t step = 0; step < 32u * kSubSteps; ++step )
        {
            const float y0 = 32.0f + (float)step / (float)kSubSteps;
            const float y1 = 32.0f + (float)(step + 1u) / (float)kSubSteps;
            DrawQuad(nullptr, (Std3DRenderState)0, 0.0f, y0, 64.0f, y1,
                ContactShadowFloorDepth(0.5f * (y0 + y1)), grey);
        }

        const float boxDepth = SceneDepthForEyeDepth(ContactShadowFloorEyeDepth(48.5f) - 0.01f,
            0.1f, 100.0f);
        DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 20.0f, 56.0f, 56.0f, boxDepth, grey);

        if ( bSkyAndOverlay )
        {
            DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 16.0f, 16.0f,
                SceneDepthForEyeDepth(0.5f, 0.1f, 100.0f), 0xFF204060u);
            // The first-person overlay range is 0..0.1 in rdCamera's scene depth.
            DrawQuad(nullptr, (Std3DRenderState)0, 40.0f, 36.0f, 46.0f, 42.0f, 0.05f,
                0xFFE0E0E0u);
        }
    }

    float ContactShadowMeanBrightness(const std::vector<std::byte>& pixels, uint32_t width,
        uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
    {
        return SsaoMeanBrightness(pixels, width, x0, y0, x1, y1);
    }

    struct RecordedPass
    {
        render::BeginPass begin;
        std::vector<render::BindTexture> aTextures;
    };

    std::vector<RecordedPass> RecordedPasses()
    {
        std::vector<RecordedPass> aPasses;
        for ( const render::RecordedCommand& command : Null().recorded() )
        {
            if ( const render::BeginPass* pBegin = std::get_if<render::BeginPass>(&command) )
            {
                RecordedPass pass;
                pass.begin = *pBegin;
                aPasses.push_back(pass);
            }
            else if ( const render::BindTexture* pBind = std::get_if<render::BindTexture>(&command) )
            {
                if ( !aPasses.empty() )
                {
                    aPasses.back().aTextures.push_back(*pBind);
                }
            }
        }
        return aPasses;
    }

    render::TextureHandle PassTarget(const RecordedPass& pass)
    {
        return pass.begin.desc.colorTargets.empty()
            ? render::TextureHandle{} : pass.begin.desc.colorTargets[0].target;
    }
}

TEST_GROUP(std3DBabylonGpu);

void TEST_std3DBabylonGpu_SETUP(void);
void TEST_std3DBabylonGpu_TEAR_DOWN(void);

TEST(std3DBabylonGpu, ContactShadowsOffLeavesTheImageByteIdentical)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseContactShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetContactShadows());
    SetContactShadowCamera();

    RenderScene([] { DrawContactShadowScene(false); });
    const std::vector<std::byte> off = CapturePresented(64u, 64u);
    RenderScene([] { DrawContactShadowScene(false); });
    const std::vector<std::byte> offAgain = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(off.size(), offAgain.size());
    TEST_ASSERT_EQUAL_MEMORY(off.data(), offAgain.data(), off.size());

    // On, with only a sky surface and with no geometry at all, every pixel is left unchanged.
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    const auto drawSky = []
    {
        DrawQuad(nullptr, STD3D_RS_SURFACE_SKY, 0.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF204060u);
    };
    RenderScene(drawSky);
    const std::vector<std::byte> skyOn = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
    RenderScene(drawSky);
    const std::vector<std::byte> skyOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(skyOff.size(), skyOn.size());
    TEST_ASSERT_EQUAL_MEMORY(skyOff.data(), skyOn.data(), skyOn.size());

    const auto drawNothing = [] {};
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    RenderScene(drawNothing);
    const std::vector<std::byte> emptyOn = CapturePresented(64u, 64u);
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
    RenderScene(drawNothing);
    const std::vector<std::byte> emptyOff = CapturePresented(64u, 64u);
    TEST_ASSERT_EQUAL_size_t(emptyOff.size(), emptyOn.size());
    TEST_ASSERT_EQUAL_MEMORY(emptyOff.data(), emptyOn.data(), emptyOn.size());
}

TEST(std3DBabylonGpu, ContactShadowPassRunsBetweenWaterAndSsao)
{
    UseNullDevice();
    OpenStd3D(64u, 64u);

    UseContactShadowOnly();
    TEST_ASSERT_TRUE(stdRenderBabylon_SetWaterRipples(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetSsao(1));
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    SetContactShadowCamera();

    Null().clearRecorded();
    TEST_ASSERT_EQUAL_INT(0, std3D_StartScene());
    DrawQuad(nullptr, STD3D_RS_SURFACE_WATER, 0.0f, 0.0f, 32.0f, 64.0f, 0.5f, 0xFF3080C0u);
    DrawQuad(nullptr, (Std3DRenderState)0, 32.0f, 0.0f, 64.0f, 64.0f, 0.5f, 0xFF204080u);
    EndFrame();

    const std::vector<RecordedPass> aPasses = RecordedPasses();
    const render::TextureHandle depthTarget  = stdRenderBabylon_GetDepthTarget();
    const render::TextureHandle normalTarget = stdRenderBabylon_GetNormalTarget();

    // The water pass samples the scene twice (slots 0 and 1) and the normal target in slot 2.
    size_t waterIndex = aPasses.size();
    for ( size_t i = 0; i < aPasses.size(); ++i )
    {
        const std::vector<render::BindTexture>& aTextures = aPasses[i].aTextures;
        if ( aTextures.size() >= 3u && aTextures[0].texture == aTextures[1].texture
            && aTextures[2].texture == normalTarget )
        {
            waterIndex = i;
            break;
        }
    }

    TEST_ASSERT_TRUE(waterIndex < aPasses.size());

    // The contact-shadow mask pass reads depth and normals and writes a scratch mask.
    size_t maskIndex = aPasses.size();
    for ( size_t i = waterIndex + 1u; i < aPasses.size(); ++i )
    {
        const std::vector<render::BindTexture>& aTextures = aPasses[i].aTextures;
        if ( aTextures.size() == 2u && aTextures[0].texture == depthTarget
            && aTextures[1].texture == normalTarget )
        {
            maskIndex = i;
            break;
        }
    }

    TEST_ASSERT_TRUE(maskIndex < aPasses.size());
    const render::TextureHandle maskOutput = PassTarget(aPasses[maskIndex]);
    TEST_ASSERT_FALSE(maskOutput.isNull());

    // The blur pass reads that mask and depth and writes a second scratch mask.
    size_t blurIndex = aPasses.size();
    for ( size_t i = maskIndex + 1u; i < aPasses.size(); ++i )
    {
        const std::vector<render::BindTexture>& aTextures = aPasses[i].aTextures;
        if ( aTextures.size() == 2u && aTextures[0].texture == maskOutput
            && aTextures[1].texture == depthTarget )
        {
            blurIndex = i;
            break;
        }
    }

    TEST_ASSERT_TRUE(blurIndex < aPasses.size());
    const render::TextureHandle blurOutput = PassTarget(aPasses[blurIndex]);
    TEST_ASSERT_FALSE(blurOutput.isNull());

    // The composite pass reads depth, normals, the water output and the blurred mask, and writes
    // the stage's target.
    const render::TextureHandle waterOutput = PassTarget(aPasses[waterIndex]);
    size_t contactIndex = aPasses.size();
    for ( size_t i = blurIndex + 1u; i < aPasses.size(); ++i )
    {
        const std::vector<render::BindTexture>& aTextures = aPasses[i].aTextures;
        if ( aTextures.size() == 4u && aTextures[0].texture == depthTarget
            && aTextures[1].texture == normalTarget && aTextures[2].texture == waterOutput
            && aTextures[3].texture == blurOutput )
        {
            contactIndex = i;
            break;
        }
    }

    TEST_ASSERT_TRUE(contactIndex < aPasses.size());
    const render::TextureHandle contactOutput = PassTarget(aPasses[contactIndex]);

    // The ambient-occlusion composite then samples the contact-shadow output in slot 0, with the
    // normals in slot 2.
    size_t ssaoCompositeIndex = aPasses.size();
    for ( size_t i = contactIndex + 1u; i < aPasses.size(); ++i )
    {
        const std::vector<render::BindTexture>& aTextures = aPasses[i].aTextures;
        if ( aTextures.size() == 3u && aTextures[0].texture == contactOutput
            && aTextures[2].texture == normalTarget )
        {
            ssaoCompositeIndex = i;
            break;
        }
    }

    TEST_ASSERT_TRUE(ssaoCompositeIndex < aPasses.size());
    TEST_ASSERT_TRUE(waterIndex < maskIndex);
    TEST_ASSERT_TRUE(maskIndex < blurIndex);
    TEST_ASSERT_TRUE(blurIndex < contactIndex);
    TEST_ASSERT_TRUE(contactIndex < ssaoCompositeIndex);
    TEST_ASSERT_TRUE(aPasses[maskIndex].aTextures[0].texture == depthTarget);
    TEST_ASSERT_TRUE(aPasses[maskIndex].aTextures[1].texture == normalTarget);
    TEST_ASSERT_TRUE(aPasses[blurIndex].aTextures[0].texture == maskOutput);
    TEST_ASSERT_TRUE(aPasses[blurIndex].aTextures[1].texture == depthTarget);
    TEST_ASSERT_TRUE(aPasses[contactIndex].aTextures[2].texture == waterOutput);
    TEST_ASSERT_TRUE(aPasses[contactIndex].aTextures[3].texture == blurOutput);
    TEST_ASSERT_FALSE(contactOutput == waterOutput);
    TEST_ASSERT_FALSE(maskOutput == blurOutput);
}

TEST(std3DBabylonGpu, ContactShadowsDarkenTheFloorBesideABox)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseContactShadowOnly();
    SetContactShadowCamera();

    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
    RenderScene([] { DrawContactShadowScene(true); });
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    RenderScene([] { DrawContactShadowScene(true); });
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    // Just left of the box base, on the side away from the light, the floor darkens.
    const float offLeft = ContactShadowMeanBrightness(off, 64u, 24u, 44u, 32u, 52u);
    const float onLeft  = ContactShadowMeanBrightness(on, 64u, 24u, 44u, 32u, 52u);
    std::printf("M11.1 contact shadows: floor beside the box %.2f off, %.2f on\n",
        (double)offLeft, (double)onLeft);
    TEST_ASSERT_TRUE(onLeft < offLeft - 1.0f);

    // The floor away from the box darkens less than the part right beside it: the box is the
    // additional occluder the stage finds.
    const float onFar = ContactShadowMeanBrightness(on, 64u, 2u, 44u, 12u, 52u);
    TEST_ASSERT_TRUE(onLeft < onFar);

    // A sky-tagged pixel is returned unchanged.
    const size_t skyPixel = ((size_t)8u * 64u + 8u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 0u], (int)on[skyPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 1u], (int)on[skyPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 2u], (int)on[skyPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[skyPixel + 3u], (int)on[skyPixel + 3u]);

    // The first-person overlay range (depth below 0.1) is returned unchanged.
    const size_t overlayPixel = ((size_t)39u * 64u + 43u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 0u], (int)on[overlayPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 1u], (int)on[overlayPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 2u], (int)on[overlayPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[overlayPixel + 3u], (int)on[overlayPixel + 3u]);
}

TEST(std3DBabylonGpu, ContactShadowsLeaveTheHudLayerPixelExact)
{
    if ( !OpenGpuStd3D(64u, 64u) )
    {
        TEST_IGNORE_MESSAGE("no SDL GPU device");
    }

    UseContactShadowOnly();
    SetContactShadowCamera();

    const auto drawScene = []
    {
        DrawContactShadowScene(true);
        std3D_BeginHudLayer();
        DrawQuad(nullptr, (Std3DRenderState)0, 28.0f, 0.0f, 30.0f, 64.0f, 0.25f, 0xFF00FF00u);
        std3D_EndHudLayer();
    };

    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
    RenderScene(drawScene);
    const std::vector<std::byte> off = CapturePresented(64u, 64u);

    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    RenderScene(drawScene);
    const std::vector<std::byte> on = CapturePresented(64u, 64u);

    const size_t hudPixel = ((size_t)32u * 64u + 29u) * 4u;
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 0u], (int)on[hudPixel + 0u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 1u], (int)on[hudPixel + 1u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 2u], (int)on[hudPixel + 2u]);
    TEST_ASSERT_EQUAL_INT((int)off[hudPixel + 3u], (int)on[hudPixel + 3u]);
}

TEST(std3DBabylonGpu, ContactShadowLightDirectionMatchesTheCamera)
{
    // Looking straight down: the world up light falls along the view's -forward axis.
    const float aDown[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, -1.0f, 0.0f
    };
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    TEST_ASSERT_TRUE(stdRenderBabylon_GetContactLightDirectionForTest(aDown, &x, &y, &z));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, 0.4967f, x);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, 0.2868f, y);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, -0.8192f, z);

    // Looking horizontally along +Y with world Z up.
    const float aAlongY[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f
    };
    TEST_ASSERT_TRUE(stdRenderBabylon_GetContactLightDirectionForTest(aAlongY, &x, &y, &z));
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, 0.4967f, x);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, 0.8192f, y);
    TEST_ASSERT_FLOAT_WITHIN(1.0e-4f, 0.2868f, z);

    TEST_ASSERT_FALSE(stdRenderBabylon_GetContactLightDirectionForTest(nullptr, &x, &y, &z));
}

TEST(std3DBabylonGpu, ContactShadowsOptionDefaultsAndSetting)
{
    // The setter refuses before the frame owner is open.
    TEST_ASSERT_FALSE(stdRenderBabylon_SetContactShadows(true));

    if ( stdConfig_HasStarted() )
    {
        stdConfig_Shutdown();
    }
    std::remove(kContactShadowConfigPath);

    const StdConfigFallback fallback = { ContactShadowFallbackHasStarted,
        ContactShadowFallbackGetInt, ContactShadowFallbackGetFloat, ContactShadowFallbackGetStr };
    stdConfig_SetFallback(&fallback);
    TEST_ASSERT_TRUE(stdJSON_Startup());
    TEST_ASSERT_TRUE(stdConfig_Startup(kContactShadowConfigPath));
    TEST_ASSERT_FALSE(stdConfig_Contains(kContactShadowKey));

    UseNullDevice();
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));

    // The Enhanced profile defaults the stage on, Retail and QOL-off builds off.
#ifdef J3D_QOL_IMPROVEMENTS
    TEST_ASSERT_TRUE(stdRenderBabylon_GetContactShadows());
#else
    TEST_ASSERT_FALSE(stdRenderBabylon_GetContactShadows());
#endif

    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(false));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetContactShadows());
    TEST_ASSERT_TRUE(stdRenderBabylon_SetContactShadows(true));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetContactShadows());
    stdRenderBabylon_Close();

    // A config value overrides the profile's default when the frame owner opens.
    TEST_ASSERT_TRUE(stdConfig_SetBool(kContactShadowKey, false));
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));
    TEST_ASSERT_FALSE(stdRenderBabylon_GetContactShadows());
    stdRenderBabylon_Close();

    TEST_ASSERT_TRUE(stdConfig_SetBool(kContactShadowKey, true));
    TEST_ASSERT_TRUE(stdRenderBabylon_Open(std3DBabylonTest_pDevice.get()));
    TEST_ASSERT_TRUE(stdRenderBabylon_GetContactShadows());
    stdRenderBabylon_Close();
    DropDevice();

    stdConfig_Shutdown();
    stdJSON_Shutdown();
    std::remove(kContactShadowConfigPath);
}
